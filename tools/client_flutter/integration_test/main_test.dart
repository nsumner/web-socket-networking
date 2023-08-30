import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:WebSocketDemo/main.dart';


// This performs thresholded retries while waiting for an element to appear.
// Note the dangers in using this approach.
Future<bool>
isPresent(WidgetTester tester,
          Finder finder,
          {Duration timeout = const Duration(seconds: 1)}) async {
  final end = tester.binding.clock.now().add(timeout);

  do {
    if (tester.binding.clock.now().isAfter(end)) {
      return false;
    }

    await tester.pumpAndSettle();
    await Future.delayed(const Duration(milliseconds: 100));
  } while (finder.evaluate().isEmpty);

  return true;
}


void
main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  group('echo test', () {
    testWidgets('Connects to echo server and receives message', (tester) async {
      await tester.pumpWidget(ChatWithConnectPageApp());
      await tester.pumpAndSettle();

      final serverFinder   = find.byKey(ValueKey('ServerField'));
      final connectFinder  = find.byKey(ValueKey('ConnectButton'));
      final messageFinder  = find.byKey(ValueKey('MessageField'));
      final sendFinder     = find.byKey(ValueKey('SendButton'));
      final receivedFinder = find.byKey(ValueKey('Message(1)'));
      final message = 'Hi, there!';

      // Enter an echo server into the server field.
      await tester.enterText(serverFinder, 'ws://echo.websocket.events');
    
      // Tap the connect button to reach the 
      await tester.tap(connectFinder);

      // Wait for the next page to load
      await tester.pumpAndSettle();

      // Enter a message into the message field
      await tester.enterText(messageFinder, message);
      await tester.tap(sendFinder);

      // Wait for a response to be triggered
      expect(await isPresent(tester, receivedFinder), true);
      expect(await tester.firstWidget<Text>(receivedFinder).data, message);
    });
  });
}

