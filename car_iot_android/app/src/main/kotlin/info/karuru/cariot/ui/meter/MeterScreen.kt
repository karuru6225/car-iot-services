package info.karuru.cariot.ui.meter

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.meter.MeterConfigStore
import info.karuru.cariot.meter.MeterSlot
import info.karuru.cariot.obd.GaugeStyle
import info.karuru.cariot.obd.ObdReading
import info.karuru.cariot.obd.obdMetricMeta
import info.karuru.cariot.state.CarIotState

// 「メーター」タブ。mobile/lib/screens/meter_screen.dartを移植。
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
            .padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.End,
    ) {
      TextButton(onClick = { showSettings = true }) { Text("項目を編集") }
    }
    LazyVerticalGrid(
        columns = GridCells.Fixed(2),
        modifier = Modifier.padding(horizontal = 16.dp),
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
  val valueText = value?.let {
    val formatted = "%.${meta.decimals}f".format(it)
    if (meta.unit.isEmpty()) formatted else "$formatted ${meta.unit}"
  } ?: "—"

  Column(modifier = Modifier.padding(8.dp)) {
    Text(meta.label, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
    when (slot.style) {
      GaugeStyle.CIRCULAR -> CircularGauge(value ?: meta.min, meta.min, meta.max, valueText)
      GaugeStyle.DIGITAL -> DigitalGauge(valueText)
      GaugeStyle.BAR -> BarGauge(value ?: meta.min, meta.min, meta.max, valueText)
      GaugeStyle.SPARKLINE -> SparklineGauge(history, meta.min, meta.max, valueText)
    }
  }
}
