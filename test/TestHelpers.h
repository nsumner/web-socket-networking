#pragma once

#include "Client.h"
#include "Server.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace testhelpers {

// Drive the server's and clients' update() pumps until done() holds or the
// iteration budget is exhausted. Returns the final value of done().
template <typename Predicate>
bool pumpUntil(Predicate&& done,
               networking::Server* server,
               std::vector<networking::Client*> clients,
               int maxIterations = 2000) {
  for (int i = 0; i < maxIterations; ++i) {
    if (done()) {
      return true;
    }
    if (server != nullptr) {
      server->update();
    }
    for (auto* client : clients) {
      if (client != nullptr) {
        client->update();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return done();
}

// Send one raw HTTP request over a plain TCP socket and pump the server until
// it closes the connection. Returns everything the server sent back. Uses
// nonblocking reads so the single-threaded server can be pumped in between.
inline std::string httpExchange(networking::Server& server,
                                unsigned short port,
                                const std::string& request) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return {};
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) != 0) {
    ::close(fd);
    return {};
  }
  ::send(fd, request.data(), request.size(), 0);

  std::string response;
  for (int i = 0; i < 2000; ++i) {
    server.update();
    char buffer[4096];
    const ssize_t count = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (count > 0) {
      response.append(buffer, static_cast<size_t>(count));
    } else if (count == 0) {
      break;  // Peer closed: the response is complete.
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ::close(fd);
  return response;
}

}  // namespace testhelpers
