import 'package:flutter/material.dart';

import '../theme/app_colors.dart';
import 'card_widgets.dart';

class AuthCard extends StatelessWidget {
  final String? userEmail;
  final bool busy;
  final VoidCallback onSignIn;
  final VoidCallback onSignOut;

  const AuthCard({
    super.key,
    required this.userEmail,
    required this.busy,
    required this.onSignIn,
    required this.onSignOut,
  });

  @override
  Widget build(BuildContext context) {
    final signedIn = userEmail != null;

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            cardLabel('OBDデータ送信アカウント'),
            const SizedBox(height: 12),
            Row(
              children: [
                Container(
                  width: 12,
                  height: 12,
                  decoration: BoxDecoration(
                    color: signedIn ? AppColors.success : AppColors.danger,
                    shape: BoxShape.circle,
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: Text(
                    signedIn ? userEmail! : '未ログイン',
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                actionButton(
                  signedIn ? 'ログアウト' : 'Googleでログイン',
                  signedIn ? onSignOut : onSignIn,
                  enabled: !busy,
                  color: signedIn ? AppColors.dangerDark : AppColors.primary,
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
