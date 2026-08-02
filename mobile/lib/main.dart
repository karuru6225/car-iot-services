import 'package:flutter/material.dart';

import 'screens/ble_home_screen.dart';
import 'theme/app_colors.dart';

void main() => runApp(const App());

class App extends StatelessWidget {
  const App({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'BLE ダッシュボード',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: const ColorScheme.dark(
          primary: AppColors.primary,
          surface: AppColors.surface,
        ),
        scaffoldBackgroundColor: AppColors.background,
        cardColor: AppColors.surface,
        useMaterial3: true,
      ),
      home: const BleHome(),
    );
  }
}
