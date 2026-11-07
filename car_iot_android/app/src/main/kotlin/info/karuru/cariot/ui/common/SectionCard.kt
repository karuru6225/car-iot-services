package info.karuru.cariot.ui.common

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

// 設定系タブ（接続・データ）のセクション枠。見出しは字間を開けた極小テキストで、
// 計器盤のパネル区画名に似せている。
@Composable
fun SectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
  OutlinedCard(
      modifier = Modifier
          .fillMaxWidth()
          .padding(vertical = 8.dp),
  ) {
    Column(modifier = Modifier.padding(16.dp)) {
      // 見出しはアクセント色ではなく前景色。小さな文字(11sp)にアクセント色を使うと
      // テーマによってWCAGの4.5:1を満たせず、実際の計器も区画名は白で刻印されていて
      // 色が乗るのは針の方であるため。
      Text(
          title,
          style = MaterialTheme.typography.titleMedium,
          color = MaterialTheme.colorScheme.onSurface,
      )
      Spacer(Modifier.height(12.dp))
      content()
    }
  }
}
