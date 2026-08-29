package info.karuru.cariot.ui.meter

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.meter.MeterConfigStore
import info.karuru.cariot.meter.MeterSlot
import info.karuru.cariot.obd.GaugeStyle
import info.karuru.cariot.obd.ObdReading
import info.karuru.cariot.obd.obdMetricMeta
import info.karuru.cariot.state.CarIotState

// 「メーター」タブ。mobile/lib/screens/meter_screen.dartを移植。
// デザインレビュー(2026/08)を反映: 「項目を編集」だけが浮いていたヘッダーに見出しを追加して
// 他タブのセクション見出しと揃え、各タイルをCardで囲んで情報密度を上げた。
@Composable
fun MeterScreen() {
  val context = LocalContext.current
  val configStore = remember { MeterConfigStore(context) }
  var slots by remember { mutableStateOf(configStore.load()) }
  var showSettings by remember { mutableStateOf(false) }
  val reading by CarIotState.obdReading.collectAsStateWithLifecycle()
  val history by CarIotState.obdHistory.collectAsStateWithLifecycle()

  Column {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
      Text(
          "メーター",
          style = MaterialTheme.typography.titleMedium,
          color = MaterialTheme.colorScheme.primary,
      )
      TextButton(onClick = { showSettings = true }) { Text("項目を編集") }
    }
    LazyVerticalGrid(
        columns = GridCells.Fixed(2),
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
      items(slots) { slot -> MeterTile(slot, reading, history[slot.metric] ?: emptyList()) }
    }
  }

  if (showSettings) {
    MeterSettingsSheet(
        slots = slots,
        onDismiss = { showSettings = false },
        onSave = { newSlots ->
          slots = newSlots
          configStore.save(newSlots)
          showSettings = false
        },
    )
  }
}

@Composable
private fun MeterTile(slot: MeterSlot, reading: ObdReading?, history: List<Float>) {
  val meta = obdMetricMeta.getValue(slot.metric)
  val value = reading?.let { meta.valueOf(it) }
  // 単位はゲージ側で小さく描くため数値と分けて渡す。値が無いときは書式どおりの
  // 桁プレースホルダー（BatteryScreenと同じ作法）。
  val valueText = value?.let { "%.${meta.decimals}f".format(it) }
      ?: buildString {
        append("--")
        if (meta.decimals > 0) {
          append('.')
          repeat(meta.decimals) { append('-') }
        }
      }

  Card(modifier = Modifier.fillMaxWidth().heightIn(min = 132.dp)) {
    Column(modifier = Modifier.padding(16.dp)) {
      Text(
          meta.label,
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
          modifier = Modifier.padding(bottom = 10.dp),
      )
      when (slot.style) {
        GaugeStyle.CIRCULAR -> CircularGauge(value, meta.min, meta.max, valueText, meta.unit, meta.decimals)
        GaugeStyle.DIGITAL -> DigitalGauge(valueText, meta.unit)
        GaugeStyle.BAR -> BarGauge(value, meta.min, meta.max, valueText, meta.unit)
        GaugeStyle.SPARKLINE -> SparklineGauge(history, meta.min, meta.max, valueText, meta.unit)
      }
    }
  }
}
