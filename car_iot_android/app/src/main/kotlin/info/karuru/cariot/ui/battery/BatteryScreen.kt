package info.karuru.cariot.ui.battery

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.state.CarIotState

private data class BatteryItem(val label: String, val value: Float?, val unit: String, val decimals: Int)

// 「バッテリー」タブ。mobile/lib/widgets/meas_card.dartの4項目をそのまま移植。
// デザインレビュー(2026/08)を反映: 素のTextの羅列だとカードが無く画面下半分が単色の余白に
// なっていたため、Cardで各項目を囲み高さを持たせて情報密度を上げた。数値はテーマの
// displaySmall(レーシング=等幅イタリック、ミニマル=極太)を使い、走行中でも一目で読める大きさにする。
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
      modifier = Modifier.padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(12.dp),
      horizontalArrangement = Arrangement.spacedBy(12.dp),
  ) {
    items(items) { item -> BatteryTile(item) }
  }
}

@Composable
private fun BatteryTile(item: BatteryItem) {
  Card(modifier = Modifier.fillMaxWidth().heightIn(min = 120.dp)) {
    Column(modifier = Modifier.padding(16.dp)) {
      Text(
          item.label,
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
      )
      val valueText = item.value?.let { "%.${item.decimals}f".format(it) } ?: "—"
      // shadcn は数値をアクセント色で塗らず前景色のまま出す。値なしのときだけ
      // muted-foreground に落として「まだ来ていない」ことを示す。
      val valueColor = if (item.value != null) {
        MaterialTheme.colorScheme.onSurface
      } else {
        MaterialTheme.colorScheme.onSurfaceVariant
      }
      Text(
          if (item.value != null) "$valueText ${item.unit}" else valueText,
          style = MaterialTheme.typography.displaySmall.copy(fontSize = 26.sp),
          color = valueColor,
          modifier = Modifier.padding(top = 8.dp),
      )
    }
  }
}
