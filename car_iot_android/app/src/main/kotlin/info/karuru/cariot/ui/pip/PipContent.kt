package info.karuru.cariot.ui.pip

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
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

// PiP(ピクチャーインピクチャー)ウィンドウ専用の簡易表示。小さいウィンドウで見えれば
// いいだけなので、選択項目のラベル+値を縦に並べるだけに留める（ゲージ描画等は行わない）。
@Composable
fun PipContent(metrics: List<ObdMetric>) {
  val reading by CarIotState.obdReading.collectAsStateWithLifecycle()

  Surface(modifier = Modifier.fillMaxSize()) {
    Column(modifier = Modifier.padding(12.dp)) {
      metrics.forEach { metric ->
        val meta = obdMetricMeta.getValue(metric)
        val value = reading?.let { meta.valueOf(it) }
        val valueText = value?.let {
          val formatted = "%.${meta.decimals}f".format(it)
          if (meta.unit.isEmpty()) formatted else "$formatted ${meta.unit}"
        } ?: "—"

        Row(
            modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
          Text(meta.label, fontSize = 16.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
          Text(valueText, fontSize = 16.sp, color = MaterialTheme.colorScheme.primary)
        }
      }
    }
  }
}
