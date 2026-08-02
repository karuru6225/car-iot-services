import 'package:flutter/material.dart';

// アプリ全体で使う配色。ThemeData と各ウィジェットで共有し、色コードの重複定義を避ける。
class AppColors {
  AppColors._();

  static const primary    = Color(0xFF4F8EF7);
  static const surface    = Color(0xFF16213E);
  static const background = Color(0xFF1A1A2E);
  static const logBackground = Color(0xFF0F1A2E);
  static const timestamp  = Color(0xFF4A6A8A);

  static const success    = Color(0xFF2ECC71);
  static const warning    = Color(0xFFF39C12);
  static const danger     = Color(0xFFE74C3C);
  static const dangerDark = Color(0xFFC0392B);
  static const disabled   = Color(0xFF2A4060);
  static const muted      = Color(0xFF888888);
}
