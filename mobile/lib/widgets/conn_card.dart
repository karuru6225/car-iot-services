import 'package:flutter/material.dart';

import '../models/conn_state.dart';
import 'card_widgets.dart';

class ConnCard extends StatelessWidget {
  final ConnState state;
  final String deviceName;
  final VoidCallback onConnect;
  final VoidCallback onDisconnect;

  const ConnCard({
    super.key,
    required this.state,
    required this.deviceName,
    required this.onConnect,
    required this.onDisconnect,
  });

  @override
  Widget build(BuildContext context) {
    final (dotColor, label) = switch (state) {
      ConnState.disconnected => (const Color(0xFFE74C3C), '未接続'),
      ConnState.scanning     => (const Color(0xFFF39C12), 'スキャン中...'),
      ConnState.connecting   => (const Color(0xFFF39C12), '接続中...'),
      ConnState.connected    => (const Color(0xFF2ECC71), '接続済み'),
    };
    final isBusy = state == ConnState.scanning || state == ConnState.connecting;
    final isConn = state == ConnState.connected;

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            cardLabel('接続'),
            const SizedBox(height: 12),
            Row(
              children: [
                Container(
                  width: 12, height: 12,
                  decoration: BoxDecoration(color: dotColor, shape: BoxShape.circle),
                ),
                const SizedBox(width: 10),
                Text(label),
                if (deviceName.isNotEmpty) ...[
                  const Spacer(),
                  Text(deviceName,
                      style: const TextStyle(color: Colors.grey, fontSize: 12)),
                ],
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                actionButton('接続', onConnect,
                    enabled: !isConn && !isBusy,
                    color: const Color(0xFF4F8EF7)),
                const SizedBox(width: 10),
                actionButton(isBusy ? '中止' : '切断', onDisconnect,
                    enabled: isConn || isBusy,
                    color: const Color(0xFFC0392B)),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
