import 'package:flutter/material.dart';

class DebugToggleCard extends StatelessWidget {
  final bool value;
  final ValueChanged<bool> onChanged;
  const DebugToggleCard({super.key, required this.value, required this.onChanged});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
        child: Row(
          children: [
            const Expanded(
              child: Text('デバッグモード（ログ表示）', style: TextStyle(fontSize: 13)),
            ),
            Switch(
              value: value,
              onChanged: onChanged,
              activeThumbColor: const Color(0xFF4F8EF7),
            ),
          ],
        ),
      ),
    );
  }
}
