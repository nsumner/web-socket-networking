/////////////////////////////////////////////////////////////////////////////
//                         Single Threaded Networking
//
// This file is distributed under the MIT License. See the LICENSE file
// for details.
/////////////////////////////////////////////////////////////////////////////


#include "Client.h"


#include <deque>
#include <sstream>

using networking::Client;


/////////////////////////////////////////////////////////////////////////////
// Private Client API
/////////////////////////////////////////////////////////////////////////////


#ifdef __EMSCRIPTEN__

#include <emscripten/websocket.h>


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

  void disconnect();

  void reportError(std::string_view message) const;

  void update() {}

  void send(std::string message);

  std::ostringstream& getIncomingStream() { return incomingMessage; }

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
  std::ostringstream incomingMessage;
  std::deque<std::string> writeBuffer;
};


}

/////////////////////////////////////////////////////////////////////////////
// Emscripten Websocket Helpers
/////////////////////////////////////////////////////////////////////////////


EM_BOOL
Client::ClientImpl::onCloseHelper(int eventType,
                                  const EmscriptenWebSocketCloseEvent* websocketEvent,
                                  void* implAsVoid) {
  Client::ClientImpl* impl = reinterpret_cast<Client::ClientImpl*>(implAsVoid);
  impl->closed = true;
  return EM_TRUE;
}


EM_BOOL
Client::ClientImpl::onOpenHelper(int eventType,
                                 const EmscriptenWebSocketOpenEvent* websocketEvent,
                                 void* implAsVoid) {
  Client::ClientImpl* impl = reinterpret_cast<Client::ClientImpl*>(implAsVoid);
  if (!impl->writeBuffer.empty()) {
    for (const auto& message : impl->writeBuffer) {
      auto result = emscripten_websocket_send_utf8_text(impl->websocket, message.c_str());
      if (result) {
        impl->disconnect();
        break;
      }
    }
    impl->writeBuffer.clear();
  }
  return EM_TRUE;
}


EM_BOOL
Client::ClientImpl::onErrorHelper(int eventType,
                                  const EmscriptenWebSocketErrorEvent* websocketEvent,
                                  void* implAsVoid) {
  Client::ClientImpl* impl = reinterpret_cast<Client::ClientImpl*>(implAsVoid);
  impl->disconnect();
  return EM_TRUE;
}


EM_BOOL
Client::ClientImpl::onMessageHelper(int eventType,
                                    const EmscriptenWebSocketMessageEvent* websocketEvent,
                                    void* implAsVoid) {
  Client::ClientImpl* impl = reinterpret_cast<Client::ClientImpl*>(implAsVoid);
  if (!websocketEvent->isText) {
    printf("Non text socket info!\n");
    impl->disconnect();
  }

  impl->incomingMessage.write(reinterpret_cast<const char*>(websocketEvent->data),
                              websocketEvent->numBytes);
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
  unsigned short readyState = 0;
  auto result = emscripten_websocket_get_ready_state(websocket, &readyState);
  if (result) {
    disconnect();
    return;
  }

  if (readyState == OPEN) {
    auto result = emscripten_websocket_send_utf8_text(websocket, message.c_str());
    if (result) {
      disconnect();
    }
  } else {
    writeBuffer.push_back(std::move(message));
  }
}


#else

#include <boost/asio.hpp>
#include <boost/beast.hpp>


namespace networking {


class Client::ClientImpl {
public:
  ClientImpl(std::string_view address, std::string_view port)
    : hostAddress{address.data(), address.size()} {
    boost::asio::ip::tcp::resolver resolver{ioService};
    connect(resolver.resolve(address, port));
  }

  void disconnect();

  void reportError(std::string_view message) const;

  void update() { ioService.poll(); }

  void send(std::string message);

  std::ostringstream& getIncomingStream() { return incomingMessage; }

  bool isClosed() const { return closed; }

private:

  void connect(boost::asio::ip::tcp::resolver::iterator endpoint);

  void handshake();

  void readMessage();

  bool closed = false;
  boost::asio::io_service ioService{};
  boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket{ioService};
  std::string hostAddress;
  boost::beast::multi_buffer readBuffer;
  std::ostringstream incomingMessage;
  std::deque<std::string> writeBuffer;
};


}


void
Client::ClientImpl::disconnect() {
  closed = true;
  websocket.async_close(boost::beast::websocket::close_code::normal,
    [] (auto errorCode) {
      // Swallow errors while closing.
    });
}


void
Client::ClientImpl::connect(boost::asio::ip::tcp::resolver::iterator endpoint) {
  boost::asio::async_connect(websocket.next_layer(), endpoint,
    [this] (auto errorCode, auto) {
      if (!errorCode) {
        this->handshake();
      } else {
        reportError("Unable to connect.");
      }
    });
}


void
Client::ClientImpl::handshake() {
  websocket.async_handshake(hostAddress, "/",
    [this] (auto errorCode) {
      if (!errorCode) {
        this->readMessage();
      } else {
        reportError("Unable to handshake.");
      }
    });
}


void
Client::ClientImpl::readMessage() {
  websocket.async_read(readBuffer,
    [this] (auto errorCode, std::size_t size) {
      if (!errorCode) {
        if (size > 0) {
          auto message = boost::beast::buffers_to_string(readBuffer.data());
          incomingMessage.write(message.c_str(), message.size());
          readBuffer.consume(readBuffer.size());
          this->readMessage();
        }
      } else {
        reportError("Unable to read.");
        this->disconnect();
      }
    });
}


void
Client::ClientImpl::send(std::string message) {
  writeBuffer.emplace_back(std::move(message));
  websocket.async_write(boost::asio::buffer(writeBuffer.back()),
    [this] (auto errorCode, std::size_t /*size*/) {
      if (!errorCode) {
        writeBuffer.pop_front();
      } else {
        reportError("Unable to write.");
        disconnect();
      }
    });
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
  auto& stream = impl->getIncomingStream();
  auto result = stream.str();
  stream.str(std::string{});
  stream.clear();
  return result;
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
