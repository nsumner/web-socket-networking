
# Single Threaded Web Socket Networking

This repository contains an example library for single threaded client/server
programs based on web sockets. The server is built using boost beast and C++.
Clients support either boost beast or WebAssembly using Emscripten.
Multiple clients and the server can transfer simple string messages back and
forth. Because the API is single threaded, it integrates easily into a main
update loop, e.g. for a game. Because it is web socket based, it supports both
native and browser based clients at the same time. Examples of both are
provided.

*Note:* Both the creation of the communication channel as well as all
communication between the client and the server is insecure. It is trivially
subject to interception and alteration, and it should not be used to transmit
sensitive information of any sort.

In addition, a simple chat server is provided as well as multiple clients using
[NCurses](https://invisible-island.net/ncurses/),
[FTXUI](https://github.com/ArthurSonzogni/FTXUI) with C++ or WebAssembly, and
[Flutter](https://flutter.dev/).


## Dependencies

This project requires:

1. C++17 or newer
2. Boost >= 1.72
3. CMake >= 3.24
4. NCurses (only tested with 6.1) [Optional]
5. FTXUI >= 5.0 [Optional]
6. Flutter >= 3.0 [Optional]


## Building with CMake

1. Clone the repository.

        git clone https://github.com/nsumner/web-socket-networking.git

2. Create a new directory for building.

        mkdir networkbuild

3. Change into the new directory.

        cd networkbuild

4. Run CMake with the path to the source.

        cmake ../web-socket-networking/

5. Run make inside the build directory:

        make

This produces chat server and client tools called `bin/chatserver` and
`bin/client-ncurses` respectively. The library for single threaded clients and
servers is built in `lib/`.

Note, building with a tool like ninja can be done by adding `-G Ninja` to
the cmake invocation and running `ninja` instead of `make`.


## Running the Example Chat Client and Chat Server

First run the chat server on an unused port of the server machine. The server
also takes an HTML file that it will server to standard http requests for
`index.html`.

    bin/chatserver 8000 ../web-socket-networking/webchat.html

In separate terminals, run multiple instances of the chat client using:

    bin/client-ncurses localhost 8000

This will connect to the given port (8000 in this case) of the local machine.
Connecting to a remote machine can be done by explicitly using the remote
machine's IP address instead of `localhost`. Inside the chat client, you can
enter commands or chat with other clients by typing text and hitting the
ENTER key. You can disconnect from the server by typing `quit`. You can shut
down the server and disconnect all clients by typing `shutdown`. Typing
anything else will send a chat message to other clients.

A browser based interface can be accessed by opening the URL
`http://localhost:8000/index.html`. The server will respond with the
specified web page above. By clicking `Connect`, the page gains access to
chat on the server via web sockets in browsers that support web sockets.


## Running the Example Flutter Chat Client

If you have Flutter installed, then a very simple chat client in Flutter
will also be built inside the build directory. From the build directory,
this can be run using:

    cd flutterclient
    flutter run

You can then enter the chat server location into the client to connect to
the server and start chatting. For instance, to connect to the server
configured in the previous example, we could enter:

    ws://localhost:8000

to connect to the local server on port 8000.


## Building and Running the Example WebAssembly Chat Client

If you have emscripten installed, then you can configure and build a C++
client that runs in the browser with WebAssembly. The GUI is driven by
FTXUI, and the browser client should behave the same as the native client.

Building is similar to a normal CMake process:

1. Create a new directory specifically for building with emscripten.

        mkdir embuild

2. Change into the new directory.

        cd networkbuild

3. Run the emscripten CMake wrapper with the path to the source.

        emcmake cmake ../web-socket-networking/

4. Run the emscripten make wrapper inside the build directory:

        emmake make

Running the client requires starting a simple web server.

        cd bin/
        ./run_webassembly.py

