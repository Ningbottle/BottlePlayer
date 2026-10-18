import QtQuick
import QtQuick.Controls
import BottleMusic

Item {
    id: root
    objectName: "queuePanel"
    property bool wall: false
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
    readonly property var shown: music.queue.map((track, index) => Object.assign({queueIndex:index}, track)).filter(track =>
        (String(track.title || "") + " " + String(track.artist || "")).toLowerCase().includes(search.text.toLowerCase()))
    MouseArea { anchors.fill: parent; onClicked: root.close() }
    Plate {
        id: panel; objectName: "queuePanelCard"; anchors.right: parent.right; anchors.rightMargin: 12; y: 12
        opacity: 0; scale: 0.97
        Component.onCompleted: panelIn.start()
        ParallelAnimation {
            id: panelIn
            NumberAnimation { target: panel; property: "opacity"; to: 1; duration: music.reducedMotion ? 0 : Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeExpand }
            NumberAnimation { target: panel; property: "scale"; to: 1; duration: music.reducedMotion ? 0 : Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeExpand }
        }
        width: Math.min(440, parent.width - 24); height: parent.height - 24
        radius: root.wall ? 16 : Theme.rLg; color: root.wall ? Theme.wallChromeSurface : Theme.bgElevated; borderColor: root.wall ? "transparent" : Theme.borderStrong; elevation: root.wall ? 3 : 0
        MouseArea { anchors.fill: parent }
        Column {
            id: header; x: 20; y: 20; width: parent.width - 40; spacing: 12
            Row {
                width: parent.width
                CssText { width: parent.width - close.width; text: "播放队列 · " + music.queue.length; font.family: root.wall ? Theme.fontUi : Theme.fontTitle; font.pixelSize: root.wall ? 14 : Theme.fsXl; font.weight: root.wall ? Theme.fwSemiBold : Theme.fwRegular; color: root.ink }
                QueueIcon { id: close; objectName: "queueClose"; glyph: "x"; size: "sm"; onClicked: root.close() }
            }
            TextField {
                id: search; objectName: "queueSearch"; width: parent.width
                placeholderText: "搜索队列中的歌曲或艺人"; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
                height: 36; color: root.ink; placeholderTextColor: root.muted; selectByMouse: true
                selectionColor: root.wall ? "#3dffffff" : Theme.accent; selectedTextColor: root.wall ? root.ink : Theme.accentContrast
                background: Rectangle { color: root.wall ? Theme.wallChromeHover : Theme.surface; border.color: Theme.border; border.width: root.wall ? 0 : Theme.hairline; radius: root.wall ? height/2 : Theme.rSm }
            }
            Row {
                spacing: 8
                ActionButton { objectName: "queueClear"; quiet: root.wall; restInk: root.wall ? root.muted : Theme.text; hoverInk: root.ink; hoverFill: root.wall ? Theme.wallChromeHover : Theme.surfaceHover; downFill: root.wall ? Theme.wallChromePressed : Theme.surfaceActive; edgeColor: root.wall ? "transparent" : Theme.border; focusInk: root.wall ? Theme.wallFocus : Theme.focus; bgRadius: root.wall ? height/2 : Theme.rSm; size: "sm"; text: "清空队列"; glyph: "trash-2"; enabled: music.queue.length > 0 && !music.radio; onClicked: confirmClear.open() }
                Text { anchors.verticalCenter: parent.verticalCenter; text: music.radio ? "红心推荐会自动补充歌曲" : "保留顺序与播放位置"; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; color: root.muted }
            }
        }
        ListView {
            id: tracks; objectName: "queueTracks"; x: 12; y: header.y + header.height + 12; width: parent.width - 24; height: Math.max(0, parent.height - y - 12)
            model: root.shown; clip: true; cacheBuffer: 160; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThinScrollBar { thumbColor: root.wall ? "#3dffffff" : Theme.borderStrong; thumbHoverColor: root.muted }
            delegate: AbstractButton {
                id: row
                required property var modelData
                width: ListView.view.width; height: Theme.compactList ? 60 : 76; hoverEnabled: true; enabled: !music.radio
                objectName: "queueTrack_" + modelData.queueIndex
                onClicked: music.playQueue(modelData.queueIndex)
                background: Rectangle { radius: root.wall ? 8 : Theme.rSm; color: row.modelData.queueIndex === music.queueIndex ? (root.wall ? Theme.wallChromePressed : Theme.accentSoft) : row.hovered ? (root.wall ? Theme.wallChromeHover : Theme.surfaceHover) : "transparent" }
                contentItem: Item {
                    Cover { x: 8; anchors.verticalCenter: parent.verticalCenter; width: 36; height: 36; url: row.modelData.image || ""; pixels: 80 }
                    Column {
                        x: 56; anchors.verticalCenter: parent.verticalCenter; width: parent.width - x - 126; spacing: 4
                        Text { width: parent.width; text: row.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: root.wall ? Theme.fontUi : Theme.fontTitle; font.pixelSize: Theme.fsMd; color: root.wall ? root.ink : row.modelData.queueIndex === music.queueIndex ? Theme.accent : Theme.text }
                        Text { width: parent.width; text: row.modelData.artist; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; color: root.muted }
                    }
                    Row {
                        anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; spacing: 0
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
