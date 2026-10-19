import QtQuick
import QtQuick.Controls
import BottleMusic

// Compact queue card (web reference: 390px floating card, 64px rows with
// 44px artwork, accent bar on the current track). It springs up from the
// queue button corner and animates out before the loader releases it.
Item {
    id: root
    objectName: "queuePanel"
    property bool wall: false
    property bool shown: true
    property bool scrim: true
    property real preferredWidth: 390
    readonly property color ink: wall ? Theme.wallChromeInk : Theme.text
    readonly property color muted: wall ? Theme.wallChromeMuted : Theme.text3
    readonly property real cardWidth: panel.width
    signal close()
    signal addToPlaylist(var track)
    component QueueIcon: IconButton {
        tint: root.wall ? root.muted : Theme.text2; hoverTint: root.ink
        hoverFill: root.wall ? Theme.wallChromeHover : Theme.surfaceHover; downFill: root.wall ? Theme.wallChromePressed : Theme.surfaceActive
        focusInk: root.wall ? Theme.wallFocus : Theme.focus; radius: root.wall ? width/2 : Theme.rSm
    }
    readonly property var rows: music.queue.map((track, index) => Object.assign({queueIndex:index}, track)).filter(track =>
        (String(track.title || "") + " " + String(track.artist || "")).toLowerCase().includes(search.text.toLowerCase()))
    onShownChanged: shown ? panelIn.restart() : panelOut.restart()
    MouseArea { anchors.fill: parent; enabled: root.scrim && root.shown; onClicked: root.close() }
    Plate {
        id: panel; objectName: "queuePanelCard"
        anchors.right: parent.right; anchors.rightMargin: 12; y: 12
        width: Math.min(root.preferredWidth, parent.width - 24); height: parent.height - 24
        radius: root.wall ? 16 : Theme.rLg; color: root.wall ? Theme.wallChromeSurface : Theme.bgElevated
        borderColor: root.wall ? "transparent" : Theme.borderStrong; elevation: root.wall ? 3 : 2
        transformOrigin: Item.BottomRight
        opacity: 0; scale: 0.94
        transform: Translate { id: drift; y: 32 }
        Component.onCompleted: if (root.shown) panelIn.start()
        ParallelAnimation {
            id: panelIn
            NumberAnimation { target: panel; property: "opacity"; to: 1; duration: music.reducedMotion ? 0 : Theme.durQueueIn * 0.6; easing.type: Easing.OutCubic }
            NumberAnimation { target: panel; property: "scale"; to: 1; duration: music.reducedMotion ? 0 : Theme.durQueueIn; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
            NumberAnimation { target: drift; property: "y"; to: 0; duration: music.reducedMotion ? 0 : Theme.durQueueIn; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
        }
        ParallelAnimation {
            id: panelOut
            NumberAnimation { target: panel; property: "opacity"; to: 0; duration: music.reducedMotion ? 0 : Theme.durQueueOut; easing.type: Easing.InCubic }
            NumberAnimation { target: panel; property: "scale"; to: 0.96; duration: music.reducedMotion ? 0 : Theme.durQueueOut; easing.type: Easing.InCubic }
            NumberAnimation { target: drift; property: "y"; to: 24; duration: music.reducedMotion ? 0 : Theme.durQueueOut; easing.type: Easing.InCubic }
        }
        MouseArea { anchors.fill: parent }
        Column {
            id: header; x: Theme.s4; y: Theme.s3; width: parent.width - Theme.s4 * 2; spacing: Theme.s2
            Item {
                width: parent.width; height: 32
                Row {
                    anchors.verticalCenter: parent.verticalCenter; spacing: Theme.s2
                    Text {
                        id: title; text: "当前播放队列"; color: root.ink
                        font.family: root.wall ? Theme.fontUi : Theme.fontTitle; font.pixelSize: root.wall ? Theme.fsMd : Theme.fsLg; font.weight: Theme.fwMedium
                    }
                    Text {
                        anchors.baseline: title.baseline; text: music.queue.length + " 首"; color: root.muted
                        font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.features: { "tnum": 1 }
                    }
                }
                Row {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 0
                    QueueIcon { objectName: "queueClear"; size: "sm"; glyph: "trash-2"; enabled: music.queue.length > 0 && !music.radio; Accessible.name: "清空队列"; onClicked: confirmClear.open() }
                    QueueIcon { id: close; objectName: "queueClose"; glyph: "x"; size: "sm"; Accessible.name: "关闭队列"; onClicked: root.close() }
                }
            }
            TextField {
                id: search; objectName: "queueSearch"; width: parent.width
                placeholderText: "搜索队列"; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
                height: 30; leftPadding: Theme.s3; rightPadding: Theme.s3; topPadding: 0; bottomPadding: 0
                color: root.ink; placeholderTextColor: root.muted; selectByMouse: true
                selectionColor: root.wall ? "#3dffffff" : Theme.accent; selectedTextColor: root.wall ? root.ink : Theme.accentContrast
                background: Rectangle { color: root.wall ? Theme.wallChromeHover : Theme.surface; border.color: Theme.border; border.width: root.wall ? 0 : Theme.hairline; radius: root.wall ? height/2 : Theme.rSm }
            }
            Text {
                visible: music.radio; width: parent.width; text: "红心推荐会自动补充歌曲"; color: root.muted
                font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
            }
        }
        Hairline { y: header.y + header.height + Theme.s3 - height; width: parent.width; color: root.wall ? "transparent" : Theme.border }
        ListView {
            id: tracks; objectName: "queueTracks"
            x: Theme.s1; y: header.y + header.height + Theme.s3; width: parent.width - Theme.s1 * 2; height: Math.max(0, parent.height - y - Theme.s2)
            model: root.rows; clip: true; cacheBuffer: 160; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThinScrollBar { thumbColor: root.wall ? "#3dffffff" : Theme.borderStrong; thumbHoverColor: root.muted }
            delegate: AbstractButton {
                id: row
                required property var modelData
                readonly property bool current: modelData.queueIndex === music.queueIndex
                readonly property bool awake: rowHover.hovered || row.activeFocus
                width: ListView.view.width; height: Theme.compactList ? 56 : 64; hoverEnabled: true; enabled: !music.radio
                objectName: "queueTrack_" + modelData.queueIndex
                onClicked: music.playQueue(modelData.queueIndex)
                HoverHandler { id: rowHover }
                background: Rectangle {
                    radius: root.wall ? 8 : Theme.rSm
                    color: row.current ? (root.wall ? Theme.wallChromePressed : Theme.accentSoft)
                        : row.awake ? (root.wall ? Theme.wallChromeHover : Theme.surfaceHover) : Theme.clear(Theme.surfaceHover)
                    Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover } }
                    Rectangle { visible: row.current && !root.wall; x: 0; anchors.verticalCenter: parent.verticalCenter; width: 2; height: parent.height - Theme.s4; radius: 1; color: Theme.accent }
                }
                contentItem: Item {
                    Cover {
                        id: art; x: Theme.s3; anchors.verticalCenter: parent.verticalCenter
                        width: Theme.compactList ? 40 : 44; height: width; url: row.modelData.image || ""; pixels: 96
                    }
                    Column {
                        x: art.x + art.width + Theme.s3; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                        width: parent.width - x - (row.awake ? actions.width + Theme.s1 : Theme.s3)
                        Text {
                            width: parent.width; text: row.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight
                            color: row.current && !root.wall ? Theme.accent : root.ink
                            font.family: root.wall ? Theme.fontUi : Theme.fontTitle; font.pixelSize: Theme.fsMd; font.weight: row.current ? Theme.fwMedium : Theme.fwRegular
                        }
                        Text {
                            width: parent.width; text: row.modelData.artist; textFormat: Text.PlainText; elide: Text.ElideRight
                            color: root.muted; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                        }
                    }
                    Row {
                        id: actions
                        anchors.right: parent.right; anchors.rightMargin: Theme.s1; anchors.verticalCenter: parent.verticalCenter; spacing: 0
                        opacity: row.awake ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover } }
                        QueueIcon { size: "sm"; glyph: "chevron-up"; enabled: row.modelData.queueIndex > 0; Accessible.name: "向前移动"; onClicked: music.moveQueue(row.modelData.queueIndex, row.modelData.queueIndex - 1) }
                        QueueIcon { size: "sm"; glyph: "chevron-down"; enabled: row.modelData.queueIndex < music.queue.length - 1; Accessible.name: "向后移动"; onClicked: music.moveQueue(row.modelData.queueIndex, row.modelData.queueIndex + 1) }
                        QueueIcon { size: "sm"; glyph: "plus"; Accessible.name: "添加到歌单"; onClicked: root.addToPlaylist(row.modelData) }
                        QueueIcon { objectName: "queueRemove_" + row.modelData.queueIndex; size: "sm"; glyph: "x"; Accessible.name: "移除歌曲"; onClicked: music.removeQueue(row.modelData.queueIndex) }
                    }
                }
            }
        }
        ScrollFade { x: tracks.x; y: tracks.y; width: tracks.width; height: tracks.height; flickable: tracks; tint: root.wall ? Theme.wallChromeSurface : Theme.bgElevated }
    }
    ConfirmDialog {
        id: confirmClear; title: "清空播放队列"
        onAccepted: music.clearQueue()
        message: "队列和本次保存的播放位置将被清空。"
    }
}
