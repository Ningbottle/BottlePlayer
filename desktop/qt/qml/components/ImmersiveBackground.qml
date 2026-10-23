import QtQuick
import BottleMusic

// A static, theme-owned dark surface keeps lyrics legible while playing.
Rectangle {
    objectName: "immersiveBackground"
    property string coverUrl: ""
    property bool animate: false
    readonly property bool flowing: false
    color: Theme.wallBg
}
