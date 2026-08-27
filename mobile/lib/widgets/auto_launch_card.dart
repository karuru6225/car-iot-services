import 'package:flutter/material.dart';

import '../theme/app_colors.dart';
import 'card_widgets.dart';

class AutoLaunchCard extends StatelessWidget {
  final bool enabled;
  final VoidCallback onEnable;

  const AutoLaunchCard({super.key, required this.enabled, required this.onEnable});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
        child: Row(
          children: [
            Expanded(
              child: Text(
                enabled ? '自動起動: 有効' : '自動起動: 未設定',
                style: const TextStyle(fontSize: 13),
              ),
            ),
            actionButton(enabled ? '再設定' : '有効化', onEnable,
                enabled: true, color: AppColors.primary),
          ],
        ),
      ),
    );
  }
}
