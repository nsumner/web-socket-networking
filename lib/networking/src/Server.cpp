/////////////////////////////////////////////////////////////////////////////
//                         Single Threaded Networking
//
// This file is distributed under the MIT License. See the LICENSE file
// for details.
/////////////////////////////////////////////////////////////////////////////


#ifdef __EMSCRIPTEN__

// The Servers are incompatible with web sockets in the browser, so disable them

#else


#include "Server.h"


#include <boost/asio.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/beast.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>


namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websock = boost::beast::websocket;

using asio::as_tuple;
using asio::awaitable;
using asio::use_awaitable;
using namespace asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

using networking::Connection;
using networking::Message;
using networking::Server;
using networking::ServerImpl;
using networking::ServerImplDeleter;


namespace networking {


class Channel;


/////////////////////////////////////////////////////////////////////////////
// Private Server API
/////////////////////////////////////////////////////////////////////////////


class ServerImpl {
public:
  using ChannelMap =
    std::unordered_map<Connection, std::shared_ptr<Channel>, ConnectionHash>;

  ServerImpl(Server& server, unsigned short port, std::string httpMessage);
  ~ServerImpl();

  // Spawn a coroutine whose lifetime is tracked in activeTasks so that the
  // destructor can cancel it and then run the context until it has provably
  // completed. Returns the signal used to request cancellation.
  template <typename Task, typename OnDone>
  std::shared_ptr<asio::cancellation_signal>
  spawnTracked(Task&& task, OnDone onDone) {
    const uint64_t id = nextTaskId++;
    auto signal = std::make_shared<asio::cancellation_signal>();
    activeTasks.emplace(id, signal);
    asio::co_spawn(ioContext, std::forward<Task>(task),
      asio::bind_cancellation_slot(signal->slot(),
        [this, id, onDone = std::move(onDone)](std::exception_ptr error) {
          if (error) {
            reportError("Coroutine ended with an exception");
          }
          activeTasks.erase(id);
          onDone();
        }));
    return signal;
  }

  awaitable<void> acceptLoop();
  awaitable<void> httpSession(asio::ip::tcp::socket socket);
  void startChannel(asio::ip::tcp::socket socket,
                    http::request<http::string_body> request);

  void registerChannel(std::shared_ptr<Channel> channel);
  void channelDone(Connection connection);
  void reportError(std::string_view message);

  Server& server;
  asio::io_context ioContext{};
  asio::ip::tcp::acceptor acceptor;
  http::string_body::value_type httpMessage;

  uintptr_t nextConnectionId = 1;
  uint64_t nextTaskId = 1;
  std::unordered_map<uint64_t, std::shared_ptr<asio::cancellation_signal>>
    activeTasks;
  bool stopping = false;

  ChannelMap channels;
  std::deque<Message> incoming;
};


/////////////////////////////////////////////////////////////////////////////
// Channels (connections private to the implementation)
/////////////////////////////////////////////////////////////////////////////


class Channel {
public:
  Channel(asio::ip::tcp::socket socket,
          Connection connection,
          ServerImpl& serverImpl)
    : connection{connection},
      serverImpl{serverImpl},
      websocket{std::move(socket)},
      wakeTimer{websocket.get_executor(),
                std::chrono::steady_clock::time_point::max()}
      { }

  // The parent coroutine owning this connection: accept the websocket,
  // register, run reader and writer until either finishes (which cancels
  // the other), then attempt a best-effort graceful close.
  [[nodiscard]] awaitable<void>
  run(std::shared_ptr<Channel> self, http::request<http::string_body> request);

  void send(std::string message);
  void requestStop();

  [[nodiscard]] Connection getConnection() const noexcept { return connection; }

  void setStopSignal(std::shared_ptr<asio::cancellation_signal> signal) {
    stopSignal = std::move(signal);
  }

private:
  [[nodiscard]] awaitable<void> reader();
  [[nodiscard]] awaitable<void> writer();

  Connection connection;
  ServerImpl& serverImpl;

  websock::stream<asio::ip::tcp::socket> websocket;

  // The timer is parked forever and cancelled to signal "queue is not empty".
  asio::steady_timer wakeTimer;
  std::deque<std::string> outbound;

