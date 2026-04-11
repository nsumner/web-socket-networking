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
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast.hpp>


using namespace std::string_literals;
using networking::Message;
using networking::Server;
using networking::ServerImpl;
using networking::ServerImplDeleter;



namespace networking {


/////////////////////////////////////////////////////////////////////////////
// Private Server API
/////////////////////////////////////////////////////////////////////////////


class Channel;


class ServerImpl {
public:
  using ChannelMap =
    std::unordered_map<Connection, std::shared_ptr<Channel>, ConnectionHash>;

  ServerImpl(Server& server, unsigned short port, std::string httpMessage)
   : server{server},
     endpoint( boost::asio::ip::tcp::v4(), port),
     acceptor{ioContext, endpoint},
     httpMessage{std::move(httpMessage)} {
    boost::asio::co_spawn(ioContext, acceptLoop(), boost::asio::detached);
  }

  ~ServerImpl();

  boost::asio::awaitable<void> acceptLoop();

  void registerChannel(Channel& channel);
  void reportError(std::string_view message);


  Server& server;
  boost::asio::io_context ioContext{};
  boost::asio::ip::tcp::endpoint endpoint;
  boost::asio::ip::tcp::acceptor acceptor;
  boost::beast::http::string_body::value_type httpMessage;

  ChannelMap channels;
  std::deque<Message> incoming;
};


/////////////////////////////////////////////////////////////////////////////
// Channels (connections private to the implementation)
/////////////////////////////////////////////////////////////////////////////


class Channel : public std::enable_shared_from_this<Channel> {
public:
  Channel(boost::asio::ip::tcp::socket socket, ServerImpl& serverImpl)
    : connection{reinterpret_cast<uintptr_t>(this)},
      serverImpl{serverImpl},
      websocket{std::move(socket)},
      sendChannel{websocket.get_executor(), 1024}
      { }

  void send(std::string outgoing);
  void disconnect();

  [[nodiscard]] Connection getConnection() const noexcept { return connection; }

  [[nodiscard]] boost::asio::awaitable<void> run(boost::beast::http::request<boost::beast::http::string_body> req);
  [[nodiscard]] boost::asio::awaitable<void> sendLoop();

private:
  Connection connection;
  ServerImpl &serverImpl;

  boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket;

  boost::asio::experimental::channel<void(boost::system::error_code, std::string)> sendChannel;
};


ServerImpl::~ServerImpl() {
  acceptor.close();

  for (auto& [_, ch] : channels) {
    ch->disconnect();
  }

  // Run at the end to drain the context
  ioContext.run();
  channels.clear();
}

}

using networking::Channel;


boost::asio::awaitable<void>
Channel::run(boost::beast::http::request<boost::beast::http::string_body> req) {
  auto self = shared_from_this(); //Pin the channel
  try {
    co_await websocket.async_accept(req, boost::asio::use_awaitable);

    serverImpl.registerChannel(*this);

    boost::asio::co_spawn(serverImpl.ioContext,
      [self]() -> boost::asio::awaitable<void> {
        co_await self->sendLoop();
      },boost::asio::detached);

    boost::beast::flat_buffer buffer;

    while (true) {
      co_await websocket.async_read(buffer, boost::asio::use_awaitable);

      auto msg = boost::beast::buffers_to_string(buffer.data());
      buffer.consume(buffer.size());

      serverImpl.incoming.push_back({connection, std::move(msg)});
    }
  } catch (const boost::system::system_error& e) {
    serverImpl.reportError(e.what());
    serverImpl.server.disconnect(connection);
  } catch (...) {
    serverImpl.server.disconnect(connection);
  }
}

boost::asio::awaitable<void>
Channel::sendLoop() {
  auto self = shared_from_this(); // Pin the channel
  try {
    while (true) {
      auto [ec, msg] = co_await sendChannel.async_receive(boost::asio::as_tuple(boost::asio::use_awaitable));

      if (ec || !websocket.is_open()) {
        break;
      }

      co_await websocket.async_write(boost::asio::buffer(msg), boost::asio::use_awaitable);
    }
  } catch (const boost::system::system_error& e) {
    serverImpl.reportError(e.what());
    serverImpl.server.disconnect(connection);
  } catch (...) {
    serverImpl.server.disconnect(connection);
  }
}

