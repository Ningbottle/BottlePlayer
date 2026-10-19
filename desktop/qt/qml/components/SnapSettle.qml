import QtQuick
import BottleMusic

// Rests a Flickable on whole device pixels once user motion ends: a
// fractional contentY leaves every glyph half a device pixel off at 125%
// scaling — Blink snaps the same way when it paints scrolled text. Wheel
// scrolling handled by SmoothWheel already lands snapped; this covers
// drags, flicks and flickables without a wheel handler. Ignored while the
// view overshoots its bounds so rubber-banding stays free.
QtObject {
    id: root
    required property Flickable flickable
    property bool horizontal: false
    readonly property Connections settle: Connections {
        target: root.flickable
        function onMovementEnded() {
            const f = root.flickable
            const loY = f.originY, hiY = loY + f.contentHeight - f.height
            if (f.contentY >= loY - 0.5 && f.contentY <= hiY + 0.5)
                f.contentY = Theme.snap(f.contentY)
            if (root.horizontal) {
                const loX = f.originX, hiX = loX + f.contentWidth - f.width
                if (f.contentX >= loX - 0.5 && f.contentX <= hiX + 0.5)
                    f.contentX = Theme.snap(f.contentX)
            }
        }
    }
}
