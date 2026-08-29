package info.karuru.cariot.ui.meter

import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import info.karuru.cariot.meter.MeterSlot
import info.karuru.cariot.meter.MeterSlotJson
import info.karuru.cariot.obd.GaugeStyle
import info.karuru.cariot.obd.ObdMetric
import info.karuru.cariot.obd.obdMetricMeta

// メーター項目の追加/削除/スタイル変更を行うボトムシート。mobile/lib/widgets/meter_settings_sheet.dart
// を移植（JSON形式のインポート/エクスポートも含む）。
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MeterSettingsSheet(
    slots: List<MeterSlot>,
    onDismiss: () -> Unit,
    onSave: (List<MeterSlot>) -> Unit,
) {
  val context = LocalContext.current
  var editedSlots by remember { mutableStateOf(slots) }
  var styleDialogIndex by remember { mutableStateOf<Int?>(null) }
  var showAddDialog by remember { mutableStateOf(false) }

  // Android標準のSAF(Storage Access Framework)でファイル選択させる。新規ライブラリ不要
  // （docs/car_iot_android_plan.md Phase8）。
  val exportLauncher = rememberLauncherForActivityResult(
      ActivityResultContracts.CreateDocument("application/json"),
  ) { uri ->
    if (uri == null) return@rememberLauncherForActivityResult
    try {
      context.contentResolver.openOutputStream(uri)?.use { out ->
        out.write(MeterSlotJson.encode(editedSlots).toByteArray())
      }
      Toast.makeText(context, "エクスポートしました", Toast.LENGTH_SHORT).show()
    } catch (e: Exception) {
      Toast.makeText(context, "エクスポートに失敗しました", Toast.LENGTH_SHORT).show()
    }
  }

  val importLauncher = rememberLauncherForActivityResult(
      ActivityResultContracts.OpenDocument(),
  ) { uri ->
    if (uri == null) return@rememberLauncherForActivityResult
    val text = try {
      context.contentResolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() }
    } catch (e: Exception) {
      null
    }
    // mobile/lib/services/meter_config_service.dartのimportFromJson()と同じく、
    // load()と違いデフォルト値へはフォールバックせず失敗をそのまま伝える。
    val imported = text?.let { MeterSlotJson.decode(it) }
    if (imported != null) {
      editedSlots = imported
      Toast.makeText(context, "インポートしました", Toast.LENGTH_SHORT).show()
    } else {
      Toast.makeText(context, "インポートに失敗しました", Toast.LENGTH_SHORT).show()
    }
  }

  ModalBottomSheet(onDismissRequest = onDismiss) {
    Column(modifier = Modifier.padding(16.dp)) {
      Text("メーター項目を編集", style = MaterialTheme.typography.titleMedium)
      Spacer(Modifier.height(8.dp))

      LazyColumn(modifier = Modifier.heightIn(max = 400.dp)) {
        itemsIndexed(editedSlots) { index, slot ->
          val meta = obdMetricMeta.getValue(slot.metric)
          ListItem(
              headlineContent = { Text(meta.label) },
              supportingContent = { Text(slot.style.label) },
              modifier = Modifier.clickable { styleDialogIndex = index },
              trailingContent = {
                IconButton(onClick = {
                  editedSlots = editedSlots.filterIndexed { i, _ -> i != index }
                }) {
                  Icon(Icons.Filled.Delete, contentDescription = "削除")
                }
              },
          )
        }
      }

      TextButton(onClick = { showAddDialog = true }) { Text("項目を追加") }

      Row(modifier = Modifier.fillMaxWidth()) {
        TextButton(onClick = { exportLauncher.launch("meter_config.json") }) { Text("エクスポート") }
        TextButton(onClick = { importLauncher.launch(arrayOf("application/json")) }) { Text("インポート") }
      }

      Row(modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) {
        TextButton(onClick = onDismiss) { Text("キャンセル") }
        Spacer(Modifier.weight(1f))
        Button(onClick = { onSave(editedSlots) }) { Text("保存") }
      }
    }
  }

  styleDialogIndex?.let { index ->
    StyleSelectDialog(
        current = editedSlots[index].style,
        onSelect = { style ->
          editedSlots = editedSlots.toMutableList().also { it[index] = it[index].copy(style = style) }
          styleDialogIndex = null
        },
        onDismiss = { styleDialogIndex = null },
    )
  }

  if (showAddDialog) {
    AddSlotDialog(
        onAdd = { metric, style ->
          editedSlots = editedSlots + MeterSlot(metric, style)
          showAddDialog = false
        },
        onDismiss = { showAddDialog = false },
    )
  }
}

