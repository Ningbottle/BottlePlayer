import QtQuick
import QtQuick.Controls
import BottleMusic

// Scrollbar geometry: a 4px rounded thumb with no track. It stays
// transparent until the scroll area is hovered (--border-strong) and turns
// --text-3 under the pointer.
ScrollBar {
    id: bar
    property bool areaHovered: false
    property color thumbColor: Theme.borderStrong
    property color thumbHoverColor: Theme.text3
    padding: 0
    minimumSize: 0.08
    implicitWidth: orientation === Qt.Vertical ? Theme.scrollBarW : 0
    implicitHeight: orientation === Qt.Horizontal ? Theme.scrollBarW : 0
    background: null
    contentItem: Rectangle {
        implicitWidth: Theme.scrollBarW; implicitHeight: Theme.scrollBarW
        radius: Theme.scrollBarW / 2
        visible: bar.size < 1
        color: bar.hovered || bar.pressed ? bar.thumbHoverColor
            : bar.areaHovered || bar.active ? bar.thumbColor : Theme.clear(bar.thumbColor)

    }
}
