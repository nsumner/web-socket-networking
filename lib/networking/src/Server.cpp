/////////////////////////////////////////////////////////////////////////////
//                         Single Threaded Networking
//
// This file is distributed under the MIT License. See the LICENSE file
// for details.
/////////////////////////////////////////////////////////////////////////////


#include "Server.h"

#include <boost/asio.hpp>
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

  ServerImpl(Server& server, unsigned short port, std::string httpMessage)
   : server{server},
     endpoint{boost::asio::ip::tcp::v4(), port},
     ioContext{},
     acceptor{ioContext, endpoint},
     httpMessage{std::move(httpMessage)} {
    listenForConnections();
  }

  void listenForConnections();
  void registerChannel(Channel& channel);
  void reportError(std::string_view message);

  using ChannelMap =
    std::unordered_map<Connection, std::shared_ptr<Channel>, ConnectionHash>;

  Server& server;
  const boost::asio::ip::tcp::endpoint endpoint;
  boost::asio::io_context ioContext;
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
    : disconnected{false},
      connection{reinterpret_cast<uintptr_t>(this)},
      serverImpl{serverImpl},
      streamBuf{},
      websocket{std::move(socket)},
      readBuffer{serverImpl.incoming}
      { }

  void start(boost::beast::http::request<boost::beast::http::string_body>& request);
  void send(std::string outgoing);
  void disconnect();

  [[nodiscard]] Connection getConnection() const noexcept { return connection; }

private:
  void readMessage();
  void afterWrite(std::error_code errorCode, std::size_t size);

  bool disconnected;
  Connection connection;
  ServerImpl &serverImpl;

  boost::beast::flat_buffer streamBuf;
  boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket;

  std::deque<Message> &readBuffer;
  std::deque<std::string> writeBuffer;
};

}

using networking::Channel;


void
Channel::start(boost::beast::http::request<boost::beast::http::string_body>& request) {
  auto self = shared_from_this();
  websocket.async_accept(request,
    [this, self] (std::error_code errorCode) {
      if (!errorCode) {
        serverImpl.registerChannel(*this);
        self->readMessage();
      } else {
        serverImpl.server.disconnect(connection);
      }
    });
}


void
Channel::disconnect() {
  disconnected = true;
  boost::beast::error_code ec;
  websocket.close(boost::beast::websocket::close_reason{}, ec);
}


void
Channel::send(std::string outgoing) {
  if (outgoing.empty()) {
    return;
  }
  writeBuffer.push_back(std::move(outgoing));

  if (1 < writeBuffer.size()) {
    // Note, multiple writes will be chained within asio via `continueSending`,
    // so that callback should be used instead of directly invoking async_write
    // again.
    return;
  }

  websocket.async_write(boost::asio::buffer(writeBuffer.front()),
    [this, self = shared_from_this()] (auto errorCode, std::size_t size) {
      afterWrite(errorCode, size);
    });
}


void
Channel::afterWrite(std::error_code errorCode, std::size_t size) {
  if (errorCode) {
    if (!disconnected) {
      serverImpl.server.disconnect(connection);
    }
    return;
  }

  writeBuffer.pop_front();

  // Continue asynchronously processing any further messages that have been
  // sent.
  if (!writeBuffer.empty()) {
    websocket.async_write(boost::asio::buffer(writeBuffer.front()),
      [this, self = shared_from_this()] (auto errorCode, std::size_t size) {
        afterWrite(errorCode, size);
      });
  }
}


void
Channel::readMessage() {
  auto self = shared_from_this();
  websocket.async_read(streamBuf,
    [this, self] (auto errorCode, std::size_t size) {
      if (!errorCode) {
        auto message = boost::beast::buffers_to_string(streamBuf.data());
        readBuffer.push_back({connection, std::move(message)});
        streamBuf.consume(streamBuf.size());
        this->readMessage();
      } else if (!disconnected) {
        serverImpl.server.disconnect(connection);
      }
    });
}


////////////////////////////////////////////////////////////////////////////////
// Basic HTTP Request Handling
////////////////////////////////////////////////////////////////////////////////


class HTTPSession : public std::enable_shared_from_this<HTTPSession> {
public:
  HTTPSession(ServerImpl& serverImpl)
    : serverImpl{serverImpl},
      socket{serverImpl.ioContext},
      streamBuf{}
      { }

  void start();
  void handleRequest();

