// BLE ダッシュボードアプリの起動確認スモークテスト。

import 'package:flutter_test/flutter_test.dart';

import 'package:car_iot_ble/main.dart';

void main() {
  testWidgets('起動して未接続状態のダッシュボードが表示される', (WidgetTester tester) async {
    await tester.pumpWidget(const App());

    // AppBar のタイトルが表示される
    expect(find.text('BLE ダッシュボード'), findsOneWidget);

    // 起動直後は未接続状態
    expect(find.text('未接続'), findsOneWidget);
  });
}
