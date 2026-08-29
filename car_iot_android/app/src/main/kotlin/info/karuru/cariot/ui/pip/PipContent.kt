package info.karuru.cariot.ui.pip

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.ui.unit.TextUnit
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
import info.karuru.cariot.ui.meter.AutoSizeValueText
import info.karuru.cariot.ui.meter.ValueRail

// PiP(ピクチャーインピクチャー)ウィンドウ専用の表示。
//
// 項目ごとにゲージ種別を選べる（メータータブと同じ MeterSlot を使う）。ただしPiPの
// ウィンドウは小さいので、メータータブと同じ描画をそのまま持ち込むと読めない。
// 種別に応じてレイアウトを変える:
//
//   ・全項目がデジタル数値 … 縦に積む。ラベル左・値右で一覧性が高い
//   ・ゲージを含む        … 横に並べる。ゲージは縦方向に場所を取るため、
//                            狭い高さに複数積むと潰れる
//
// レイアウトが2種類あるぶんウィンドウの縦横比も変える必要があるので、
// MainActivity.pipAspectRatio() が同じ判定を持っている（片方だけ直すとズレる）。
//
// PiPウィンドウはユーザーがピンチで拡大できる。大きさを決めるのはシステムで、アプリが
// 渡せるのは縦横比だけ（最小〜最大の範囲もシステム側が持つ）。文字やゲージを固定サイズで
// 書くと、広げても中身が小さいままで余白だけが増えるため、実寸から倍率を出して全部に掛ける。
@Composable
fun PipContent(slots: List<MeterSlot>) {
  val reading by CarIotState.obdReading.collectAsStateWithLifecycle()
  val history by CarIotState.obdHistory.collectAsStateWithLifecycle()

  Surface(modifier = Modifier.fillMaxSize()) {
    BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
      // 基準はPiPの最小サイズ相当の高さ。ここを1.0倍として、広げた分だけ拡大する。
      // 上限を置いているのは、極端に間延びした字面にならないようにするため。
      val scale = (maxHeight / PIP_BASE_HEIGHT).coerceIn(1f, 2f)

      if (slots.all { it.style == GaugeStyle.DIGITAL }) {
        Column(
            modifier = Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.SpaceEvenly,
        ) {
          slots.forEach { slot ->
            TextRow(slot, reading, scale, modifier = Modifier.weight(1f))
          }
        }
      } else {
        Row(
            modifier = Modifier.fillMaxSize().padding(horizontal = 10.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
          slots.forEach { slot ->
            GaugeCell(
                slot = slot,
                reading = reading,
                history = history[slot.metric].orEmpty(),
                scale = scale,
                modifier = Modifier.weight(1f),
            )
          }
        }
      }
    }
  }
}

// PiPの最小サイズ相当の高さ。実機(Pixel 10a)では最小 284px / density 2.625 ≒ 108dp。
private val PIP_BASE_HEIGHT = 108.dp

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
private fun ColumnScope.TextRow(
    slot: MeterSlot,
    reading: ObdReading?,
    scale: Float,
    modifier: Modifier = Modifier,
) {
  val meta = obdMetricMeta.getValue(slot.metric)
  Row(
      modifier = modifier.fillMaxWidth(),
      horizontalArrangement = Arrangement.SpaceBetween,
      verticalAlignment = Alignment.CenterVertically,
  ) {
    Text(
        meta.label,
        fontSize = (15 * scale).sp,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
    )
    // 単位はラベルと同じ扱い（薄色・小さめ）。数値だけが前景色で立つようにする。
    Row(verticalAlignment = Alignment.Bottom) {
      Text(
          formatValue(meta, reading),
          fontSize = (15 * scale).sp,
          color = MaterialTheme.colorScheme.onSurface,
          maxLines = 1,
      )
      if (meta.unit.isNotEmpty()) {
        UnitText(meta.unit, scale)
      }
    }
  }
}

