import 'package:flutter/material.dart';

import '../theme/app_colors.dart';

Widget cardLabel(String text) => Text(
      text,
      style: const TextStyle(
          fontSize: 11,
          color: Colors.grey,
          letterSpacing: 1,
          fontWeight: FontWeight.w600),
    );

Widget actionButton(String label, VoidCallback onTap,
    {required bool enabled, required Color color}) {
  return ElevatedButton(
    onPressed: enabled ? onTap : null,
    style: ElevatedButton.styleFrom(
      backgroundColor: color,
      disabledBackgroundColor: AppColors.disabled,
      minimumSize: const Size(72, 44),
    ),
    child: Text(label, style: const TextStyle(color: Colors.white)),
  );
}
