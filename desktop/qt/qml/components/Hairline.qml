import QtQuick
import BottleMusic

// A shared half-pixel separator. Text snapping is independent of its weight.
Rectangle {
    antialiasing: true
    property bool vertical: false
    implicitWidth: vertical ? Theme.hairline : 0
    implicitHeight: vertical ? 0 : Theme.hairline
    color: Theme.border
}