@Composable
private fun RowScope.GaugeCell(
    slot: MeterSlot,
    reading: ObdReading?,
    history: List<Float>,
    scale: Float,
    modifier: Modifier = Modifier,
) {
  val meta = obdMetricMeta.getValue(slot.metric)
  val value = reading?.let { meta.valueOf(it) }
  val valueText = formatValue(meta, reading)

  // ウィンドウの高さはいちばん背の高いセル（アナログダイヤル）に合わせて確保される。
  // そのため数値だけ・細い線だけのセルは下が大きく余る。種別ごとに余りの使い道を変える。
  //
  // 数値はどの種別でも AutoSizeValueText で出す。桁が枠に収まらないとき既定では黙って
  // 切り落とされ「別の正しそうな値」に見えるため、縮めて全桁を残す（他画面と同じ作法）。
  Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
    Text(
        meta.label,
        fontSize = (9 * scale).sp,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
    )

    when (slot.style) {
      // 数値だけのセルは、隣にゲージが並ぶ中で貧相に見えないよう、ゲージ1つ分の面積を
      // 数値で占める。単位は数値の下へ逃がす——横に並べると数値が幅を分け合うことになり、
      // 自動縮小が効いて結局小さくなるため。
      GaugeStyle.DIGITAL -> Column(
          modifier = Modifier.weight(1f).fillMaxWidth(),
          horizontalAlignment = Alignment.CenterHorizontally,
          verticalArrangement = Arrangement.Center,
      ) {
        AutoSizeValueText(
            text = valueText,
            style = MaterialTheme.typography.displaySmall.copy(fontSize = (48 * scale).sp),
            color = MaterialTheme.colorScheme.onSurface,
            minFontSize = (14 * scale).sp,
            modifier = Modifier.fillMaxWidth(),
        )
        if (meta.unit.isNotEmpty()) {
          UnitText(meta.unit, scale)
        }
      }

      GaugeStyle.CIRCULAR -> {
        PipValue(valueText, meta.unit, (15 * scale).sp, scale)
        AnalogDial(
            value = value,
            min = meta.min,
            max = meta.max,
            decimals = meta.decimals,
            height = 84.dp * scale,
            numeralSize = (8 * scale).sp,
            modifier = Modifier.padding(top = 2.dp),
        )
      }

      // レールは細いので上寄せだと宙に浮く。余りを数値の下に入れて下端付近まで送り、
      // 隣のダイヤルの目盛りと同じくらいの高さに来るようにする。
      GaugeStyle.BAR -> {
        PipValue(valueText, meta.unit, (22 * scale).sp, scale)
        Spacer(modifier = Modifier.weight(1f))
        ValueRail(
            fraction = value?.let { ((it - meta.min) / (meta.max - meta.min)).coerceIn(0f, 1f) },
            modifier = Modifier.padding(bottom = 12.dp * scale),
        )
      }

      GaugeStyle.SPARKLINE -> {
        PipValue(valueText, meta.unit, (15 * scale).sp, scale)
        PipSparkline(
            history = history,
            metricMin = meta.min,
            metricMax = meta.max,
            modifier = Modifier.weight(1f).padding(top = 6.dp),
        )
      }
    }
  }
}

// 数値＋単位。単位はラベルと同じ扱い（薄色・小さめ）にして、数値との落差を保つ。
// 単位まで数値と同じ大きさ・同じ色で出すと、どれが読むべき数字か一瞬迷う。
@Composable
private fun PipValue(valueText: String, unit: String, fontSize: TextUnit, scale: Float) {
  Row(
      modifier = Modifier.fillMaxWidth(),
      horizontalArrangement = Arrangement.Center,
      verticalAlignment = Alignment.Bottom,
  ) {
    AutoSizeValueText(
        text = valueText,
        style = MaterialTheme.typography.displaySmall.copy(fontSize = fontSize),
        color = MaterialTheme.colorScheme.onSurface,
        minFontSize = (11 * scale).sp,
        modifier = Modifier.weight(1f, fill = false),
    )
    if (unit.isNotEmpty()) {
      UnitText(unit, scale)
    }
  }
}

@Composable
private fun UnitText(unit: String, scale: Float) {
  Text(
      unit,
      fontSize = (9 * scale).sp,
      color = MaterialTheme.colorScheme.onSurfaceVariant,
      maxLines = 1,
      modifier = Modifier.padding(start = 3.dp, bottom = 2.dp),
  )
}
