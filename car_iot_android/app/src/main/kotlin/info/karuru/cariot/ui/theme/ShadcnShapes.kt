package info.karuru.cariot.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Shapes
import androidx.compose.ui.unit.dp

// shadcn/ui の角丸スケール。原典は --radius: 0.625rem(=10px)を基準に
//   --radius-sm: calc(var(--radius) - 4px) = 6px
//   --radius-md: calc(var(--radius) - 2px) = 8px
//   --radius-lg: var(--radius)             = 10px
//   --radius-xl: calc(var(--radius) + 4px) = 14px
// と派生させる方式。dp と px を 1:1 とみなして写している。
//
// Material3 の既定は Button が CircleShape(完全なピル型)で、これが「Material3
// そのままの見た目」の最大の要因になっている。shadcn のボタンは rounded-md(8px)
// なので、Shapes を差し替えるだけでは足りず Button 側に shape を明示的に渡す必要が
// ある(ShadcnButtonShape を各ボタンに指定している)。
val ShadcnShapes: Shapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp),
    small = RoundedCornerShape(8.dp),
    medium = RoundedCornerShape(10.dp),
    large = RoundedCornerShape(14.dp),
    extraLarge = RoundedCornerShape(14.dp),
)

// Button/OutlinedButton に明示的に渡すための shadcn 既定のボタン角丸(rounded-md)。
val ShadcnButtonShape = RoundedCornerShape(8.dp)