  std::shared_ptr<asio::cancellation_signal> stopSignal;
};


awaitable<void>
Channel::run(std::shared_ptr<Channel> self,
             http::request<http::string_body> request) {
  auto [acceptError] =
    co_await websocket.async_accept(request, as_tuple(use_awaitable));
  if (acceptError) {
    co_return;
  }

  // A queued channel async_accept success can still resume here after
  // ~ServerImpl has begun tearing down. Once stopping, no user callbacks may
  // fire, so skip registration (and the handleConnect it would trigger)
  // rather than run during the destructor.
  if (serverImpl.stopping) {
    co_return;
  }

  serverImpl.registerChannel(std::move(self));

  co_await (reader() || writer());

  // Best-effort graceful close. Skipped when this coroutine was cancelled
  // (explicit disconnect or server teardown). Those paths are deliberately
  // abrupt so shutdown never depends on a peer. Bounded so a hostile peer
  // cannot stall. A dead transport fails immediately.
  auto state = co_await asio::this_coro::cancellation_state;
  if (state.cancelled() == asio::cancellation_type::none
      && websocket.is_open()) {
    co_await websocket.async_close(websock::close_code::normal,
                                   asio::cancel_after(1s, as_tuple(use_awaitable)));
  }
}


awaitable<void>
Channel::reader() {
  beast::flat_buffer buffer;
  while (true) {
    auto [error, bytes] =
      co_await websocket.async_read(buffer, as_tuple(use_awaitable));
    (void)bytes;
    if (error) {
      co_return;
    }
    serverImpl.incoming.push_back(
      {connection, beast::buffers_to_string(buffer.data())});
    buffer.consume(buffer.size());
  }
}


awaitable<void>
Channel::writer() {
  auto cancelState = co_await asio::this_coro::cancellation_state;
  while (cancelState.cancelled() == asio::cancellation_type::none) {
    if (outbound.empty()) {
      // Park until send() cancels the timer or cancelled.
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
Channel::send(std::string message) {
  if (message.empty()) {
    return;
  }
  outbound.push_back(std::move(message));
  wakeTimer.cancel_one();
}


void
Channel::requestStop() {
  if (stopSignal) {
    stopSignal->emit(asio::cancellation_type::terminal);
  }
}


/////////////////////////////////////////////////////////////////////////////
// Basic HTTP Request Handling
/////////////////////////////////////////////////////////////////////////////


awaitable<void>
ServerImpl::httpSession(asio::ip::tcp::socket socket) {
  beast::flat_buffer buffer;
  http::request<http::string_body> request;

  auto [readError, readBytes] =
    co_await http::async_read(socket, buffer, request, as_tuple(use_awaitable));
  (void)readBytes;
  if (readError) {
    co_return;
  }

  if (websock::is_upgrade(request)) {
    startChannel(std::move(socket), std::move(request));
    co_return;
  }

  const bool isHead = request.method() == http::verb::head;
  http::response<http::string_body> response{http::status::ok,
                                             request.version()};
  response.set(http::field::content_type, "text/html");
  if (request.method() != http::verb::get && !isHead) {
    response.result(http::status::bad_request);
    response.body() = "Unknown HTTP-method";
    response.prepare_payload();
  } else if (isHead) {
    response.content_length(httpMessage.size());
  } else {
    response.body() = httpMessage;
    response.prepare_payload();
  }

  co_await http::async_write(socket, response, as_tuple(use_awaitable));
}


void
ServerImpl::startChannel(asio::ip::tcp::socket socket,
                         http::request<http::string_body> request) {
  // A queued httpSession read-success can still resume and reach here after
  // ~ServerImpl has begun tearing down. Once stopping, no fresh untracked
  // work may be spawned, so drop the socket instead of starting a Channel.
  if (stopping) {
    return;
  }

  const Connection connection{nextConnectionId++};
  auto channel = std::make_shared<Channel>(std::move(socket), connection, *this);
  auto signal = spawnTracked(
    // The factory lambda keeps the shared_ptr alive for the coroutine's
    // whole lifetime; co_spawn guarantees the captures outlive the frame.
    [channel, request = std::move(request)]() mutable -> awaitable<void> {
      return channel->run(channel, std::move(request));
    },
    [this, connection] { channelDone(connection); });
  channel->setStopSignal(std::move(signal));
}


/////////////////////////////////////////////////////////////////////////////
// Accept Loop
/////////////////////////////////////////////////////////////////////////////


// GCC's -Wmaybe-uninitialized fires on the anonymous as_tuple() result
// carried in the coroutine's frame. The backoff wait below discards its
// error_code, so nothing ever reads the temporary it warns about. The
// diagnostic only appears under optimization, so it is invisible to Debug
// builds. Maybe investigate more later.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

awaitable<void>
ServerImpl::acceptLoop() {
  asio::steady_timer backoff{ioContext};
  while (acceptor.is_open()) {
    auto [error, socket] =
      co_await acceptor.async_accept(as_tuple(use_awaitable));
    if (error == asio::error::operation_aborted) {
      co_return;
    }
    if (error) {
      reportError("Accept error");
      // Back off instead of spinning on persistent errors.
      backoff.expires_after(100ms);
      co_await backoff.async_wait(as_tuple(use_awaitable));
      continue;
    }
    spawnTracked(httpSession(std::move(socket)), [] { });
  }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif


/////////////////////////////////////////////////////////////////////////////
// Hidden Server implementation
/////////////////////////////////////////////////////////////////////////////


ServerImpl::ServerImpl(Server& server,
                       unsigned short port,
                       std::string httpMessage)
  : server{server},
    acceptor{ioContext, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), port}},
    httpMessage{std::move(httpMessage)} {
  spawnTracked(acceptLoop(), [] { });
}


ServerImpl::~ServerImpl() {
  stopping = true;

  boost::system::error_code ignored;
  acceptor.close(ignored);

  for (auto& [id, signal] : activeTasks) {
    signal->emit(asio::cancellation_type::terminal);
  }

  // Drive the context until every tracked coroutine has completed, so that
  // every frame and owned buffer is destroyed.
  while (!activeTasks.empty()) {
    ioContext.restart();
    if (ioContext.run() == 0 && !activeTasks.empty()) {
      // No further progress is possible, so there is a bug.
      // A coroutine suspended on something cancellation cannot reach.
      assert(false && "tracked coroutines failed to complete during shutdown");
      break;
    }
  }

  channels.clear();
}


void
ServerImpl::registerChannel(std::shared_ptr<Channel> channel) {
  const auto connection = channel->getConnection();
  channels[connection] = std::move(channel);
  server.connectionHandler->handleConnect(connection);
}


void
ServerImpl::channelDone(Connection connection) {
  if (stopping) {
    return;
  }
  // erase() returning zero means the connection was never registered or was
  // already removed by an explicit disconnect.
  if (channels.erase(connection) > 0) {
    server.connectionHandler->handleDisconnect(connection);
  }
}


void
ServerImpl::reportError(std::string_view /*message*/) {
  // Swallow errors....
}


void
ServerImplDeleter::operator()(ServerImpl* serverImpl) {
  // NOTE: This is a custom deleter used to help hide the impl class. Thus
  // it must use a raw delete.
  // NOLINTNEXTLINE (cppcoreguidelines-owning-memory)
  delete serverImpl;
}

}


/////////////////////////////////////////////////////////////////////////////
// Core Server
/////////////////////////////////////////////////////////////////////////////


unsigned short
Server::getPort() const {
  return impl->acceptor.local_endpoint().port();
}


void
Server::update() {
  impl->ioContext.poll();
}


std::deque<Message>
Server::receive() {
  std::deque<Message> oldIncoming;
  std::swap(oldIncoming, impl->incoming);
  return oldIncoming;
}


void
Server::send(const std::deque<Message>& messages) {
  for (const auto& message : messages) {
    auto found = impl->channels.find(message.connection);
    if (impl->channels.end() != found) {
      found->second->send(message.text);
    }
  }
}


void
Server::disconnect(Connection connection) {
  auto found = impl->channels.find(connection);
  if (impl->channels.end() != found) {
    // Pin the channel locally while cleaning up.
    auto channel = std::move(found->second);
    impl->channels.erase(found);

    connectionHandler->handleDisconnect(connection);
    channel->requestStop();
  }
}


std::unique_ptr<ServerImpl,ServerImplDeleter>
Server::buildImpl(Server& server,
                  unsigned short port,
                  std::string httpMessage) {
  // NOTE: We are using a custom deleter here so that the impl class can be
  // hidden within the source file rather than exposed in the header. Using
  // a custom deleter means that we need to use a raw `new` rather than using
  // `std::make_unique`.
  auto* impl = new ServerImpl(server, port, std::move(httpMessage));
  return std::unique_ptr<ServerImpl,ServerImplDeleter>(impl);
}

#endif
