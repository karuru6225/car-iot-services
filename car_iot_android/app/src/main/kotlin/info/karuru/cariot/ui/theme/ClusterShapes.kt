package info.karuru.cariot.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Shapes
import androidx.compose.ui.unit.dp

// ベントーグリッドのタイルは面として大きめの角丸、操作要素は締まった角丸、という
// 二段構え。Material3既定はボタンが完全なピル型で「Androidの標準UI」に見えるため、
// ボタンには ClusterButtonShape を明示的に渡して外している。
val ClusterShapes: Shapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp),
    small = RoundedCornerShape(10.dp),
    medium = RoundedCornerShape(18.dp),
    large = RoundedCornerShape(22.dp),
    extraLarge = RoundedCornerShape(28.dp),
)

val ClusterButtonShape = RoundedCornerShape(12.dp)
