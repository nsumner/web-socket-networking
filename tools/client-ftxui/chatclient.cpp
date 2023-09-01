/////////////////////////////////////////////////////////////////////////////
//                         Single Threaded Networking
//
// This file is distributed under the MIT License. See the LICENSE file
// for details.
/////////////////////////////////////////////////////////////////////////////


#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Client.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/loop.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

int
main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: \n  " << argv[0] << " <ip address> <port>\n"
              << "  e.g. " << argv[0] << " localhost 4002\n";
    return 1;
  }

  networking::Client client{argv[1], argv[2]};

  bool done = false;

  auto onTextEntry = [&done, &client] (std::string text) {
    if ("exit" == text || "quit" == text) {
      done = true;
    } else {
      client.send(std::move(text));
    }
  };

  using namespace ftxui;

  std::string entry;
  std::vector<Element> history;
  Component entryField = Input(&entry, "Enter messages here.");

  // Define the core appearance with a renderer for the components.
  // A `Renderer` takes input consuming components like `Input` and
  // produces a DOM to visually represent their current state.
  auto renderer = Renderer(entryField, [&history,&entryField] {
    return vbox({
      window(text("Chat"),
        yframe(
          vbox(history) | focusPositionRelative(0, 1)
        )
      ) | yflex,

      window(text("Next Message"),
        entryField->Render()
      ) | size(HEIGHT, EQUAL, 3)
    }) | color(Color::GreenLight);
  });

  auto screen = ScreenInteractive::Fullscreen();

  // Bind a handler for "return" presses that consumes the text entered
  // so far.
  auto handler = CatchEvent(renderer, [&entry,&onTextEntry](const Event& event) {
    if (event == Event::Return) {
      onTextEntry(std::move(entry));
      entry.clear();
      return true;
    }
    return false;
  });

  const int UPDATE_INTERVAL_IN_MS = 50;
  Loop loop(&screen, handler);
  while (!done && !client.isDisconnected() && !loop.HasQuitted()) {
    try {
      client.update();
    } catch (std::exception& e) {
      history.push_back(text("Exception from Client update:"));
      history.push_back(text(e.what()));
      done = true;
    }

    auto response = client.receive();
    if (!response.empty()) {
      history.push_back(paragraphAlignLeft(response));
      screen.RequestAnimationFrame();
    }

    loop.RunOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_INTERVAL_IN_MS));
  }

  return 0;
}

