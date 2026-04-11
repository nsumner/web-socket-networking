/////////////////////////////////////////////////////////////////////////////
//                         Single Threaded Networking
//
// This file is distributed under the MIT License. See the LICENSE file
// for details.
/////////////////////////////////////////////////////////////////////////////


#include "Client.h"


#include <deque>
#include <ranges>
#include <utility>

using networking::Client;


/////////////////////////////////////////////////////////////////////////////
// Private Client API
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Channels for emscripten
/////////////////////////////////////////////////////////////////////////////


#ifdef __EMSCRIPTEN__

#include <emscripten/websocket.h>


class SimpleChannel {
public:
  void send(std::string value) {
    queue.push_back(std::move(value));
  }

  std::deque<std::string> drain() {
    std::deque<std::string> result = std::move(queue);
    queue = std::deque<std::string>{};
    return result;
  }

  bool empty() const { return queue.empty(); }

private:
  std::deque<std::string> queue;
};


static std::string
makeHostAddress(std::string_view address, std::string_view port) {
  std::string hostAddress;
  // TODO: Replace the protocol with ws:// if needed?
  hostAddress.reserve(address.size() + port.size() + 1);
  hostAddress.append(address);
  hostAddress.push_back(':');
  hostAddress.append(port);
  return hostAddress;
}


namespace networking {


class Client::ClientImpl {
public:
  ClientImpl(std::string_view address, std::string_view port)
    : hostAddress{makeHostAddress(address, port)},
      attrs{hostAddress.c_str(), nullptr, EM_TRUE},
      websocket{connect(attrs)}
    { }

  ~ClientImpl() {
    if (websocket > 0) {
      emscripten_websocket_delete(websocket);
    }
  }

  void disconnect();

  void reportError(std::string_view message) const;

  void update() {}

  void send(std::string message);

  std::deque<std::string> receive();

  bool isClosed() const { return closed; }

private:

  EMSCRIPTEN_WEBSOCKET_T connect(EmscriptenWebSocketCreateAttributes& attrs);

  // Static helpers make it easier to interface with the C style callbacks
  // of the emscripten websocket APIs.

  static EM_BOOL
  onCloseHelper(int eventType,
                const EmscriptenWebSocketCloseEvent* websocketEvent,
                void* impl);

  static EM_BOOL
  onOpenHelper(int eventType,
               const EmscriptenWebSocketOpenEvent* websocketEvent,
               void* impl);

  static EM_BOOL
  onErrorHelper(int eventType,
                const EmscriptenWebSocketErrorEvent* websocketEvent,
                void* impl);

  static EM_BOOL
  onMessageHelper(int eventType,
                  const EmscriptenWebSocketMessageEvent* websocketEvent,
                  void* impl);

  bool closed = false;
  const std::string hostAddress;
  EmscriptenWebSocketCreateAttributes attrs;
  EMSCRIPTEN_WEBSOCKET_T websocket;

  SimpleChannel incoming;
  SimpleChannel outgoing;
};


}

/////////////////////////////////////////////////////////////////////////////
// Emscripten Websocket Helpers
/////////////////////////////////////////////////////////////////////////////


EM_BOOL
Client::ClientImpl::onCloseHelper(int eventType,
                                  const EmscriptenWebSocketCloseEvent* websocketEvent,
                                  void* implAsVoid) {
  static_cast<ClientImpl*>(implAsVoid)->closed = true;
  return EM_TRUE;
}


EM_BOOL
Client::ClientImpl::onOpenHelper(int eventType,
                                 const EmscriptenWebSocketOpenEvent* websocketEvent,
                                 void* implAsVoid) {
  auto* impl = static_cast<ClientImpl*>(implAsVoid);

  for (const auto& msg : impl->outgoing.drain()) {
    emscripten_websocket_send_utf8_text(impl->websocket, msg.c_str());
  }

  return EM_TRUE;
}


EM_BOOL
Client::ClientImpl::onErrorHelper(int eventType,
                                  const EmscriptenWebSocketErrorEvent* websocketEvent,
                                  void* implAsVoid) {
  static_cast<ClientImpl*>(implAsVoid)->disconnect();
  return EM_TRUE;
}


EM_BOOL
Client::ClientImpl::onMessageHelper(int eventType,
                                    const EmscriptenWebSocketMessageEvent* websocketEvent,
                                    void* implAsVoid) {
  auto* impl = static_cast<ClientImpl*>(implAsVoid);

  if (!websocketEvent->isText) {
    impl->disconnect();
    return EM_TRUE;
  }

  impl->incoming.send(
    std::string(reinterpret_cast<const char*>(websocketEvent->data),
                                              websocketEvent->numBytes)
  );

  return EM_TRUE;
}


/////////////////////////////////////////////////////////////////////////////
// Emscripten Client Impl
/////////////////////////////////////////////////////////////////////////////


EMSCRIPTEN_WEBSOCKET_T
Client::ClientImpl::connect(EmscriptenWebSocketCreateAttributes& attrs) {
  EMSCRIPTEN_WEBSOCKET_T websocket = emscripten_websocket_new(&attrs);
  emscripten_websocket_set_onopen_callback(websocket,    this, onOpenHelper);
  emscripten_websocket_set_onerror_callback(websocket,   this, onErrorHelper);
  emscripten_websocket_set_onclose_callback(websocket,   this, onCloseHelper);
  emscripten_websocket_set_onmessage_callback(websocket, this, onMessageHelper);
  return websocket;
}