  boost::asio::ip::tcp::socket & getSocket() { return socket; }

private:
  ServerImpl &serverImpl;
  boost::asio::ip::tcp::socket socket;
  boost::beast::flat_buffer streamBuf;
  boost::beast::http::request<boost::beast::http::string_body> request;
};


void
HTTPSession::start() {
  boost::beast::http::async_read(socket, streamBuf, request,
    [this, session = this->shared_from_this()]
    (std::error_code ec, std::size_t /*bytes*/) {
      if (ec) {
        serverImpl.reportError("Error reading from HTTP stream.");

      } else if (boost::beast::websocket::is_upgrade(request)) {
        auto channel = std::make_shared<Channel>(std::move(socket), serverImpl);
        channel->start(request);

      } else {
        session->handleRequest();
      }
    });
}


void
HTTPSession::handleRequest() {
  auto send = [this, session = this->shared_from_this()] (auto&& response) {
    using Response = typename std::decay<decltype(response)>::type;
    auto sharedResponse =
      std::make_shared<Response>(std::forward<decltype(response)>(response));

    boost::beast::http::async_write(socket, *sharedResponse,
      [this, session, sharedResponse] (std::error_code ec, std::size_t /*bytes*/) {
        if (ec) {
          session->serverImpl.reportError("Error writing to HTTP stream");
          socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send);
        } else if (sharedResponse->need_eof()) {
          // This signifies a deliberate close
          boost::system::error_code ec;
          socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
          if (ec) {
            session->serverImpl.reportError("Error closing HTTP stream");
          }
        } else {
          session->start();
        }
      });
  };

  auto const badRequest =
    [&request = this->request] (boost::beast::string_view why) {
    boost::beast::http::response<boost::beast::http::string_body> result {
      boost::beast::http::status::bad_request,
      request.version()
    };
    result.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    result.set(boost::beast::http::field::content_type, "text/html");
    result.keep_alive(request.keep_alive());
    result.body() = why.to_string();
    result.prepare_payload();
    return result;
  };

  if (auto method = request.method();
      method != boost::beast::http::verb::get
      && method != boost::beast::http::verb::head) {
    send(badRequest("Unknown HTTP-method"));
  }

  // We only support index.html and /.
  auto shouldServeIndex = [] (auto target) {
    std::string const index = "/index.html"s;
    constexpr auto npos = boost::beast::string_view::npos;
    // NOTE: in C++20, we can use `ends_with` here instead.
    return target == "/"
      || (index.size() <= target.size()
        && target.compare(target.size() - index.size(), npos, index) == 0);
  };
  if (!shouldServeIndex(request.target())) {
    send(badRequest("Illegal request-target"));
  }
       
  boost::beast::http::string_body::value_type body = serverImpl.httpMessage;

  auto addResponseMetaData =
    [bodySize = body.size(), &request = this->request] (auto& response) {
    response.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    response.set(boost::beast::http::field::content_type, "text/html");
    response.content_length(bodySize);
    response.keep_alive(request.keep_alive());
  };

  if (request.method() == boost::beast::http::verb::head) {
    // Respond to HEAD
    boost::beast::http::response<boost::beast::http::empty_body> result {
      boost::beast::http::status::ok,
      request.version()
    };
    addResponseMetaData(result);
    send(std::move(result));

  } else {
    // Respond to GET
    boost::beast::http::response<boost::beast::http::string_body> result {
      std::piecewise_construct,
      std::make_tuple(std::move(body)),
      std::make_tuple(boost::beast::http::status::ok, request.version())
    };
    addResponseMetaData(result);
    send(std::move(result));
  }
}


/////////////////////////////////////////////////////////////////////////////
// Hidden Server implementation
/////////////////////////////////////////////////////////////////////////////


void
ServerImpl::listenForConnections() {
  auto session =
    std::make_shared<HTTPSession>(*this);

  acceptor.async_accept(session->getSocket(),
    [this, session] (auto errorCode) {
      if (!errorCode) {
        session->start();
      } else {
        reportError("Fatal error while accepting");
      }
      this->listenForConnections();
    });
}


void
ServerImpl::registerChannel(Channel& channel) {
  auto connection = channel.getConnection();
  channels[connection] = channel.shared_from_this();
  server.connectionHandler->handleConnect(connection);
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
  for (auto& message : messages) {
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
    connectionHandler->handleDisconnect(connection);
    found->second->disconnect();
    impl->channels.erase(found);
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

