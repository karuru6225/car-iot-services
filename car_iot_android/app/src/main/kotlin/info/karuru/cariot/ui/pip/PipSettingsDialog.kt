package info.karuru.cariot.ui.pip

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import info.karuru.cariot.obd.ObdMetric
import info.karuru.cariot.obd.obdMetricMeta

// PiP(ピクチャーインピクチャー)表示に使う項目を選ぶダイアログ。接続タブから開く。
// メーター項目編集(MeterSettingsSheet)と違いスタイル選択は無く、項目の複数選択のみ。
@Composable
fun PipSettingsDialog(
    selected: List<ObdMetric>,
    onSave: (List<ObdMetric>) -> Unit,
    onDismiss: () -> Unit,
) {
  var editedSelection by remember { mutableStateOf(selected.toSet()) }

  AlertDialog(
      onDismissRequest = onDismiss,
      title = { Text("PiP表示項目を選択") },
      text = {
        LazyColumn(modifier = Modifier.heightIn(max = 400.dp)) {
          items(ObdMetric.entries) { metric ->
            val checked = metric in editedSelection
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable {
                      editedSelection = if (checked) {
                        editedSelection - metric
                      } else {
                        editedSelection + metric
                      }
                    }
                    .padding(vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
              Checkbox(checked = checked, onCheckedChange = null)
              Text(obdMetricMeta.getValue(metric).label)
            }
          }
        }
      },
      confirmButton = {
        // 選択0件のままの保存はPiP表示側で表示するものが無くなるため許可しない
        // （PipConfigStore.load()の空リストフォールバックはインポート破損等の保険であり、
        // ここでは意図的な0件保存自体を弾く）。
        TextButton(
            onClick = { onSave(editedSelection.toList()) },
            enabled = editedSelection.isNotEmpty(),
        ) { Text("保存") }
      },
      dismissButton = {
        TextButton(onClick = onDismiss) { Text("キャンセル") }
      },
  )
}
