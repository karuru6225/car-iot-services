package info.karuru.cariot.ui.battery

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
import info.karuru.cariot.state.CarIotState

private data class BatteryItem(val label: String, val value: Float?, val unit: String, val decimals: Int)

// 「バッテリー」タブ。mobile/lib/widgets/meas_card.dartの4項目をそのまま移植。
@Composable
fun BatteryScreen() {
  val measurement by CarIotState.measurement.collectAsStateWithLifecycle()
  val items = listOf(
      BatteryItem("メイン電圧", measurement.vMain, "V", 3),
      BatteryItem("電流", measurement.curr, "A", 3),
      BatteryItem("電力", measurement.pwr, "W", 2),
      BatteryItem("サブ電圧", measurement.vSub, "V", 3),
  )

  LazyVerticalGrid(
      columns = GridCells.Fixed(2),
      modifier = Modifier.padding(24.dp),
  ) {
    items(items) { item -> BatteryTile(item) }
  }
}

@Composable
private fun BatteryTile(item: BatteryItem) {
  Column(modifier = Modifier.padding(12.dp)) {
    Text(item.label, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
    val valueText = item.value?.let { "%.${item.decimals}f".format(it) } ?: "—"
    val valueColor = if (item.value != null) {
      MaterialTheme.colorScheme.primary
    } else {
      MaterialTheme.colorScheme.onSurfaceVariant
    }
    Text(
        if (item.value != null) "$valueText ${item.unit}" else valueText,
        fontSize = 22.sp,
        fontWeight = FontWeight.Bold,
        color = valueColor,
    )
  }
}
