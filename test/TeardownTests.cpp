#include "TestHelpers.h"

#include "gtest/gtest.h"

#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using networking::Client;
using networking::Connection;
using networking::Message;
using networking::Server;
using testhelpers::pumpUntil;

namespace {

// A server plus callback records, destructible on demand via server.reset().
// Binds to port 0 so the OS picks a free port, exposed via `port` for clients.
struct ServerHarness {
  ServerHarness() {
    server.emplace(0, "<html/>",
                   [this](Connection c) { connects.push_back(c); },
                   [this](Connection c) { disconnects.push_back(c); });
    port = server->getPort();
    portString = std::to_string(port);
  }

  unsigned short port = 0;
  std::string portString;
  std::optional<Server> server;
  std::vector<Connection> connects;
  std::vector<Connection> disconnects;
};

TEST(Teardown, ServerDestroyedWithQueuedOutboundFrames) {
  ServerHarness harness;
  Client client{"localhost", harness.portString};
  ASSERT_TRUE(pumpUntil([&] { return harness.connects.size() == 1; },
                        &*harness.server, {&client}));

  std::deque<Message> frames;
  for (int i = 0; i < 8; ++i) {
    frames.push_back(
        Message{harness.connects.front(), "queued frame " + std::to_string(i)});
  }
  harness.server->send(frames);
  // Destroy with the frames still queued and never pumped out. LSan must
  // stay clean: the queued strings must be freed deterministically.
  harness.server.reset();
}

TEST(Teardown, ServerDestroyedMidConversation) {
  ServerHarness harness;
  Client client{"localhost", harness.portString};
  ASSERT_TRUE(pumpUntil([&] { return harness.connects.size() == 1; },
                        &*harness.server, {&client}));

  for (int round = 0; round < 5; ++round) {
    client.send("ping " + std::to_string(round));
    harness.server->send(std::deque<Message>{
        Message{harness.connects.front(), "pong " + std::to_string(round)}});
    harness.server->update();
    client.update();
  }
  // Frames may be anywhere: queued, mid-write, or in kernel buffers.
  harness.server.reset();
}

TEST(Teardown, ClientDestroyedWithQueuedOutboundMessages) {
  ServerHarness harness;
  {
    Client client{"localhost", harness.portString};
    ASSERT_TRUE(pumpUntil([&] { return harness.connects.size() == 1; },
                          &*harness.server, {&client}));
    for (int i = 0; i < 8; ++i) {
      client.send("queued message " + std::to_string(i));
    }
    // Destroy the client without pumping the queued messages out.
  }
  harness.server.reset();
}

TEST(Teardown, ServerAndClientDestroyedImmediatelyAfterConnect) {
  ServerHarness harness;
  Client client{"localhost", harness.portString};
  ASSERT_TRUE(pumpUntil([&] { return harness.connects.size() == 1; },
                        &*harness.server, {&client}));
  harness.server.reset();
}

TEST(Teardown, ClientDestroyedBeforeConnectionCompletes) {
  ServerHarness harness;
  {
    // Destroyed immediately, so the client is never pumped: the session
    // coroutine first runs inside the destructor's drain loop, already
    // under terminal cancellation. It executes the synchronous resolve,
    // then aborts at the first async op (async_connect). The old
    // implementation could hang here (its destructor ran the handshake to
    // completion against a server that never pumps); the rewrite cancels
    // instead. Added post-rewrite for that reason — red verification would
    // hang, not fail.
    Client client{"localhost", harness.portString};
  }
  harness.server.reset();
}

// Destroy a client after a bounded number of pump iterations, so the session
// coroutine is genuinely suspended at whatever phase N pumps reached —
// async_connect, async_handshake, or the established reader/writer — when
// cancellation hits it. Complements ClientDestroyedBeforeConnectionCompletes
// above, which never suspends (the session starts pre-cancelled). The exact
// phase per N depends on kernel timing; every interleaving must be leak-free,
// so LSan at process exit remains the oracle.
class TeardownPumped : public ::testing::TestWithParam<int> { };

TEST_P(TeardownPumped, ClientDestroyedAfterBoundedPumping) {
  ServerHarness harness;
  {
    Client client{"localhost", harness.portString};
    for (int i = 0; i < GetParam(); ++i) {
      harness.server->update();
      client.update();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  harness.server.reset();
}

INSTANTIATE_TEST_SUITE_P(PumpCounts, TeardownPumped, ::testing::Range(0, 6));

}  // namespace
