package info.karuru.cariot.ui.obd

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.obd.ObdMetric
import info.karuru.cariot.obd.obdMetricMeta
import info.karuru.cariot.state.CarIotState

// 「OBD」タブ。mobile/lib/widgets/obd_card.dartを移植。
@Composable
fun ObdScreen() {
  val reading by CarIotState.obdReading.collectAsStateWithLifecycle()
  val current = reading

  when {
    current == null -> Text("—", modifier = Modifier.padding(24.dp))
    !current.valid -> Text(
        "応答なし（IGN OFF または CAN 未接続）",
        modifier = Modifier.padding(24.dp),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    else -> LazyVerticalGrid(
        columns = GridCells.Fixed(2),
        modifier = Modifier.padding(24.dp),
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
  Column(modifier = Modifier.padding(8.dp)) {
    Text(label, fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
    val valueText = "%.${decimals}f".format(value)
    Text(
        if (unit.isEmpty()) valueText else "$valueText $unit",
        fontSize = 14.sp,
        fontWeight = FontWeight.Bold,
        color = MaterialTheme.colorScheme.primary,
    )
  }
}
