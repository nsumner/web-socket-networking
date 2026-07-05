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
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/beast.hpp>

#include <cassert>
#include <chrono>

namespace asio = boost::asio;
namespace beast = boost::beast;

using asio::as_tuple;
using asio::awaitable;
using asio::use_awaitable;
using namespace asio::experimental::awaitable_operators;
using namespace std::chrono_literals;


namespace networking {


class Client::ClientImpl {
public:
  ClientImpl(std::string_view address, std::string_view port)
    : websocket{ioContext},
      wakeTimer{ioContext, std::chrono::steady_clock::time_point::max()},
      hostAddress{address},
      hostPort{port} {
    asio::co_spawn(ioContext, session(),
      asio::bind_cancellation_slot(stopSignal.slot(),
        [this](std::exception_ptr error) {
          if (error) {
            reportError("Session ended with an exception");
          }
          closed = true;
          sessionDone = true;
        }));
  }

  ~ClientImpl() {
    stopSignal.emit(asio::cancellation_type::terminal);
    // Drive the context until the session coroutine has completed, so its
    // frame and every queued message are destroyed deterministically.
    while (!sessionDone) {
      ioContext.restart();
      if (ioContext.run() == 0 && !sessionDone) {
        // No further progress is possible. This indicates the session is
        // suspended on something cancellation cannot reach — a bug.
        assert(false && "session coroutine failed to complete during shutdown");
        break;
      }
    }
  }

  void reportError(std::string_view message) const;

  void update() { ioContext.poll(); }

  void send(std::string message);

  std::deque<std::string> receive() {
    return std::exchange(incoming, std::deque<std::string>{});
  }

  bool isClosed() const { return closed; }

private:
  awaitable<void> session();
  awaitable<void> reader();
  awaitable<void> writer();

  asio::io_context ioContext;
  beast::websocket::stream<asio::ip::tcp::socket> websocket;

  // The timer is parked forever and cancelled to signal "queue is not empty".
  asio::steady_timer wakeTimer;
  std::deque<std::string> outbound;
  std::deque<std::string> incoming;

  asio::cancellation_signal stopSignal;
  bool closed = false;
  bool sessionDone = false;
  std::string hostAddress;
  std::string hostPort;
};


}


boost::asio::awaitable<void>
Client::ClientImpl::session() {
  asio::ip::tcp::resolver resolver{ioContext};
  // Note: unlike the async form, this call is not cancellable, so a slow or
  // unreachable DNS server stalls the first update() (or the destructor drain,
  // if never pumped) for the system resolver timeout.
  boost::system::error_code resolveError;
  auto endpoints = resolver.resolve(hostAddress, hostPort, resolveError);
  if (resolveError) {
    reportError("Resolve failed");
    co_return;
  }

  auto [connectError, endpoint] =
    co_await asio::async_connect(websocket.next_layer(), endpoints,
                                 as_tuple(use_awaitable));
  (void)endpoint;
  if (connectError) {
    reportError("Connect failed");
    co_return;
  }

  auto [handshakeError] =
    co_await websocket.async_handshake(hostAddress, "/",
                                       as_tuple(use_awaitable));
  if (handshakeError) {
    reportError("Handshake failed");
    co_return;
  }

  co_await (reader() || writer());

  // Best-effort graceful close, skipped when cancelled (client destruction)
  // so teardown never depends on the peer. Bounded so an unresponsive server
  // cannot stall. A dead transport fails at once.
  auto state = co_await asio::this_coro::cancellation_state;
  if (state.cancelled() == asio::cancellation_type::none
      && websocket.is_open()) {
    co_await websocket.async_close(beast::websocket::close_code::normal,
                                   asio::cancel_after(1s, as_tuple(use_awaitable)));
  }
}


boost::asio::awaitable<void>
Client::ClientImpl::reader() {
  beast::flat_buffer buffer;
  while (true) {
    auto [error, bytes] =
      co_await websocket.async_read(buffer, as_tuple(use_awaitable));
    (void)bytes;
    if (error) {
      co_return;
    }
    incoming.push_back(beast::buffers_to_string(buffer.data()));
    buffer.consume(buffer.size());
  }
}


boost::asio::awaitable<void>
Client::ClientImpl::writer() {
  auto cancelState = co_await asio::this_coro::cancellation_state;
  while (cancelState.cancelled() == asio::cancellation_type::none) {
    if (outbound.empty()) {
      // Park until send() cancels the timer or we are cancelled.
      // The loop condition distinguishes the two.
      co_await wakeTimer.async_wait(as_tuple(use_awaitable));
      continue;
    }
    std::string message = std::move(outbound.front());
    outbound.pop_front();
    auto [error, bytes] =
      co_await websocket.async_write(asio::buffer(message),
                                     as_tuple(use_awaitable));
    (void)bytes;
    if (error) {
      co_return;
    }
  }
}


void
Client::ClientImpl::send(std::string message) {
  if (closed || message.empty()) {
    return;
  }
  outbound.push_back(std::move(message));
  wakeTimer.cancel_one();
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
