package info.karuru.cariot.ui.battery

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
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
import info.karuru.cariot.ui.meter.ValueRail

private data class BatteryItem(
    val label: String,
    val value: Float?,
    val unit: String,
    val decimals: Int,
    // レンジは実車の常用域。ValueRail が「今どのあたりか」を示すために使う。
    val min: Float,
    val max: Float,
)

// 「バッテリー」タブ。mobile/lib/widgets/meas_card.dartの4項目を移植。
//
// デザイン刷新(2026/08): 4項目を均等な2x2で並べるのをやめ、メイン電圧を全幅の
// ヒーロータイルにした。運転中に見たいのは主にメインバッテリーの電圧で、
// 4つが同格に並んでいると「どれを見ればいいか」が毎回判断になる。
// レイアウトそのものに優先順位を持たせている。
@Composable
fun BatteryScreen() {
  val measurement by CarIotState.measurement.collectAsStateWithLifecycle()

  val hero = BatteryItem("メイン電圧", measurement.vMain, "V", 3, min = 11f, max = 15f)
  val rest = listOf(
      BatteryItem("電流", measurement.curr, "A", 3, min = -20f, max = 20f),
      BatteryItem("電力", measurement.pwr, "W", 2, min = -250f, max = 250f),
      BatteryItem("サブ電圧", measurement.vSub, "V", 3, min = 11f, max = 15f),
  )

  Column(
      modifier = Modifier
          .fillMaxWidth()
          .verticalScroll(rememberScrollState())
          .padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(12.dp),
  ) {
    HeroTile(hero)
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
      rest.forEach { item ->
        SmallTile(item, modifier = Modifier.weight(1f))
      }
    }
  }
}

// 値が無いときは "—" ではなく書式どおりの桁プレースホルダー("--.---")を出す。
// 巨大な文字サイズでは "—" 一文字が黒い帯に見えてしまうのと、消灯したセグメントを
// 見せるのが計器の作法なため。桁数が変わってもレイアウトが動かない利点もある。
private fun BatteryItem.formatted(): String =
    value?.let { "%.${decimals}f".format(it) }
        ?: buildString {
          append("--")
          if (decimals > 0) {
            append('.')
            repeat(decimals) { append('-') }
          }
        }

private fun BatteryItem.fraction(): Float? {
  val v = value ?: return null
  val range = max - min
  if (range == 0f) return null
  return ((v - min) / range).coerceIn(0f, 1f)
}

@Composable
private fun HeroTile(item: BatteryItem) {
  Card(modifier = Modifier.fillMaxWidth()) {
    Column(modifier = Modifier.padding(20.dp)) {
      Text(
          item.label,
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
      )
      Row(
          modifier = Modifier.padding(top = 6.dp),
          verticalAlignment = androidx.compose.ui.Alignment.Bottom,
      ) {
        Text(
            item.formatted(),
            style = MaterialTheme.typography.displayLarge,
            color = if (item.value != null) {
              MaterialTheme.colorScheme.onSurface
            } else {
              MaterialTheme.colorScheme.outline
            },
        )
        // 単位は数値に従属する情報なので、極小・くすませてベースラインに沿わせる。
        Text(
            item.unit,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(start = 8.dp, bottom = 12.dp),
        )
      }
      ValueRail(item.fraction(), modifier = Modifier.padding(top = 4.dp))
    }
  }
}

@Composable
private fun SmallTile(item: BatteryItem, modifier: Modifier = Modifier) {
  Card(modifier = modifier.heightIn(min = 116.dp)) {
    Column(modifier = Modifier.padding(14.dp)) {
      Text(
          item.label,
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
      )
      // 3列に並ぶぶん1タイルが狭く、小数3桁("12.604")は26spだと折り返してしまう。
      // 桁を削ると計測値としての情報が落ちるので、文字サイズ側を詰めて1行に収める。
      Text(
          item.formatted(),
          style = MaterialTheme.typography.displaySmall.copy(fontSize = 20.sp),
          color = if (item.value != null) {
            MaterialTheme.colorScheme.onSurface
          } else {
            MaterialTheme.colorScheme.outline
          },
          maxLines = 1,
          modifier = Modifier.padding(top = 6.dp),
      )
      ValueRail(item.fraction(), modifier = Modifier.padding(top = 6.dp))
    }
  }
}
