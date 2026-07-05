#include "TestHelpers.h"

#include "gtest/gtest.h"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using networking::Client;
using networking::Connection;
using networking::Message;
using networking::Server;

// One fuzzer-managed client plus the model of what it must observe.
struct ClientSlot {
  std::unique_ptr<Client> client;          // null once destroyed
  std::optional<Connection> connection;    // set once the server reports it
  bool serverDisconnected = false;         // Server::disconnect was called
  int nextClientSendSeq = 0;               // next c->s sequence to send
  int nextServerExpectSeq = 0;             // next c->s sequence server must see
  int nextServerSendSeq = 0;               // next s->c sequence to send
  std::string pendingReceived;             // partial client-side receive data
  std::deque<std::string> expectToClient;  // FIFO of s->c payloads not yet seen
};

class ScheduleFuzzer {
public:
  explicit ScheduleFuzzer(unsigned seed) : rng{seed} { }

  void run(int steps) {
    server.emplace(0, "<html/>",
                   [this](Connection c) { onConnect(c); },
                   [this](Connection c) { onDisconnect(c); });
    port = server->getPort();
    addClient();
    for (int step = 0; step < steps; ++step) {
      const std::string action = act();
      SCOPED_TRACE("step=" + std::to_string(step) + " action=" + action);
      checkDeliveries();
      if (::testing::Test::HasFatalFailure() || ::testing::Test::HasFailure()) {
        return;
      }
    }
    // Leave traffic queued on every live connection, then tear the server
    // down: the reported defect's exact scenario, at fuzzed queue depths.
    for (auto& slot : slots) {
      if (isConnected(slot)) {
        serverSend(slot);
        clientSend(slot);
      }
    }
    server.reset();
    slots.clear();  // destroy surviving clients after the server is gone
  }

private:
  static bool isConnected(const ClientSlot& slot) {
    return slot.client != nullptr && slot.connection.has_value()
           && !slot.serverDisconnected;
  }

  ClientSlot* pick(bool needsConnection) {
    std::vector<ClientSlot*> eligible;
    for (auto& slot : slots) {
      if (needsConnection ? isConnected(slot) : slot.client != nullptr) {
        eligible.push_back(&slot);
      }
    }
    if (eligible.empty()) {
      return nullptr;
    }
    return eligible[std::uniform_int_distribution<size_t>{
        0, eligible.size() - 1}(rng)];
  }

  std::string act() {
    const int roll = std::uniform_int_distribution<int>{0, 99}(rng);
    if (roll < 25) {
      return pumpBoth(1 + roll % 3);
    }
    if (roll < 45) {
      if (auto* slot = pick(true)) {
        clientSend(*slot);
        return "clientSend";
      }
    } else if (roll < 65) {
      if (auto* slot = pick(true)) {
        serverSend(*slot);
        return "serverSend";
      }
    } else if (roll < 75) {
      server->update();
      return "serverUpdate";
    } else if (roll < 85) {
      if (auto* slot = pick(false)) {
        slot->client->update();
        return "clientUpdate";
      }
    } else if (roll < 90) {
      if (slots.size() < 6) {
        return addClient();
      }
    } else if (roll < 95) {
      if (auto* slot = pick(true)) {
        server->disconnect(*slot->connection);
        slot->serverDisconnected = true;
        return "serverDisconnect";
      }
    } else {
      if (auto* slot = pick(false)) {
        slot->client.reset();
        return "destroyClient";
      }
    }
    return pumpBoth(1);
  }

  std::string addClient() {
    auto& slot = slots.emplace_back();
    slot.client = std::make_unique<Client>("localhost", std::to_string(port));
    const size_t before = connectOrder.size();
    const bool connected = testhelpers::pumpUntil(
        [&] { return connectOrder.size() > before; },
        &*server, {slot.client.get()});
    EXPECT_TRUE(connected) << "new client failed to connect";
    if (connected) {
      slot.connection = connectOrder.back();
    }
    return "newClient";
  }