void
Client::ClientImpl::disconnect() {
  closed = true;
  auto _ = emscripten_websocket_close(websocket, 1000, "Disconnecting.");
  if (_) {
    // Swallow errors while closing.
  }
}


enum WSReadyStates : unsigned short {
  CONNECTING = 0,
  OPEN = 1,
  CLOSING = 2,
  CLOSED = 3
};


void
Client::ClientImpl::send(std::string message) {
  if (closed) {
    return;
  }

  unsigned short readyState = 0;
  if (auto result = emscripten_websocket_get_ready_state(websocket, &readyState)) {
    disconnect();
    return;
  }

  if (readyState == OPEN) {
    if (auto result = emscripten_websocket_send_utf8_text(websocket, message.c_str())) {
      disconnect();
    }
  } else {
    outgoing.send(std::move(message));
  }
}


std::deque<std::string>
Client::ClientImpl::receive() {
  return incoming.drain();
}


#else

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>


namespace networking {


class Client::ClientImpl {
public:
  ClientImpl(std::string_view address, std::string_view port)
    : ioContext{},
      resolver{ioContext},
      websocket{ioContext},
      outgoing{ioContext, 1024},
      incoming{},
      hostAddress{address},
      hostPort{port} {
    // TODO: Other command line args?
    boost::asio::co_spawn(ioContext, session(), boost::asio::detached);
  }

  ~ClientImpl() {
    disconnect();
    // Run at the end to drain the context
    ioContext.run();
  }

  void disconnect();

  void reportError(std::string_view message) const;

  void update() { ioContext.poll(); }

  void send(std::string message);

  std::deque<std::string> receive();

  bool isClosed() const { return closed; }

private:

  boost::asio::awaitable<void> session();

  boost::asio::awaitable<void> reader();

  boost::asio::awaitable<void> writer();

  boost::asio::io_context ioContext;
  boost::asio::ip::tcp::resolver resolver;
  boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket;

  boost::asio::experimental::channel<void(boost::system::error_code, std::string)> outgoing;
  std::deque<std::string> incoming;

  bool closed = false;
  std::string hostAddress;
  std::string hostPort;
};


}


boost::asio::awaitable<void>
Client::ClientImpl::session() {
  try {
    auto endpoint = co_await resolver.async_resolve(hostAddress, hostPort, boost::asio::use_awaitable);

    co_await boost::asio::async_connect(websocket.next_layer(), endpoint, boost::asio::use_awaitable);

    co_await websocket.async_handshake(hostAddress, "/", boost::asio::use_awaitable);

    boost::asio::co_spawn(ioContext, reader(), boost::asio::detached);
    co_await writer();

  } catch (const boost::system::system_error& e) {
    reportError(e.what());
    disconnect();
  } catch (...) {
    disconnect();
  }
}


boost::asio::awaitable<void>
Client::ClientImpl::reader() {
  try {
    while (!isClosed()) {
      boost::beast::flat_buffer buffer;
      co_await websocket.async_read(buffer, boost::asio::use_awaitable);
      incoming.push_back(boost::beast::buffers_to_string(buffer.data()));
    }
  } catch (const boost::system::system_error& e) {
    reportError(e.what());
    disconnect();
  } catch (...) {
    disconnect();
  }
}


boost::asio::awaitable<void>
Client::ClientImpl::writer() {
  try {
    while (!isClosed()) {
      std::string message = co_await outgoing.async_receive(boost::asio::use_awaitable);
      co_await websocket.async_write(boost::asio::buffer(message), boost::asio::use_awaitable);
    }
  } catch (const boost::system::system_error& e) {
    reportError(e.what());
    disconnect();
  } catch (...) {
    disconnect();
  }
}


void
Client::ClientImpl::disconnect() {
  if (closed) {
    return;
  }

  closed = true;
  outgoing.close();
  if (websocket.is_open()) {
    boost::beast::get_lowest_layer(websocket).cancel();
  }
}



void
Client::ClientImpl::send(std::string message) {
  if (isClosed() || message.empty()) {
    return;
  }

  if (!outgoing.try_send(boost::system::error_code{}, std::move(message))) {
    reportError("Send buffer full");
  }
}


std::deque<std::string>
Client::ClientImpl::receive() {
  return std::exchange(incoming, std::deque<std::string>{});
}


#endif


/////////////////////////////////////////////////////////////////////////////
// Core Client
/////////////////////////////////////////////////////////////////////////////


Client::Client(std::string_view address, std::string_view port)
  : impl{std::make_unique<ClientImpl>(address, port)}
    { }


Client::~Client() = default;


void
Client::ClientImpl::reportError(std::string_view /*message*/) const {
  // Swallow errors by default.
  // This can still provide a useful entrypoint for debugging.
}


void
Client::update() {
  impl->update();
}


std::string
Client::receive() {
  return impl->receive()
      | std::views::join
      | std::ranges::to<std::string>();
}


void
Client::send(std::string message) {
  if (message.empty()) {
    return;
  }
  impl->send(std::move(message));
}


bool
Client::isDisconnected() const noexcept {
  return impl->isClosed();
}
