import QtQuick
import BottleMusic

Item {
    id: root
    signal openImmersive()
    Cover {
        id: art; anchors.fill: parent
        radius: music.vinyl ? width / 2 : Theme.rCard
        // Fixed decode size: resizing never starts another cover request.
        url: music.current.image || ""; title: ""; pixels: 400; glyphScale: 0.3
        RotationAnimator { id: spin; target: art; from: 0; to: 360; duration: 12000; loops: Animation.Infinite }
        Rectangle {
            anchors.centerIn: parent; visible: music.vinyl
            width: parent.width * 0.3; height: width; radius: width / 2
            color: "#171c18"; border.color: "#e5eee6"; border.width: Theme.hairline
            Rectangle { anchors.centerIn: parent; width: parent.width * 0.16; height: width; radius: width / 2; color: Theme.bg }
        }
    }
    Item {
        anchors.fill: parent; visible: Theme.rules && !music.vinyl
        Rectangle { width: parent.width; height: Theme.hairline; color: Theme.ruleInk }
        Rectangle { y: parent.height - Theme.hairline; width: parent.width; height: Theme.hairline; color: Theme.ruleInk }
        Rectangle { width: Theme.hairline; height: parent.height; color: Theme.ruleInk }
        Rectangle { x: parent.width - Theme.hairline; width: Theme.hairline; height: parent.height; color: Theme.ruleInk }
    }
    Item {
        id: corners; anchors.fill: parent; anchors.margins: -10; visible: !music.vinyl
        Repeater {
            model: 4
            Item {
                required property int index
                readonly property bool rightSide: index % 2 === 1
                readonly property bool bottomSide: index >= 2
                x: rightSide ? corners.width - 16 : 0; y: bottomSide ? corners.height - 16 : 0
                width: 16; height: 16
                Rectangle { width: 16; height: Theme.hairline; y: bottomSide ? 16 - height : 0; color: Theme.ruleInk }
                Rectangle { width: Theme.hairline; height: 16; x: rightSide ? 16 - width : 0; color: Theme.ruleInk }
            }
        }
    }
    MouseArea { anchors.fill: parent; onDoubleClicked: root.openImmersive() }
    function updateRotation() {
        if (music.vinyl && music.playing && !music.reducedMotion && visible) {
            if (spin.paused) spin.resume()
            else if (!spin.running) spin.start()
        } else if (spin.running && !spin.paused) spin.pause()
    }
    onVisibleChanged: updateRotation()
    Connections {
        target: music
        function onPlaybackChanged() { root.updateRotation() }
        function onAppearanceChanged() { root.updateRotation() }
    }
    Component.onCompleted: updateRotation()
}