@Composable
private fun StyleSelectDialog(current: GaugeStyle, onSelect: (GaugeStyle) -> Unit, onDismiss: () -> Unit) {
  AlertDialog(
      onDismissRequest = onDismiss,
      title = { Text("ゲージ種別を選択") },
      text = {
        Column {
          GaugeStyle.entries.forEach { style ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { onSelect(style) }
                    .padding(vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
              RadioButton(selected = style == current, onClick = { onSelect(style) })
              Text(style.label)
            }
          }
        }
      },
      confirmButton = {},
  )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddSlotDialog(onAdd: (ObdMetric, GaugeStyle) -> Unit, onDismiss: () -> Unit) {
  var selectedMetric by remember { mutableStateOf(ObdMetric.RPM) }
  var selectedStyle by remember { mutableStateOf(GaugeStyle.CIRCULAR) }
  var metricMenuExpanded by remember { mutableStateOf(false) }
  var styleMenuExpanded by remember { mutableStateOf(false) }

  AlertDialog(
      onDismissRequest = onDismiss,
      title = { Text("項目を追加") },
      text = {
        Column {
          ExposedDropdownMenuBox(
              expanded = metricMenuExpanded,
              onExpandedChange = { metricMenuExpanded = it },
          ) {
            TextField(
                value = obdMetricMeta.getValue(selectedMetric).label,
                onValueChange = {},
                readOnly = true,
                label = { Text("項目") },
                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = metricMenuExpanded) },
                modifier = Modifier.menuAnchor(MenuAnchorType.PrimaryNotEditable),
            )
            ExposedDropdownMenu(
                expanded = metricMenuExpanded,
                onDismissRequest = { metricMenuExpanded = false },
            ) {
              ObdMetric.entries.forEach { metric ->
                DropdownMenuItem(
                    text = { Text(obdMetricMeta.getValue(metric).label) },
                    onClick = {
                      selectedMetric = metric
                      metricMenuExpanded = false
                    },
                )
              }
            }
          }
          Spacer(Modifier.height(8.dp))
          // ゲージ種別もドロップダウンにする。以前は横1列のチップだったが、日本語ラベル
          // (サーキュラー/デジタル数値/バー/ミニグラフ)がダイアログ幅を超え、最後の
          // 「ミニグラフ」が画面外に切れて選べなくなっていた（実機で発覚）。
          // 上の項目選択と形式を揃えることで、種別がいくつ増えても破綻しない。
          ExposedDropdownMenuBox(
              expanded = styleMenuExpanded,
              onExpandedChange = { styleMenuExpanded = it },
          ) {
            TextField(
                value = selectedStyle.label,
                onValueChange = {},
                readOnly = true,
                label = { Text("ゲージ種別") },
                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = styleMenuExpanded) },
                modifier = Modifier.menuAnchor(MenuAnchorType.PrimaryNotEditable),
            )
            ExposedDropdownMenu(
                expanded = styleMenuExpanded,
                onDismissRequest = { styleMenuExpanded = false },
            ) {
              GaugeStyle.entries.forEach { style ->
                DropdownMenuItem(
                    text = { Text(style.label) },
                    onClick = {
                      selectedStyle = style
                      styleMenuExpanded = false
                    },
                )
              }
            }
          }
        }
      },
      confirmButton = {
        TextButton(onClick = { onAdd(selectedMetric, selectedStyle) }) { Text("追加") }
      },
      dismissButton = {
        TextButton(onClick = onDismiss) { Text("キャンセル") }
      },
  )
}
