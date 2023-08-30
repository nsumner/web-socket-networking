import 'package:web_socket_channel/io.dart';
import 'package:flutter/material.dart';
import 'package:web_socket_channel/web_socket_channel.dart';


void main() => runApp(ChatWithConnectPageApp());


class ChatWithConnectPageApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    final title = 'WebSocket Demo';
    return MaterialApp(
      title: title,
      home: ConnectPage(
        title: title,
      ),
    );
  }
}


class ConnectPage extends StatefulWidget {
  final String title;

  ConnectPage({Key? key, required this.title})
      : super(key: key);

  @override
  _ConnectPageState createState() => _ConnectPageState();
}


class _ConnectPageState extends State<ConnectPage> {
  TextEditingController _nameController = TextEditingController();
  TextEditingController _serverController = TextEditingController();

  @override
  Widget build(BuildContext context) {

    final nameField = TextField(
      obscureText: false,
      controller: _nameController,
      decoration: InputDecoration(hintText: "User Name"),
    );
    
    final serverField = TextField(
      obscureText: false,
      key: ValueKey("ServerField"),
      controller: _serverController,
      decoration: InputDecoration(hintText: "Chat Server Location"),
      autofocus: true,
      onSubmitted: (String server) => _connectToServer(_nameController.text, server)
    );

    final connectButton = Material(
      child: MaterialButton(
        key: ValueKey("ConnectButton"),
        color: Color(0x000AACC),
        onPressed: () => _connectToServer(_nameController.text, _serverController.text),
        child: Text("Connect",
          textAlign: TextAlign.center,
          style: TextStyle(color: Colors.white),
        ),
      ),
    );


    return Scaffold(
      appBar: AppBar(
        title: Text(widget.title),
      ),

      body: Center(
        child: Container(
          child: SizedBox(
            width: 300.0,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.center,
              mainAxisAlignment: MainAxisAlignment.center,
              children: <Widget>[
                nameField,
                SizedBox(height: 10.0),
                serverField,
                SizedBox(height: 10.0),
                connectButton,
              ],
            ),
          ),
        ),
      ),
    );
  }

  void _connectToServer(String name, String server) {
    Navigator.push(
      context,
      MaterialPageRoute(builder: (context) => ChatPage(
          title: widget.title,
          channel: IOWebSocketChannel.connect(_serverController.text),
        )
      ),
    );
  }

  @override
  void dispose() {
    _nameController.dispose();
    _serverController.dispose();
    super.dispose();
  }
}


class ChatPage extends StatefulWidget {
  final String title;
  final WebSocketChannel channel;

  ChatPage({Key? key, required this.title, required this.channel})
      : super(key: key);

  @override
  _ChatPageState createState() => _ChatPageState();
}


class _ChatPageState extends State<ChatPage> {
  TextEditingController _controller = TextEditingController();
  ScrollController _scrollController = ScrollController(
    keepScrollOffset: true,
  );
  FocusNode? _focusNode;
  List<String> _contents = [];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.title),
      ),

      body: Padding(
        padding: const EdgeInsets.all(20.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            TextField(
              key: ValueKey("MessageField"),
              controller: _controller,
              focusNode: _focusNode,
              autofocus: true,
              decoration: InputDecoration(labelText: 'Send a message'),
              onSubmitted: _sendMessage,
            ),
            StreamBuilder(
              stream: widget.channel.stream,
              builder: (context, snapshot) {
                if (snapshot.hasData) {
                  _contents.add(snapshot.data);
                }
                                
                return Expanded(
                  child: Padding(
                    padding: const EdgeInsets.symmetric(vertical: 20.0),
                    child: Scrollbar(
                      controller: _scrollController,
                      child:  ListView.builder(
                        shrinkWrap: true,
                        reverse: true,
                        itemBuilder: _buildMessage,
                        itemCount: _contents.length,
                        controller: _scrollController,
                      ),
                    ),
                  ),
                );
              },
            )
          ],
        ),
      ),
      floatingActionButton: FloatingActionButton(
        key: ValueKey("SendButton"),
        onPressed: () => _sendMessage(_controller.text),
        tooltip: 'Send message',
        child: Icon(Icons.send),
      ),
    );
  }
  
  Widget _buildMessage(BuildContext context, int index) {
    final id = _contents.length - index - 1;
    return Card(
      child: Text(
        _contents[id],
        key: ValueKey('Message(${id})')
      ),
    );
  }

  void _sendMessage(String message) {
    if (message.isNotEmpty) {
      widget.channel.sink.add(message);
    }
    _controller.clear();
    _focusNode?.requestFocus();
  }

  @override
  void initState() {
    super.initState();
    _focusNode = FocusNode();
  }
  
  @override
  void dispose() {
    _focusNode?.dispose();
    widget.channel.sink.close();
    _controller.dispose();
    _scrollController.dispose();
    super.dispose();
  }
}


