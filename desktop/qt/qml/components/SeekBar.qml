import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

// Seek bar geometry: fs-xs tabular times (min 4ch, centered), 16px hit row
// with a 3px pill track, accent fill, 12px thumb that only shows on hover.
RowLayout {
    id: root
    spacing: Theme.s2
    property bool disabledLook: music.duration <= 0
    property bool showTimes: true
    HoverHandler { id: hover }

    component Time: Text {
        color: Theme.text3
        font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
        font.features: { "tnum": 1 }
        horizontalAlignment: Text.AlignHCenter
        Layout.minimumWidth: 4 * zero.advanceWidth
        TextMetrics { id: zero; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; text: "0" }
    }

    Time { visible: root.showTimes; text: Theme.time(music.position) }
    Slider {
        id: seek; objectName: "seekSlider"; Layout.fillWidth: true
        implicitHeight: Theme.s4; padding: 0
        from: 0; to: Math.max(1, music.duration); value: music.position
        enabled: music.duration > 0; onMoved: music.seek(value)
        opacity: root.disabledLook ? 0.4 : 1
        Accessible.name: "播放进度"
        background: Item {
            x: seek.leftPadding; width: seek.availableWidth; height: seek.height
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width; height: 3; radius: Theme.rFull; color: Theme.progressTrack
                Rectangle { width: seek.visualPosition * parent.width; height: parent.height; radius: Theme.rFull; color: Theme.accent }
            }
        }
        handle: Rectangle {
            x: seek.leftPadding + seek.visualPosition * seek.availableWidth - width / 2
            y: seek.topPadding + seek.availableHeight / 2 - height / 2
            width: Theme.s3; height: Theme.s3; radius: Theme.rFull; color: Theme.accent
            visible: !root.disabledLook
            scale: hover.hovered || seek.pressed || seek.visualFocus ? 1 : 0

            layer.enabled: Theme.hasShadow(1)
            layer.effect: ShadowEffect { level: 1 }
            PixelSnap.fit: true
        }
    }
    Time { visible: root.showTimes; text: Theme.time(music.duration) }
}
