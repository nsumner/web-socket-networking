#include "TestHelpers.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <vector>

using networking::Client;
using networking::Connection;
using networking::Message;
using networking::Server;
using testhelpers::pumpUntil;

namespace {

class EndToEnd : public ::testing::Test {
protected:
  EndToEnd() {
    // Bind to port 0 so the OS assigns a free port, then read it back. This
    // avoids colliding with ephemeral ports handed out elsewhere on the host.
    server.emplace(0, "<html>test-page</html>",
                   [this](Connection c) { connects.push_back(c); },
                   [this](Connection c) { disconnects.push_back(c); });
    port = server->getPort();
    portString = std::to_string(port);
  }

  // Pump until every client in the list has produced a connect callback.
  bool connectClients(std::vector<Client*> clients) {
    const size_t target = connects.size() + clients.size();
    return pumpUntil([&] { return connects.size() >= target; },
                     &*server, clients);
  }

  unsigned short port = 0;      // assigned by the OS; filled in by the ctor
  std::string portString;       // set from getPort() in the ctor
  std::optional<Server> server;
  std::vector<Connection> connects;
  std::vector<Connection> disconnects;
};

TEST_F(EndToEnd, ClientConnectFiresCallback) {
  Client client{"localhost", portString};
  EXPECT_TRUE(connectClients({&client}));
  ASSERT_EQ(connects.size(), 1u);
  EXPECT_TRUE(disconnects.empty());
}

TEST_F(EndToEnd, EchoRoundTrip) {
  Client client{"localhost", portString};
  ASSERT_TRUE(connectClients({&client}));

  client.send("hello server");
  std::deque<Message> received;
  ASSERT_TRUE(pumpUntil(
      [&] {
        auto batch = server->receive();
        received.insert(received.end(), batch.begin(), batch.end());
        return !received.empty();
      },
      &*server, {&client}));
  ASSERT_EQ(received.size(), 1u);
  EXPECT_EQ(received.front().text, "hello server");
  EXPECT_EQ(received.front().connection, connects.front());

  server->send(std::deque<Message>{Message{connects.front(), "hello client"}});
  std::string got;
  ASSERT_TRUE(pumpUntil(
      [&] {
        got += client.receive();
        return !got.empty();
      },
      &*server, {&client}));
  EXPECT_EQ(got, "hello client");
}

TEST_F(EndToEnd, MultiClientFanoutIsIsolated) {
  Client one{"localhost", portString};
  ASSERT_TRUE(connectClients({&one}));
  Client two{"localhost", portString};
  ASSERT_TRUE(connectClients({&two}));
  ASSERT_EQ(connects.size(), 2u);

  server->send(std::deque<Message>{Message{connects[0], "for one;"},
                                   Message{connects[1], "for two;"}});

  std::string gotOne;
  std::string gotTwo;
  ASSERT_TRUE(pumpUntil(
      [&] {
        gotOne += one.receive();
        gotTwo += two.receive();
        return !gotOne.empty() && !gotTwo.empty();
      },
      &*server, {&one, &two}));
  EXPECT_EQ(gotOne, "for one;");
  EXPECT_EQ(gotTwo, "for two;");
}

TEST_F(EndToEnd, MessageOrderIsPreservedBothDirections) {
  Client client{"localhost", portString};
  ASSERT_TRUE(connectClients({&client}));

  std::deque<Message> outbound;
  for (int i = 0; i < 20; ++i) {
    client.send("c2s:" + std::to_string(i));
    outbound.push_back(
        Message{connects.front(), "s2c:" + std::to_string(i) + ";"});
  }
  server->send(outbound);

  std::deque<Message> atServer;
  std::string atClient;
  ASSERT_TRUE(pumpUntil(
      [&] {
        auto batch = server->receive();
        atServer.insert(atServer.end(), batch.begin(), batch.end());
        atClient += client.receive();
        return atServer.size() >= 20 &&
               std::count(atClient.begin(), atClient.end(), ';') >= 20;
      },
      &*server, {&client}));

  ASSERT_EQ(atServer.size(), 20u);
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(atServer[static_cast<size_t>(i)].text,
              "c2s:" + std::to_string(i));
  }
  std::string expected;
  for (int i = 0; i < 20; ++i) {
    expected += "s2c:" + std::to_string(i) + ";";
  }
  EXPECT_EQ(atClient, expected);
}

TEST_F(EndToEnd, ClientDestructionLeadsToDisconnectCallback) {
  {
    Client client{"localhost", portString};
    ASSERT_TRUE(connectClients({&client}));
  }
  EXPECT_TRUE(pumpUntil([&] { return disconnects.size() == 1; },
                        &*server, {}));
}

TEST_F(EndToEnd, HttpGetServesTheConfiguredPage) {
  const std::string response = testhelpers::httpExchange(
      *server, port, "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  EXPECT_NE(response.find("<html>test-page</html>"), std::string::npos);
}

TEST_F(EndToEnd, ServerDisconnectFiresCallbackAndClientObserves) {
  Client client{"localhost", portString};
  ASSERT_TRUE(connectClients({&client}));
  EXPECT_FALSE(client.isDisconnected());

  server->disconnect(connects.front());
  ASSERT_EQ(disconnects.size(), 1u);
  EXPECT_EQ(disconnects.front(), connects.front());

  EXPECT_TRUE(pumpUntil([&] { return client.isDisconnected(); },
                        &*server, {&client}));
}

TEST_F(EndToEnd, HttpHeadReturnsHeadersOnly) {
  const std::string response = testhelpers::httpExchange(
      *server, port, "HEAD /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
  EXPECT_NE(response.find("200"), std::string::npos);
  EXPECT_EQ(response.find("test-page"), std::string::npos);
}

TEST_F(EndToEnd, ConnectionIdsAreNotReusedAcrossClients) {
  {
    Client client{"localhost", portString};
    ASSERT_TRUE(connectClients({&client}));
  }
  ASSERT_TRUE(pumpUntil([&] { return disconnects.size() == 1; },
                        &*server, {}));
  {
    Client client{"localhost", portString};
    ASSERT_TRUE(connectClients({&client}));
  }
  ASSERT_EQ(connects.size(), 2u);
  EXPECT_FALSE(connects[0] == connects[1]);
}

TEST_F(EndToEnd, NoDisconnectCallbacksDuringServerDestruction) {
  Client client{"localhost", portString};
  ASSERT_TRUE(connectClients({&client}));
  server.reset();  // Client is still connected while the server dies.
  EXPECT_TRUE(disconnects.empty());
}

}  // namespace
