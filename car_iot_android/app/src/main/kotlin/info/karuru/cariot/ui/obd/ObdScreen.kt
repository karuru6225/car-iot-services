package info.karuru.cariot.ui.obd

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.obd.ObdMetric
import info.karuru.cariot.obd.obdMetricMeta
import info.karuru.cariot.state.CarIotState

// 「OBD」タブ。mobile/lib/widgets/obd_card.dartを移植。
// デザインレビュー(2026/08)を反映: 35項目あるため重いCardではなくOutlinedCard(細枠のみ)で
// 各項目を軽く囲み、テレメトリのデータグリッドらしい密度で並べる。数値はテーマの
// displaySmallを縮小して使い、バッテリータブと表記の系統を揃える。
@Composable
fun ObdScreen() {
  val reading by CarIotState.obdReading.collectAsStateWithLifecycle()
  val current = reading

  when {
    current == null -> Text(
        "—",
        modifier = Modifier.padding(24.dp),
        style = MaterialTheme.typography.bodyLarge,
    )
    !current.valid -> Text(
        "応答なし（IGN OFF または CAN 未接続）",
        modifier = Modifier.padding(24.dp),
        style = MaterialTheme.typography.bodyLarge,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    else -> LazyVerticalGrid(
        columns = GridCells.Fixed(2),
        modifier = Modifier.padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
      items(ObdMetric.entries) { metric ->
        val meta = obdMetricMeta.getValue(metric)
        ObdTile(meta.label, meta.valueOf(current), meta.unit, meta.decimals)
      }
    }
  }
}

@Composable
private fun ObdTile(label: String, value: Float, unit: String, decimals: Int) {
  OutlinedCard(modifier = Modifier.fillMaxWidth()) {
    Column(modifier = Modifier.padding(10.dp)) {
      Text(
          label,
          style = MaterialTheme.typography.labelSmall.copy(fontSize = 9.sp),
          color = MaterialTheme.colorScheme.onSurfaceVariant,
      )
      val valueText = "%.${decimals}f".format(value)
      Text(
          if (unit.isEmpty()) valueText else "$valueText $unit",
          style = MaterialTheme.typography.displaySmall.copy(fontSize = 15.sp),
          color = MaterialTheme.colorScheme.primary,
          modifier = Modifier.padding(top = 2.dp),
      )
    }
  }
}
