import QtQuick
import BottleMusic

// The masthead's double rule: a thick ink bar, a 3px gap, then the usual
// hairline — the classic newspaper folio divider. Both edges land on whole
// device pixels via snapped rectangles (aliased quads smear at 125% DPI).
Column {
    property color ink: Theme.ruleInk
    property real thick: Math.max(Theme.hairline * 2, Math.round(2 * Theme.dpr) / Theme.dpr)
    spacing: Math.max(Theme.hairline * 2, Math.round(3 * Theme.dpr) / Theme.dpr)
    Rectangle { width: parent.width; height: parent.thick; color: parent.ink }
    Rectangle { width: parent.width; height: Theme.hairline; color: parent.ink }
}
