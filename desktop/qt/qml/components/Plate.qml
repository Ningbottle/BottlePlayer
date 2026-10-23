import QtQuick
import BottleMusic

// Elevated surface: token radius/color plus the shadow ladder
// (elevation 1/2/3 maps to --shadow-1/--shadow-2/--shadow-pop).
// Only the face rectangle is layered for the shadow; children sit on top
// unlayered so their text keeps native glyph rendering instead of being
// rasterized into the effect texture (which blurred it).
Item {
    id: root
    property int elevation: 1
    property alias color: face.color
    property alias radius: face.radius
    property color borderColor: "transparent"
    property real borderWidth: Theme.hairline
    default property alias content: holder.data

    Rectangle {
        id: face
        anchors.fill: parent
        border.color: root.borderColor
        border.pixelAligned: false; antialiasing: true; border.width: root.borderColor.a > 0 ? root.borderWidth : 0
        layer.enabled: root.elevation > 0 && Theme.hasShadow(root.elevation)
        layer.effect: ShadowEffect { level: root.elevation }
    }
    Item { id: holder; anchors.fill: parent }
}