void
Channel::send(std::string msg) {
  if (!msg.empty()) {
    sendChannel.async_send({}, std::move(msg), boost::asio::detached);
  }
}

void
Channel::disconnect() {
  boost::beast::error_code ec;
  websocket.next_layer().cancel(ec);
  websocket.close(boost::beast::websocket::close_code::normal, ec);
  sendChannel.close();
}


////////////////////////////////////////////////////////////////////////////////
// Basic HTTP Request Handling
////////////////////////////////////////////////////////////////////////////////


boost::asio::awaitable<void>
handleSession(ServerImpl& impl, boost::asio::ip::tcp::socket socket) {
  boost::beast::flat_buffer buffer;
  boost::beast::http::request<boost::beast::http::string_body> req;

  try {
    co_await boost::beast::http::async_read(socket, buffer, req, boost::asio::use_awaitable);

    if (boost::beast::websocket::is_upgrade(req)) {
      auto channel = std::make_shared<Channel>(std::move(socket), impl);

      boost::asio::co_spawn(impl.ioContext,
                            [channel, req = std::move(req)]() mutable -> boost::asio::awaitable<void> {
                              co_await channel->run(std::move(req));
                            }, boost::asio::detached);

      co_return;
    }

    // Handle HTTP (index.html only)
    auto const badRequest = [&](std::string_view why) {
      boost::beast::http::response<boost::beast::http::string_body> res{boost::beast::http::status::bad_request, req.version()};
      res.set(boost::beast::http::field::content_type, "text/html");
      res.body() = std::string(why);
      res.prepare_payload();
      return res;
    };

    if (req.method() != boost::beast::http::verb::get && req.method() != boost::beast::http::verb::head) {
      auto res = badRequest("Unknown HTTP-method");
      co_await boost::beast::http::async_write(socket, res, boost::asio::use_awaitable);
      co_return;
    }

    std::string body = impl.httpMessage;

    boost::beast::http::response<boost::beast::http::string_body> res{
      std::piecewise_construct,
      std::make_tuple(body),
      std::make_tuple(boost::beast::http::status::ok, req.version())
    };

    res.set(boost::beast::http::field::content_type, "text/html");
    res.prepare_payload();

    co_await boost::beast::http::async_write(socket, res, boost::asio::use_awaitable);

  } catch (const boost::system::system_error& e) {
    impl.reportError(e.what());
  } catch (...) {
    impl.reportError("HTTP session error");
  }
}


/////////////////////////////////////////////////////////////////////////////
// Accept Loop
/////////////////////////////////////////////////////////////////////////////

boost::asio::awaitable<void>
ServerImpl::acceptLoop() {
  while (acceptor.is_open()) {
    try {
      boost::asio::ip::tcp::socket socket = co_await acceptor.async_accept(boost::asio::use_awaitable);

      boost::asio::co_spawn(ioContext,
                    handleSession(*this, std::move(socket)),
                    boost::asio::detached);

    } catch (const boost::system::system_error& e) {
      if (e.code() == boost::asio::error::operation_aborted) {
        co_return;
      }
      reportError("Accept boost error");
    } catch (...) {
      reportError("Accept error");
    }
  }
}


/////////////////////////////////////////////////////////////////////////////
// Hidden Server implementation
/////////////////////////////////////////////////////////////////////////////


void
ServerImpl::registerChannel(Channel& channel) {
  auto connection = channel.getConnection();
  channels[connection] = channel.shared_from_this();
  server.connectionHandler->handleConnect(connection);
}


void
ServerImpl::reportError(std::string_view message) {
  // Swallow errors....
}

void
ServerImplDeleter::operator()(ServerImpl* serverImpl) {
  // NOTE: This is a custom deleter used to help hide the impl class. Thus
  // it must use a raw delete.
  // NOLINTNEXTLINE (cppcoreguidelines-owning-memory)
  delete serverImpl;
}


/////////////////////////////////////////////////////////////////////////////
// Core Server
/////////////////////////////////////////////////////////////////////////////

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
    channel->disconnect();
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

