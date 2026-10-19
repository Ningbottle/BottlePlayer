import QtQuick
import BottleMusic

// Newspaper halftone: a screen of tiny ink dots laid over artwork at low
// alpha — the press's photo dot. renderTarget Image paints once into a
// texture; the Cover's own mask clips it to the rounded frame. clipRadius
// punches the disc-shaped screen the hero's vinyl mark uses.
Canvas {
    id: root
    property color ink: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.5)
    property real pitch: 2.6      // logical px between dot centers
    property real dot: 0.55       // dot radius, logical px
    // >0 keeps only dots inside the centered circle (the vinyl disc).
    property real clipRadius: 0
    opacity: 0.2
    renderTarget: Canvas.Image
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onInkChanged: requestPaint()
    Component.onCompleted: requestPaint()
    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = ink
        var cx = width / 2, cy = height / 2, r2 = clipRadius * clipRadius
        for (var y = pitch / 2; y < height; y += pitch) {
            for (var x = pitch / 2; x < width; x += pitch) {
                if (clipRadius > 0) {
                    var dx = x - cx, dy = y - cy
                    if (dx * dx + dy * dy > r2) continue
                }
                ctx.beginPath()
                ctx.arc(x, y, dot, 0, Math.PI * 2)
                ctx.fill()
            }
        }
    }
}
