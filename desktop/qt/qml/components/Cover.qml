import QtQuick
import QtQuick.Effects
import BottleMusic

// Artwork tile. Rounded corners clip the image through a MultiEffect mask
// (Item.clip only clips to the bounding box, which left square artwork
// poking out of rounded cards). Missing art shows the title monogram, or the
// Disc3 placeholder the player bar uses.
Item {
    id: root
    property string url: ""
    property string title: ""
    property int pixels: 400
    property real radius: 0
    property color color: Theme.surfaceHover
    property real glyphScale: 0.45
    property color glyphColor: Theme.text3
    readonly property bool ready: picture.status === Image.Ready

    Rectangle { anchors.fill: parent; radius: root.radius; color: root.color; visible: !root.ready }
    Text {
        anchors.centerIn: parent; visible: !root.ready && root.title !== ""
        text: root.title.slice(0, 1)
        font.family: Theme.fontTitle; font.pixelSize: Math.min(root.width, root.height) * 0.28
        font.weight: Theme.fwMedium; color: Theme.text3
    }
    Icon {
        anchors.centerIn: parent; visible: !root.ready && root.title === ""
        name: "disc-3"; color: root.glyphColor; size: Math.round(Math.min(root.width, root.height) * root.glyphScale)
    }
    Image {
        id: picture; anchors.fill: parent
        source: Theme.artwork(root.url, root.pixels)
        asynchronous: true; cache: false
        sourceSize.width: root.pixels; sourceSize.height: root.pixels
        fillMode: Image.PreserveAspectCrop
        visible: root.radius <= 0
    }
    Item {
        id: mask; anchors.fill: parent; visible: false; layer.enabled: root.radius > 0
        Rectangle { anchors.fill: parent; radius: root.radius; color: "black" }
    }
    MultiEffect {
        anchors.fill: parent; visible: root.radius > 0 && root.ready
        source: picture; maskEnabled: true; maskSource: mask
        maskThresholdMin: 0.5; maskSpreadAtMin: 1.0
    }
}
