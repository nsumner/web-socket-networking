/////////////////////////////////////////////////////////////////////////////
//                         Single Threaded Networking
//
// This file is distributed under the MIT License. See the LICENSE file
// for details.
/////////////////////////////////////////////////////////////////////////////


#include "Client.h"

#include <boost/asio.hpp>
#include <boost/beast.hpp>


#include <deque>
#include <sstream>

using networking::Client;


/////////////////////////////////////////////////////////////////////////////
// Private Client API
/////////////////////////////////////////////////////////////////////////////

namespace networking {


class Client::ClientImpl {
public:
  ClientImpl(std::string_view address, std::string_view port)
    : isClosed{false},
      hostAddress{address.data(), address.size()},
      ioService{},
      websocket{ioService} {
    boost::asio::ip::tcp::resolver resolver{ioService};
    connect(resolver.resolve(address, port));
  }

  void disconnect();

  void connect(boost::asio::ip::tcp::resolver::iterator endpoint);

  void handshake();

  void readMessage();

  void reportError(std::string_view message);

  bool isClosed;
  std::string hostAddress;
  boost::asio::io_service ioService;
  boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket;
  boost::beast::multi_buffer readBuffer;
  std::ostringstream incomingMessage;
  std::deque<std::string> writeBuffer;
};


}


void
Client::ClientImpl::disconnect() {
  isClosed = true;
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
Client::ClientImpl::reportError(std::string_view /*message*/) {
  // Swallow errors....
}


/////////////////////////////////////////////////////////////////////////////
// Core Client
/////////////////////////////////////////////////////////////////////////////


Client::Client(std::string_view address, std::string_view port)
  : impl{std::make_unique<ClientImpl>(address, port)}
    { }


Client::~Client() = default;


void
Client::update() {
  impl->ioService.poll();  
}


std::string
Client::receive() {
  auto result = impl->incomingMessage.str();
  impl->incomingMessage.str(std::string{});
  impl->incomingMessage.clear();
  return result;
}


void
Client::send(std::string message) {
  if (message.empty()) {
    return;
  }

  impl->writeBuffer.emplace_back(std::move(message));
  impl->websocket.async_write(boost::asio::buffer(impl->writeBuffer.back()),
    [this] (auto errorCode, std::size_t /*size*/) {
      if (!errorCode) {
        impl->writeBuffer.pop_front();
      } else {
        impl->reportError("Unable to write.");
        impl->disconnect();
      }
    });
}


bool
Client::isDisconnected() const noexcept {
  return impl->isClosed;
}

