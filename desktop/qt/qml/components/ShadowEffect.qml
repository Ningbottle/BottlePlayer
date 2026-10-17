import QtQuick
import QtQuick.Effects
import BottleMusic

// layer.effect for theme shadows: 1 --shadow-1, 2 --shadow-2,
// 3 --shadow-pop, 4 --fx-glow.
MultiEffect {
    property int level: 1
    readonly property color tone: level === 4 ? Theme.glowColor : level === 3 ? Theme.shadowPopColor : level === 2 ? Theme.shadow2Color : Theme.shadow1Color
    shadowEnabled: true
    shadowBlur: 1.0
    blurMax: level === 4 ? Theme.glowBlurMax : level === 3 ? Theme.shadowPopBlurMax : level === 2 ? Theme.shadow2BlurMax : Theme.shadow1BlurMax
    // The shader blends shadowColor as if premultiplied: a translucent navy
    // like rgba(20,30,60,.1) added its RGB on top and lit the paper blue
    // instead of darkening it. Opaque RGB plus shadowOpacity is correct
    // either way.
    shadowColor: Qt.rgba(tone.r, tone.g, tone.b, 1)
    shadowOpacity: tone.a
    shadowVerticalOffset: level === 4 ? Theme.glowY : level === 3 ? Theme.shadowPopY : level === 2 ? Theme.shadow2Y : Theme.shadow1Y
}