  void clientSend(ClientSlot& slot) {
    slot.client->send("c" + std::to_string(indexOf(slot)) + ":"
                      + std::to_string(slot.nextClientSendSeq++) + ";");
  }

  void serverSend(ClientSlot& slot) {
    const std::string payload =
        "s" + std::to_string(indexOf(slot)) + ":"
        + std::to_string(slot.nextServerSendSeq++) + ";";
    server->send(std::deque<Message>{Message{*slot.connection, payload}});
    slot.expectToClient.push_back(payload);
  }

  std::string pumpBoth(int rounds) {
    for (int round = 0; round < rounds; ++round) {
      server->update();
      for (auto& slot : slots) {
        if (slot.client) {
          slot.client->update();
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return "pumpBoth(" + std::to_string(rounds) + ")";
  }

  void checkDeliveries() {
    // Server side: every message must be the owning client's next c->s
    // sequence number — FIFO per connection, no cross-wiring. Messages from
    // disconnected or destroyed clients may legitimately trickle in; the
    // same FIFO rule still applies to them.
    for (auto& message : server->receive()) {
      auto* slot = slotFor(message.connection);
      ASSERT_NE(slot, nullptr) << "message from unknown connection";
      const std::string expected =
          "c" + std::to_string(indexOf(*slot)) + ":"
          + std::to_string(slot->nextServerExpectSeq) + ";";
      ASSERT_EQ(message.text, expected);
      ++slot->nextServerExpectSeq;
    }

    // Client side: consume complete ';'-terminated tokens and match them
    // FIFO against what the server queued for that client. In-flight loss
    // on teardown is legal (discard semantics); reordering is not.
    for (auto& slot : slots) {
      if (!slot.client) {
        continue;
      }
      slot.pendingReceived += slot.client->receive();
      size_t position = 0;
      size_t terminator = 0;
      while ((terminator = slot.pendingReceived.find(';', position))
             != std::string::npos) {
        const std::string token =
            slot.pendingReceived.substr(position, terminator - position + 1);
        ASSERT_FALSE(slot.expectToClient.empty())
            << "client received unexpected " << token;
        ASSERT_EQ(token, slot.expectToClient.front());
        slot.expectToClient.pop_front();
        position = terminator + 1;
      }
      slot.pendingReceived.erase(0, position);
    }
  }

  ClientSlot* slotFor(Connection connection) {
    for (auto& slot : slots) {
      if (slot.connection && slot.connection->id == connection.id) {
        return &slot;
      }
    }
    return nullptr;
  }

  size_t indexOf(const ClientSlot& slot) const {
    return static_cast<size_t>(&slot - slots.data());
  }

  void onConnect(Connection c) { connectOrder.push_back(c); }

  void onDisconnect(Connection c) {
    EXPECT_NE(slotFor(c), nullptr) << "disconnect for unknown connection";
    EXPECT_EQ(disconnected.count(c.id), 0u) << "duplicate disconnect callback";
    disconnected.insert(c.id);
  }

  std::mt19937 rng;
  unsigned short port = 0;  // assigned by the OS in run(), read via getPort()
  std::optional<Server> server;
  std::vector<ClientSlot> slots;
  std::vector<Connection> connectOrder;
  std::set<uintptr_t> disconnected;
};

class ScheduleFuzz : public ::testing::TestWithParam<unsigned> { };

TEST_P(ScheduleFuzz, RandomScheduleMaintainsInvariants) {
  ScheduleFuzzer fuzzer{GetParam()};
  fuzzer.run(250);
}

INSTANTIATE_TEST_SUITE_P(FixedSeeds, ScheduleFuzz, ::testing::Range(1u, 11u));

TEST(ScheduleFuzzExtra, EnvironmentSeed) {
  const char* seedText = std::getenv("NETWORKING_FUZZ_SEED");
  if (seedText == nullptr) {
    GTEST_SKIP() << "set NETWORKING_FUZZ_SEED to run a longer ad-hoc seed";
  }
  ScheduleFuzzer fuzzer{static_cast<unsigned>(std::stoul(seedText))};
  fuzzer.run(1000);
}

}  // namespace
