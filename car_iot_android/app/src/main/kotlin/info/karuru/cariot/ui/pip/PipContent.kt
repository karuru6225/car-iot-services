package info.karuru.cariot.ui.pip

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
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
import info.karuru.cariot.obd.ObdMetric
import info.karuru.cariot.obd.obdMetricMeta
import info.karuru.cariot.state.CarIotState

// PiP(ピクチャーインピクチャー)ウィンドウ専用の簡易表示。小さいウィンドウで見えれば
// いいだけなので、選択項目のラベル+値を縦に並べるだけに留める（ゲージ描画等は行わない）。
//
// 実機検証(2026/08)で最終行が下端で見切れていた。ウィンドウの高さは
// MainActivity側が渡すアスペクト比で決まるが、端末やシステムの都合で必ずしも
// 要求どおりのサイズにならないため、コンテンツ側も与えられた高さに追従させる:
//   ・各行をweight(1f)で等分し、行数が増えても全行が必ず収まるようにする
//   ・行内は縦中央揃え。1行に収まらない場合は省略記号にして折り返さない
@Composable
fun PipContent(metrics: List<ObdMetric>) {
  val reading by CarIotState.obdReading.collectAsStateWithLifecycle()

  Surface(modifier = Modifier.fillMaxSize()) {
    Column(
        modifier = Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.SpaceEvenly,
    ) {
      metrics.forEach { metric ->
        val meta = obdMetricMeta.getValue(metric)
        val value = reading?.let { meta.valueOf(it) }
        val valueText = value?.let { "%.${meta.decimals}f".format(it) }
            ?: buildString {
              append("--")
              if (meta.decimals > 0) {
                append('.')
                repeat(meta.decimals) { append('-') }
              }
            }

        Row(
            modifier = Modifier.fillMaxWidth().weight(1f),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
          PipCell(meta.label, MaterialTheme.colorScheme.onSurfaceVariant)
          // 値はアクセント色ではなく前景色。他画面と同じ扱いに揃えるのと、
          // アクセント色を小さな文字に使うとテーマによってはWCAGの4.5:1を満たせないため
          // （小さいPiPウィンドウでこそ読めないと困る）。
          PipCell(
              if (meta.unit.isEmpty()) valueText else "$valueText ${meta.unit}",
              MaterialTheme.colorScheme.onSurface,
          )
        }
      }
    }
  }
}

@Composable
private fun RowScope.PipCell(text: String, color: androidx.compose.ui.graphics.Color) {
  Text(
      text,
      fontSize = 15.sp,
      color = color,
      maxLines = 1,
      overflow = TextOverflow.Ellipsis,
  )
}
