package info.karuru.cariot.ui.pip

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.meter.MeterSlot
import info.karuru.cariot.obd.GaugeStyle
import info.karuru.cariot.obd.ObdMetricMeta
import info.karuru.cariot.obd.ObdReading
import info.karuru.cariot.obd.obdMetricMeta
import info.karuru.cariot.state.CarIotState
import info.karuru.cariot.ui.meter.AnalogDial
import info.karuru.cariot.ui.meter.ValueRail

// PiP(ピクチャーインピクチャー)ウィンドウ専用の表示。
//
// 項目ごとにゲージ種別を選べる（メータータブと同じ MeterSlot を使う）。ただしPiPの
// ウィンドウは小さいので、メータータブと同じ描画をそのまま持ち込むと読めない。
// 種別に応じてレイアウトを変える:
//
//   ・全項目がデジタル数値 … 縦に積む。ラベル左・値右で一覧性が高い（従来どおり）
//   ・ゲージを含む        … 横に並べる。ゲージは縦方向に場所を取るため、
//                            狭い高さに複数積むと潰れる
//
// レイアウトが2種類あるぶんウィンドウの縦横比も変える必要があるので、
// MainActivity.pipAspectRatio() が同じ判定を持っている（片方だけ直すとズレる）。
@Composable
fun PipContent(slots: List<MeterSlot>) {
  val reading by CarIotState.obdReading.collectAsStateWithLifecycle()
  val history by CarIotState.obdHistory.collectAsStateWithLifecycle()

  Surface(modifier = Modifier.fillMaxSize()) {
    if (slots.all { it.style == GaugeStyle.DIGITAL }) {
      Column(
          modifier = Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 8.dp),
          verticalArrangement = Arrangement.SpaceEvenly,
      ) {
        slots.forEach { slot ->
          TextRow(slot, reading, modifier = Modifier.weight(1f))
        }
      }
    } else {
      Row(
          modifier = Modifier.fillMaxSize().padding(horizontal = 10.dp, vertical = 8.dp),
          horizontalArrangement = Arrangement.spacedBy(10.dp),
      ) {
        slots.forEach { slot ->
          GaugeCell(slot, reading, history[slot.metric].orEmpty(), modifier = Modifier.weight(1f))
        }
      }
    }
  }
}

// 値が無いときは書式どおりの桁プレースホルダー（他画面と同じ作法）。
private fun formatValue(meta: ObdMetricMeta, reading: ObdReading?): String {
  val v = reading?.let { meta.valueOf(it) }
  return v?.let { "%.${meta.decimals}f".format(it) }
      ?: buildString {
        append("--")
        if (meta.decimals > 0) {
          append('.')
          repeat(meta.decimals) { append('-') }
        }
      }
}

@Composable
private fun ColumnScope.TextRow(slot: MeterSlot, reading: ObdReading?, modifier: Modifier = Modifier) {
  val meta = obdMetricMeta.getValue(slot.metric)
  Row(
      modifier = modifier.fillMaxWidth(),
      horizontalArrangement = Arrangement.SpaceBetween,
      verticalAlignment = Alignment.CenterVertically,
  ) {
    PipText(meta.label, MaterialTheme.colorScheme.onSurfaceVariant)
    val value = formatValue(meta, reading)
    PipText(
        if (meta.unit.isEmpty()) value else "$value ${meta.unit}",
        MaterialTheme.colorScheme.onSurface,
    )
  }
}

@Composable
private fun RowScope.GaugeCell(
    slot: MeterSlot,
    reading: ObdReading?,
    history: List<Float>,
    modifier: Modifier = Modifier,
) {
  val meta = obdMetricMeta.getValue(slot.metric)
  val value = reading?.let { meta.valueOf(it) }
  val valueText = formatValue(meta, reading)

  Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
    Text(
        meta.label,
        fontSize = 9.sp,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
    )
    Text(
        if (meta.unit.isEmpty()) valueText else "$valueText ${meta.unit}",
        style = MaterialTheme.typography.displaySmall.copy(fontSize = 15.sp),
        color = MaterialTheme.colorScheme.onSurface,
        maxLines = 1,
    )
    // ゲージ本体。PiPでは高さが取れないので、メータータブより一段小さく描く。
    when (slot.style) {
      GaugeStyle.CIRCULAR -> AnalogDial(
          value = value,
          min = meta.min,
          max = meta.max,
          decimals = meta.decimals,
          modifier = Modifier.padding(top = 2.dp),
      )
      GaugeStyle.BAR -> ValueRail(
          fraction = value?.let { ((it - meta.min) / (meta.max - meta.min)).coerceIn(0f, 1f) },
          modifier = Modifier.padding(top = 6.dp),
      )
      GaugeStyle.SPARKLINE -> PipSparkline(history, modifier = Modifier.padding(top = 6.dp))
      // DIGITAL は上の数値だけで完結する（ゲージを含む構成の中に混ざった場合）。
      GaugeStyle.DIGITAL -> Unit
    }
  }
}

@Composable
private fun PipText(text: String, color: androidx.compose.ui.graphics.Color) {
  Text(
      text,
      fontSize = 15.sp,
      color = color,
      maxLines = 1,
      overflow = TextOverflow.Ellipsis,
  )
}
