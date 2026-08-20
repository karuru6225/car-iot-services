import 'dart:convert';
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../models/meter_slot.dart';
import '../models/obd_metric.dart';
import '../services/meter_config_service.dart';
import '../theme/app_colors.dart';
import 'card_widgets.dart';

// メーター画面のタイル編集用ボトムシート。
// Navigator.pop(context, updatedList) で編集結果を返す（キャンセル時はnullのまま閉じる）。
class MeterSettingsSheet extends StatefulWidget {
  final List<MeterSlot> initial;
  const MeterSettingsSheet({super.key, required this.initial});

  @override
  State<MeterSettingsSheet> createState() => _MeterSettingsSheetState();
}

class _MeterSettingsSheetState extends State<MeterSettingsSheet> {
  final _configService = MeterConfigService();
  late List<MeterSlot> _slots;
  bool _adding = false;
  ObdMetric _newMetric = ObdMetric.values.first;
  GaugeStyle _newStyle = GaugeStyle.digital;

  @override
  void initState() {
    super.initState();
    _slots = List.of(widget.initial);
  }

  void _removeAt(int index) => setState(() => _slots.removeAt(index));

  Future<void> _exportToFile() async {
    final json = _configService.exportToJson(_slots);
    try {
      await FilePicker.saveFile(
        dialogTitle: 'メーター設定をエクスポート',
        fileName: 'meter_config.json',
        bytes: Uint8List.fromList(utf8.encode(json)),
        mimeType: 'application/json',
        type: FileType.custom,
        allowedExtensions: ['json'],
      );
    } catch (e) {
      if (mounted) _showMessage('エクスポートに失敗しました: $e');
    }
  }

  Future<void> _importFromFile() async {
    PlatformFile? file;
    try {
      file = await FilePicker.pickFile(type: FileType.custom, allowedExtensions: ['json']);
    } catch (e) {
      if (mounted) _showMessage('ファイル選択に失敗しました: $e');
      return;
    }
    if (file == null) return; // キャンセル

    try {
      final bytes = await file.readAsBytes();
      final imported = _configService.importFromJson(utf8.decode(bytes));
      if (imported == null) {
        if (mounted) _showMessage('設定ファイルの読み込みに失敗しました（形式が不正です）');
        return;
      }
      setState(() => _slots = imported);
      if (mounted) _showMessage('${imported.length}件の項目をインポートしました');
    } catch (e) {
      if (mounted) _showMessage('インポートに失敗しました: $e');
    }
  }

  void _showMessage(String message) {
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  void _addSlot() {
    setState(() {
      _slots.add(MeterSlot(metric: _newMetric, style: _newStyle));
      _adding = false;
    });
  }

  Future<void> _changeStyle(int index) async {
    final current = _slots[index];
    final picked = await showDialog<GaugeStyle>(
      context: context,
      builder: (context) => SimpleDialog(
        title: Text('${obdMetricMeta[current.metric]!.label} の表示形式'),
        children: [
          for (final style in GaugeStyle.values)
            SimpleDialogOption(
              onPressed: () => Navigator.pop(context, style),
              child: Text(style.label),
            ),
        ],
      ),
    );
    if (picked != null) {
      setState(() => _slots[index] = MeterSlot(metric: current.metric, style: picked));
    }
  }

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Padding(
        padding: EdgeInsets.only(
          left: 16, right: 16, top: 16,
          bottom: 16 + MediaQuery.of(context).viewInsets.bottom,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(child: cardLabel('メーター項目の編集')),
                IconButton(
                  icon: const Icon(Icons.file_upload_outlined),
                  tooltip: 'ファイルからインポート',
                  onPressed: _importFromFile,
                ),
                IconButton(
                  icon: const Icon(Icons.file_download_outlined),
                  tooltip: 'ファイルへエクスポート',
                  onPressed: _exportToFile,
                ),
                IconButton(
                  icon: const Icon(Icons.check, color: AppColors.primary),
                  onPressed: () => Navigator.pop(context, _slots),
                ),
              ],
            ),
            const SizedBox(height: 8),
            ConstrainedBox(
              constraints: BoxConstraints(maxHeight: MediaQuery.of(context).size.height * 0.5),
              child: _slots.isEmpty
                  ? const Padding(
                      padding: EdgeInsets.symmetric(vertical: 24),
                      child: Text('項目がありません。「＋項目を追加」から追加してください',
                          style: TextStyle(color: Colors.grey)),
                    )
                  : ListView.builder(
                      shrinkWrap: true,
                      itemCount: _slots.length,
                      itemBuilder: (context, i) {
                        final slot = _slots[i];
                        final meta = obdMetricMeta[slot.metric]!;
                        return ListTile(
                          dense: true,
                          title: Text(meta.label),
                          subtitle: Text(slot.style.label),
                          onTap: () => _changeStyle(i),
                          trailing: IconButton(
                            icon: const Icon(Icons.delete_outline, color: AppColors.danger),
                            onPressed: () => _removeAt(i),
                          ),
                        );
                      },
                    ),
            ),
            const SizedBox(height: 8),
            if (_adding) _buildAddForm() else _buildAddButton(),
          ],
        ),
      ),
    );
  }

  Widget _buildAddButton() {
    return actionButton(
      '＋項目を追加',
      () => setState(() => _adding = true),
      enabled: true,
      color: AppColors.primary,
    );
  }

  Widget _buildAddForm() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        DropdownButtonFormField<ObdMetric>(
          initialValue: _newMetric,
          isExpanded: true,
          decoration: const InputDecoration(labelText: '項目'),
          items: [
            for (final metric in ObdMetric.values)
              DropdownMenuItem(value: metric, child: Text(obdMetricMeta[metric]!.label)),
          ],
          onChanged: (v) => setState(() => _newMetric = v ?? _newMetric),
        ),
        const SizedBox(height: 8),
        Wrap(
          spacing: 8,
          children: [
            for (final style in GaugeStyle.values)
              ChoiceChip(
                label: Text(style.label),
                selected: _newStyle == style,
                onSelected: (_) => setState(() => _newStyle = style),
              ),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            actionButton('追加', _addSlot, enabled: true, color: AppColors.primary),
            const SizedBox(width: 8),
            actionButton(
              'キャンセル',
              () => setState(() => _adding = false),
              enabled: true,
              color: AppColors.disabled,
            ),
          ],
        ),
      ],
    );
  }
}
