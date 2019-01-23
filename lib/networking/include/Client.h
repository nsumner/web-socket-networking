/////////////////////////////////////////////////////////////////////////////
//                         Single Threaded Networking
//
// This file is distributed under the MIT License. See the LICENSE file
// for details.
/////////////////////////////////////////////////////////////////////////////


#ifndef NETWORKING_CLIENT_H
#define NETWORKING_CLIENT_H

#include <memory>
#include <string>


namespace networking {


/**
 *  @class Client
 *
 *  @brief A single threaded network client for transferring text.
 *
 *  The Client class transfers text to and from a Server running on a given
 *  IP address and port. The behavior is single threaded, so all transfer
 *  operations are grouped and performed on the next call to Client::update().
 *  Text can be sent to the Server using Client::send() and received from the
 *  Server using Client::receive().
 */
class Client {
public:
  /**
   *  Construct a Client and acquire a connection to a remote Server at the
   *  given address and port.
   */
  Client(std::string_view address, std::string_view port);

  /** Out of line default constructor for compilation firewall. */
  ~Client();

  /**
   *  Perform all pending sends and receives. This function can throw an
   *  exception if any of the I/O operations encounters an error.
   */
  void update();

  /**
   *  Send a message to the server.
   */
  void send(std::string message);

  /**
   *  Receive messages from the Server. This returns all messages collected by
   *  previous calls to Client::update() and not yet received. If multiple
   *  messages were received from the Server, they are first concatenated
   *  into a single std::string.
   */
  [[nodiscard]] std::string receive();

  /**
   *  Returns true iff the client disconnected from the server after initially
   *  connecting.
   */
  [[nodiscard]] bool isDisconnected() const noexcept;

private:
  class ClientImpl;

  std::unique_ptr<ClientImpl> impl;
};


}


#endif

