import 'package:flutter/material.dart';

import '../models/log_entry.dart';
import '../theme/app_colors.dart';
import 'card_widgets.dart';

class LogCard extends StatelessWidget {
  final List<LogEntry> log;
  final ScrollController scroll;
  const LogCard({super.key, required this.log, required this.scroll});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            cardLabel('ログ'),
            const SizedBox(height: 8),
            Container(
              height: 200,
              decoration: BoxDecoration(
                color: AppColors.logBackground,
                borderRadius: BorderRadius.circular(6),
              ),
              child: ListView.builder(
                controller: scroll,
                padding: const EdgeInsets.all(8),
                itemCount: log.length,
                itemBuilder: (_, i) {
                  final e = log[i];
                  final ts =
                      '${e.time.hour.toString().padLeft(2, '0')}:'
                      '${e.time.minute.toString().padLeft(2, '0')}:'
                      '${e.time.second.toString().padLeft(2, '0')}';
                  final msgColor = switch (e.type) {
                    LogType.rx  => AppColors.success,
                    LogType.tx  => AppColors.warning,
                    LogType.err => AppColors.danger,
                    LogType.sys => AppColors.muted,
                  };
                  return RichText(
                    text: TextSpan(
                      style: const TextStyle(
                          fontFamily: 'monospace', fontSize: 12, height: 1.6),
                      children: [
                        TextSpan(text: '$ts  ',
                            style: const TextStyle(color: AppColors.timestamp)),
                        TextSpan(text: e.msg,
                            style: TextStyle(color: msgColor)),
                      ],
                    ),
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}
