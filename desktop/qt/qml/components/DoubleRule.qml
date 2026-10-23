import QtQuick
import BottleMusic

// Two half-pixel rules retain the newspaper masthead without a heavy frame.
Column {
    property color ink: Theme.ruleInk
    property real thick: Theme.hairline
    spacing: Math.max(Theme.hairline * 2, Math.round(3 * Theme.dpr) / Theme.dpr)
    Rectangle { antialiasing: true; width: parent.width; height: parent.thick; color: parent.ink }
    Rectangle { antialiasing: true; width: parent.width; height: Theme.hairline; color: parent.ink }
}
