import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import BottleMusic

// Reference layout: art and metadata at the left, audio-clock lyrics at the
// right. The content stays in place while the full-screen chrome fades away.
Item {
    id: root
    objectName: "immersivePlayerPage"
    property Window win
    property bool queueOpen: false
    property bool controlsAwake: true
    property point lastPointer: Qt.point(-1, -1)
    readonly property bool playbackActive: music.playing
    readonly property string trackId: music.current.hash || ""
    onPlaybackActiveChanged: wakeControls()
    onTrackIdChanged: wakeControls()
    readonly property bool isFullscreen: win && win.immersive
    readonly property bool controlsVisible: !isFullscreen || !music.playing || controlsAwake || queueOpen
    readonly property var composition: Theme.immersiveLayout(width,height,titleMetrics.height,artistMetrics.height)
    readonly property real artSize: composition.s
    readonly property real artX: composition.groupX
    FontMetrics { id: titleMetrics; font: trackTitle.font }
    FontMetrics { id: artistMetrics; font: trackArtist.font }
    signal close()
    signal fullscreen()
    signal toggleQueue()
    signal openSettings()
    focus: true
    function wakeControls() { controlsAwake = true; if (idle) idle.restart() }
    onIsFullscreenChanged: wakeControls()
    onQueueOpenChanged: if (queueOpen) wakeControls()
    Keys.onPressed: wakeControls()
    HoverHandler {
        onPointChanged: {
            const p = point.scenePosition
            if (p.x !== root.lastPointer.x || p.y !== root.lastPointer.y) {
                root.lastPointer = Qt.point(p.x, p.y)
                root.wakeControls()
            }
        }
    }
    Timer {
        id: idle; objectName: "immersiveIdleTimer"; interval: 2800
        onTriggered: {
            if (!dock.interacting && !root.queueOpen)
                root.controlsAwake = false
            else restart()
        }
    }
    ImmersiveBackground {
        anchors.fill: parent
        coverUrl: music.current.image || ""
        animate: music.playing && !music.reducedMotion && root.win && root.win.active && root.win.visibility !== Window.Minimized
    }
    Item {
        id: artwork
        objectName: "immersiveArtwork"
        x: root.artX; y: root.composition.artTop
        width: root.artSize; height: width
        Cover {
            anchors.fill: parent; radius: Theme.rCard
            visible: !music.vinyl
            url: music.current.image || ""; title: ""; pixels: 800
            color: Theme.wallSurface; glyphColor: Theme.wallMuted; glyphScale: 0.24
        }
        Item {
            anchors.fill: parent; visible: music.vinyl
            Rectangle { anchors.fill: parent; radius: width / 2; color: Theme.wallBg }
            Repeater {
                model: 6
                Rectangle {
                    required property int index
                    width: artwork.width * (0.90 - index * 0.055); height: width
                    anchors.centerIn: parent; radius: width / 2; color: "transparent"
                    border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: Theme.wallBorder
                }
            }
            Cover {
                id: recordLabel
                width: parent.width * 0.60; height: width; anchors.centerIn: parent
                radius: width / 2; pixels: 400; url: music.current.image || ""
                color: Theme.wallSurface; glyphColor: Theme.wallMuted; glyphScale: 0.24
                RotationAnimator {
                    target: recordLabel; from: 0; to: 360; duration: 22000; loops: Animation.Infinite
                    running: music.vinyl
                    paused: running && (!music.playing || music.reducedMotion || !root.win || !root.win.active)
                }
            }
            Rectangle { anchors.centerIn: parent; width: 14; height: 14; radius: 7; color: Theme.wallSurface; border.color: Theme.wallBorderStrong }
        }
    }
    Column {
        x: root.artX; y: artwork.y + artwork.height + 30
        width: artwork.width; spacing: 9
        Text {
            id: trackTitle; width: parent.width; text: music.current.title || "未播放歌曲"; textFormat: Text.PlainText
            elide: Text.ElideRight; color: Theme.white
            font.family: Theme.fontUi; font.pixelSize: Theme.fsXl
            font.weight: Font.DemiBold; renderType: Text.NativeRendering
        }
        Text {
            id: trackArtist; width: parent.width; text: music.current.artist || ""; textFormat: Text.PlainText
            elide: Text.ElideRight; color: Theme.wallInkSecondary
            font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; renderType: Text.NativeRendering
        }
        Text {
            visible: music.preview; text: "试听音源"; color: Theme.wallInkSecondary
            font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
        }
    }
    LyricView {
        objectName: "immersiveLyricView"
        x: root.composition.lyricX; y: root.composition.stageTop
        width: root.composition.lyricW; height: root.composition.stageH
        leadingInset: 0; trailingInset: 0; focusY: root.composition.focusY
        immersive: true
        textSize: Theme.lyricPixels(music.lyricSize)
    }
    component ChromeButton: IconButton {
        id: button
        tint: Theme.wallInk; hoverTint: Theme.white; checkedTint: Theme.white
        hoverFill: Theme.wallChromeHover; glyphLift: 0; radius: width / 2
        background: Rectangle {
            radius: button.radius
            color: button.down ? Theme.wallChromePressed : button.hovered || button.visualFocus ? Theme.wallChromeHover : "transparent"
            border.pixelAligned: false; antialiasing: true; border.width: button.visualFocus ? Theme.hairline : 0; border.color: Theme.wallFocus
        }
    }
    Item {
        id: header
        objectName: "immersiveHeader"
        width: parent.width; height: 56
        opacity: root.controlsVisible ? 1 : 0; enabled: root.controlsVisible
        Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
        HoverHandler { id: headerHover }
        MouseArea { anchors.fill: parent; enabled: !root.isFullscreen; onPressed: root.win.startSystemMove() }
        Row {
            x: 20; anchors.verticalCenter: parent.verticalCenter; spacing: 8
            ChromeButton {
                objectName: "immersiveCloseButton"; glyph: "chevron-down"
                Accessible.name: "收起播放页"; onClicked: root.close()
                ToolTip.visible: hovered; ToolTip.text: "收起 (Esc)"
            }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            ChromeButton { glyph: "settings"; Accessible.name: "播放外观设置"; onClicked: root.openSettings() }
            ChromeButton {
                objectName: "immersiveFullscreenButton"; glyph: "maximize-2"
                Accessible.name: root.isFullscreen ? "退出页面全屏" : "进入页面全屏"
                onClicked: root.fullscreen()
                ToolTip.visible: hovered; ToolTip.text: "全屏 (F11)"
            }
            ChromeButton { visible: !root.isFullscreen; glyph: "minus"; Accessible.name: "最小化"; onClicked: root.win.showMinimized() }
            ChromeButton { visible: !root.isFullscreen; glyph: "x"; Accessible.name: "关闭窗口"; onClicked: root.win.close() }
        }
    }
    ImmersiveControls {
        id: dock
        anchors.bottom: parent.bottom; width: parent.width
        queueOpen: root.queueOpen
        opacity: root.controlsVisible ? 1 : 0; enabled: root.controlsVisible
        Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
        HoverHandler { id: dockHover }
        onToggleQueue: { root.wakeControls(); root.toggleQueue() }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: music.duration > 0 ? parent.width * music.position / music.duration : 0
        height: 2; color: Theme.wallAccent; visible: !root.controlsVisible
    }
    ChromeButton {
        objectName: "immersiveRevealButton"
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 22
        glyph: "mic-vocal"; visible: !root.controlsVisible
        Accessible.name: "显示播放控制"; onClicked: root.wakeControls()
    }
    Component.onCompleted: wakeControls()
}
