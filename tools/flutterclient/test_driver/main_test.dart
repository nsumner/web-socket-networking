
import 'package:flutter_driver/flutter_driver.dart';
import 'package:test/test.dart';


// This function just translates exceptions into booleans to check
// whether a widget with the given ID has loaded within a time
// threshold. Note the dangers in using this approach!
Future<bool>
isPresent(SerializableFinder byValueKey,
          FlutterDriver driver,
          {Duration timeout = const Duration(seconds: 1)}) async {
  try {
    await driver.waitFor(byValueKey, timeout: timeout);
    return true;
  } catch(exception) {
    return false;
  }
}


void
main() {
  group('Counter App', () {
    FlutterDriver driver;

    // Connect to the Flutter driver before running any tests.
    setUpAll(() async {
      driver = await FlutterDriver.connect();
    });

    // Close the connection to the driver after the tests have completed.
    tearDownAll(() async {
      if (driver != null) {
        driver.close();
      }
    });

    test('Connects to echo server and receives message', () async {
      final serverFinder   = find.byValueKey('ServerField');
      final connectFinder  = find.byValueKey('ConnectButton');
      final messageFinder  = find.byValueKey('MessageField');
      final sendFinder     = find.byValueKey('SendButton');
      final receivedFinder = find.byValueKey('Message(0)');
      final message = 'Hi, there!';

      // Enter an echo server into the server field.
      await driver.tap(serverFinder);
      await driver.enterText('ws://echo.websocket.org');
    
      // Tap the connect button to reach the 
      await driver.tap(connectFinder);

      // Wait for the next page to load
      expect(await isPresent(messageFinder, driver), true);

      // Enter a message into the message field
      await driver.tap(messageFinder);
      await driver.enterText(message);
      await driver.tap(sendFinder);

      // Wait for a response to be triggered
      expect(await isPresent(receivedFinder, driver), true);
      expect(await driver.getText(receivedFinder), message);
    });
  });
}

