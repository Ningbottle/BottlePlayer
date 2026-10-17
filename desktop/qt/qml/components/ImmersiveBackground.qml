import QtQuick
import BottleMusic

Item {
    id: root
    objectName: "immersiveBackground"
    property string coverUrl: ""
    property bool animate: false
    readonly property bool flowing: flow.running && !flow.paused
    ColorExtractor {
        id: palette
        objectName: "immersivePalette"
        imageSource: Theme.artwork(root.coverUrl, 400)
    }
    function coverColor(index, fallback) {
        var colors = palette.dominantColors
        return colors.length ? colors[index % colors.length] : fallback
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: background.color1 }
            GradientStop { position: 1; color: background.color2 }
        }
    }
    ShaderEffect {
        id: background
        anchors.fill: parent
        property real elapsed: 0
        property vector2d resolution: Qt.vector2d(width, height)
        property color color1: root.coverColor(0, "#596b8a")
        property color color2: root.coverColor(1, "#89616f")
        property color color3: root.coverColor(2, "#857969")
        Behavior on color1 { ColorAnimation { duration: music.reducedMotion ? 0 : 1000 } }
        Behavior on color2 { ColorAnimation { duration: music.reducedMotion ? 0 : 1000 } }
        Behavior on color3 { ColorAnimation { duration: music.reducedMotion ? 0 : 1000 } }
        fragmentShader: "qrc:/bottle/shaders/immersive-fluid.frag.qsb"
        // Pausing preserves the phase instead of restarting the background.
        NumberAnimation {
            id: flow; target: background; property: "elapsed"
            from: 0; to: 3600; duration: 3600000; loops: Animation.Infinite
            running: true; paused: !root.animate
        }
    }
    Rectangle { anchors.fill: parent; color: "#180a1020" }
}
