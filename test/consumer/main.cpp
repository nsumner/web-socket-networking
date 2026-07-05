#include "Client.h"

// Build-time smoke test for the installation.
int main(int argc, char** argv) {
  if (argc > 42) {
    networking::Client client{argv[1], argv[2]};
    client.send("hello");
    client.update();
    return client.isDisconnected() ? 1 : 0;
  }
  return 0;
}
