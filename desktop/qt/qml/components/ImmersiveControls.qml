import QtQuick
import QtQuick.Controls
import BottleMusic

Item {
    id: root
    objectName: "immersiveControls"
    property bool queueOpen: false
    readonly property bool interacting: progress.pressed || volume.pressed
    signal toggleQueue()
    signal close()
    height: 76
    component LightButton: IconButton {
        id: button
        tint: "#c9ffffff"; hoverTint: "white"; checkedTint: "white"
        glyphSize: 18; glyphLift: 0; radius: width / 2
        background: Rectangle {
            radius: button.radius
            color: button.down || button.checked ? "#30ffffff" : button.hovered || button.visualFocus ? "#18ffffff" : "transparent"
            border.width: button.visualFocus ? 2 : 0; border.color: "#99ffffff"
        }
    }
    Rectangle { anchors.fill: parent; color: "#27ffffff" }
    Slider {
        id: progress
        objectName: "immersiveSeekSlider"
        x: 0; y: -8; width: parent.width; height: 16; padding: 0
        from: 0; to: Math.max(1, music.duration); value: music.position
        enabled: music.duration > 0; onMoved: music.seek(value)
        hoverEnabled: true
        Accessible.name: "播放进度"
        background: Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width; height: progress.hovered || progress.pressed ? 5 : 2
            color: "#36ffffff"
            Rectangle { width: progress.visualPosition * parent.width; height: parent.height; color: "#e6ffffff" }
        }
        handle: Rectangle {
            x: progress.visualPosition * progress.width - width / 2; y: (progress.height - height) / 2
            width: 12; height: 12; radius: 6; color: "white"
            visible: progress.hovered || progress.pressed || progress.visualFocus
        }
        ToolTip.visible: hovered || pressed
        ToolTip.text: Theme.time(value)
    }
    Row {
        x: 28; anchors.verticalCenter: parent.verticalCenter; spacing: 12
        Column {
            width: Math.max(110, Math.min(220, root.width * 0.22)); spacing: 5
            Text {
                width: parent.width; textFormat: Text.PlainText; text: music.current.title || "未播放歌曲"; elide: Text.ElideRight
                font.family: Theme.fontUi; font.pixelSize: 13; font.weight: Font.DemiBold; color: "white"
            }
            Text {
                width: parent.width; textFormat: Text.PlainText; text: music.current.artist || "选一首歌开始"; elide: Text.ElideRight
                font.family: Theme.fontUi; font.pixelSize: 12; color: "#a6ffffff"
            }
        }
        LightButton {
            objectName: "immersiveFavoriteButton"; anchors.verticalCenter: parent.verticalCenter
            glyph: "heart"; glyphFilled: music.favorite; checked: music.favorite
            enabled: music.queueIndex >= 0
            Accessible.name: music.favorite ? "取消收藏" : "收藏"
            onClicked: music.toggleFavorite()
        }
    }
    Row {
        anchors.centerIn: parent; spacing: root.width < 1000 ? 12 : 20
        LightButton {
            objectName: "immersiveLoopButton"; anchors.verticalCenter: parent.verticalCenter
            glyph: music.loopMode === "random" ? "shuffle" : music.loopMode === "single" ? "repeat-1" : "repeat"
            enabled: music.queueIndex >= 0 && !music.radio; checked: music.loopMode !== "list"
            Accessible.name: music.loopMode === "random" ? "随机播放" : music.loopMode === "single" ? "单曲循环" : "列表循环"
            onClicked: music.cycleLoopMode()
        }
        LightButton {
            objectName: "immersivePreviousButton"; anchors.verticalCenter: parent.verticalCenter
            media: "prev"; mediaBox: 40; mediaGlyph: 18; enabled: music.queueIndex >= 0 && !music.radio
            Accessible.name: "上一首"; onClicked: music.previous()
        }
        LightButton {
            objectName: "immersivePlayButton"
            mediaBox: 48; mediaGlyph: 24
            media: music.playing ? "pause" : "play"; tint: "#31384c"; hoverTint: tint
            enabled: !music.loading; Accessible.name: music.playing ? "暂停" : "播放"
            background: Rectangle { radius: width / 2; color: "#dbffffff"; border.width: parent.visualFocus ? 2 : 0; border.color: "white" }
            onClicked: music.toggle()
        }
        LightButton {
            objectName: "immersiveNextButton"; anchors.verticalCenter: parent.verticalCenter
            media: "next"; mediaBox: 40; mediaGlyph: 18; enabled: music.queueIndex >= 0
            Accessible.name: "下一首"; onClicked: music.next()
        }
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 22; anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        Text {
            anchors.verticalCenter: parent.verticalCenter; visible: root.width >= 1050
            text: Theme.time(music.position) + " / " + Theme.time(music.duration)
            font.family: Theme.fontUi; font.pixelSize: 12; font.features: { "tnum": 1 }; color: "#bdffffff"
        }
        LightButton {
            objectName: "immersiveMuteButton"; glyph: music.volume > 0 ? "volume-2" : "volume-x"
            property real restoreVolume: 0.7
            Accessible.name: music.volume > 0 ? "静音" : "取消静音"
            onClicked: {
                if (music.volume > 0) { restoreVolume = music.volume; music.volume = 0 }
                else music.volume = Math.max(0.1, restoreVolume)
            }
        }
        Slider {
            id: volume
            objectName: "immersiveVolumeSlider"
            anchors.verticalCenter: parent.verticalCenter
            width: root.width >= 1400 ? 72 : 48; height: 20; padding: 0
            from: 0; to: 1; value: music.volume; onMoved: music.volume = value
            Accessible.name: "音量"
            background: Rectangle {
                y: (volume.height - height) / 2; width: volume.width; height: 3; radius: 2; color: "#40ffffff"
                Rectangle { width: volume.visualPosition * parent.width; height: parent.height; radius: 2; color: "#c9ffffff" }
            }
            handle: Rectangle {
                x: volume.visualPosition * (volume.width - width); y: (volume.height - height) / 2
                width: 9; height: 9; radius: 5; color: "white"
            }
        }
        LightButton {
            objectName: "immersiveQueueButton"; glyph: "list-music"; checked: root.queueOpen
            Accessible.name: "播放队列"; onClicked: root.toggleQueue()
        }
    }
}
