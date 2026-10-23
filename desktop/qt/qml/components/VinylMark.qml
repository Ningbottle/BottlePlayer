import QtQuick
import BottleMusic

// The hero's corner ornament: the paper's vinyl print — a halftone-screened
// disc, concentric hairline grooves, the cinnabar label and the paper hole.
// Drawn oversized at the plate's lower-right edge; the plate clips it.
Item {
    id: root
    property real size: 168
    implicitWidth: size; implicitHeight: size

    Halftone {
        anchors.centerIn: parent; width: root.size * 0.78; height: width
        clipRadius: width / 2; opacity: 0.5; pitch: 2.2; dot: 0.5
    }
    // Groove rings — 0.6em hairlines at 68/55/42 of the 200-unit disc.
    Repeater {
        model: [0.68, 0.55, 0.42]
        Rectangle {
            required property real modelData
            anchors.centerIn: parent
            width: root.size * modelData; height: width; radius: width / 2
            color: "transparent"
            border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline
            border.color: Theme.borderStrong
        }
    }
    // The record label: seal red with the paper hole at center.
    Rectangle {
        anchors.centerIn: parent; width: root.size * 0.2; height: width; radius: width / 2
        color: Theme.seal
    }
    Rectangle {
        anchors.centerIn: parent; width: Math.max(Theme.s1, root.size * 0.03); height: width; radius: width / 2
        color: Theme.surface
    }
}
