import QtQuick
import BottleMusic

// Text set in Blink line boxes. Qt's FixedHeight lines put the baseline one
// ascent below the line top and leave all the extra space underneath; CSS
// splits it (half-leading above, floored, with the ascent and descent rounded
// to device pixels). The padding pair moves every line down to Blink's
// baseline while the height stays lines × lineBox. Set `lineBox` explicitly
// when the font is sized in points.
Text {
    id: root
    property real lineBox: font.pixelSize * Theme.lhNormal
    // FixedHeight cannot shrink a line below the font's natural height (big
    // display faces: Caslon's line is 1.24em, so a 1.1 line box never
    // applies). `clamp` forces the box to lines × lineBox anyway; the glyphs
    // overflow the box the same way they overflow Blink's line boxes.
    property bool clamp: false
    // `minBox` floors the clamped box (CSS min-height on a line-box block,
    // e.g. .stage-cue's 6rem); the text stays top-aligned inside it.
    property real minBox: 0
    readonly property real lead: Theme.lineBaseline(metrics.ascent, metrics.descent, lineBox) - metrics.ascent
    // Display sizes draw from outlines: hinted native glyphs lose the thin
    // curves of serif CJK at heading sizes.
    renderType: font.pixelSize >= 28 ? Text.CurveRendering : Text.NativeRendering
    lineHeightMode: Text.FixedHeight
    lineHeight: lineBox
    topPadding: lead
    bottomPadding: -lead
    height: clamp ? Math.max(lineBox * lineCount, minBox) : implicitHeight
    FontMetrics { id: metrics; font: root.font }
}
