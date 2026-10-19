import QtQuick
import QtQuick.Controls
import BottleMusic

Item {
    id: root
    objectName: "queuePanel"
    property bool wall: false
    property bool cardShown: true
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
    readonly property var shown: {
        // Stable per-row keys (hash + occurrence) let a recreated delegate
        // find where it used to sit — the FLIP animation's memory.
        var seen = {}
        return music.queue.map((track, index) => {
            var h = String(track.hash || track.url || index)
            var occ = seen[h] = (seen[h] || 0) + 1
            return Object.assign({queueIndex: index, key: h + "#" + occ}, track)
        }).filter(track =>
            (String(track.title || "") + " " + String(track.artist || "")).toLowerCase().includes(search.text.toLowerCase()))
    }
    MouseArea { anchors.fill: parent; onClicked: root.close() }
    Plate {
        id: panel; objectName: "queuePanelCard"; anchors.right: parent.right; anchors.rightMargin: 12; y: 12
        // Springs up from the queue button corner; plays out before unload.
        transformOrigin: Item.BottomRight
        opacity: 0; scale: 0.94
        transform: Translate { id: drift; y: 32 }
        Component.onCompleted: if (root.cardShown) panelIn.start()
        Connections { target: root; function onCardShownChanged() { root.cardShown ? panelIn.restart() : panelOut.restart() } }
        ParallelAnimation {
            id: panelOut
            NumberAnimation { target: panel; property: "opacity"; to: 0; duration: music.reducedMotion ? 0 : Theme.durQueueOut; easing.type: Easing.InCubic }
            NumberAnimation { target: panel; property: "scale"; to: 0.96; duration: music.reducedMotion ? 0 : Theme.durQueueOut; easing.type: Easing.InCubic }
            NumberAnimation { target: drift; property: "y"; to: 24; duration: music.reducedMotion ? 0 : Theme.durQueueOut; easing.type: Easing.InCubic }
        }
        ParallelAnimation {
            id: panelIn
            NumberAnimation { target: panel; property: "opacity"; to: 1; duration: music.reducedMotion ? 0 : Theme.durQueueIn * 0.6; easing.type: Easing.OutCubic }
            NumberAnimation { target: panel; property: "scale"; to: 1; duration: music.reducedMotion ? 0 : Theme.durQueueIn; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
            NumberAnimation { target: drift; property: "y"; to: 0; duration: music.reducedMotion ? 0 : Theme.durQueueIn; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
        }
        width: Math.min(root.preferredWidth, parent.width - 24); height: parent.height - 24
        radius: root.wall ? 16 : Theme.rLg; color: root.wall ? Theme.wallChromeSurface : Theme.bgElevated; borderColor: root.wall ? "transparent" : Theme.borderStrong; elevation: root.wall ? 3 : 0
        MouseArea { anchors.fill: parent }
        Column {
            id: header; x: 20; y: 20; width: parent.width - 40; spacing: 12
            Item {
                width: parent.width; height: queueTitle.height
                CssText {
                    id: queueTitle; anchors.verticalCenter: parent.verticalCenter
                    text: root.wall ? "播放队列 · " + music.queue.length : "当前播放队列"
                    font.family: root.wall ? Theme.fontUi : Theme.fontSerif
                    font.pixelSize: root.wall ? 14 : Theme.fsLg
                    font.weight: root.wall ? Theme.fwSemiBold : Theme.fwRegular; color: root.ink
                }
                CssText {
                    visible: !root.wall; anchors.baseline: queueTitle.baseline; anchors.left: queueTitle.right; anchors.leftMargin: Theme.s2
                    text: "Queue · " + music.queue.length; color: Theme.text3
                    font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.letterSpacing: Theme.metaTracking; font.capitalization: Theme.metaCase
                }
                QueueIcon { id: close; objectName: "queueClose"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; glyph: "x"; size: "sm"; onClicked: root.close() }
            }
            Hairline { visible: Theme.rules; width: parent.width }
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
            // A 4px gutter reads each row as its own card.
            spacing: Theme.s1
            SnapSettle { flickable: tracks }
            ScrollBar.vertical: ThinScrollBar { thumbColor: root.wall ? "#3dffffff" : Theme.borderStrong; thumbHoverColor: root.muted }
            // FLIP: queue is a QVariantList — every edit resets the view, so
            // row positions are snapshotted before the mutation and each
            // recreated delegate springs back from where it used to sit.
            property var preMoveYs: ({})
            property double preMoveStamp: 0
            function capturePositions() {
                var map = {}
                for (var i = 0; i < count; ++i) {
                    var it = itemAtIndex(i)
                    if (it) map[it.rowKey] = it.y
                }
                preMoveYs = map
                preMoveStamp = Date.now()
            }
            delegate: AbstractButton {
                id: row
                required property var modelData
                readonly property string rowKey: modelData.key || ""
                width: ListView.view.width; height: Theme.compactList ? 52 : 64; hoverEnabled: true; enabled: !music.radio
                objectName: "queueTrack_" + modelData.queueIndex
                onClicked: music.playQueue(modelData.queueIndex)
                function dismiss() {
                    if (music.reducedMotion) { music.removeQueue(modelData.queueIndex); return }
                    tracks.capturePositions()
                    exitOut.restart()
                }
                Component.onCompleted: {
                    if (music.reducedMotion || Date.now() - tracks.preMoveStamp > 600) return
                    var old = tracks.preMoveYs[rowKey]
                    if (old !== undefined) {
                        delete tracks.preMoveYs[rowKey]
                        var dy = old - y
                        if (Math.abs(dy) > 1) {
                            // Slid out of place; fly back with a card tilt.
                            cell.y += dy
                            cell.rotation = Theme.clamp(-2.4, -dy * 0.025, 2.4)
                            flipBack.restart()
                        }
                    } else {
                        // A newly added row rises and settles.
                        cell.opacity = 0; cell.y += Theme.s2; cell.rotation = 1.2
                        flipBack.restart()
                    }
                }
                ParallelAnimation {
                    id: flipBack
                    NumberAnimation { target: cell; property: "y"; to: 0; duration: 280; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSnap }
                    NumberAnimation { target: cell; property: "rotation"; to: 0; duration: 280; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSnap }
                    NumberAnimation { target: cell; property: "opacity"; to: 1; duration: 160 }
                }
                SequentialAnimation {
                    id: exitOut
                    ParallelAnimation {
                        NumberAnimation { target: cell; property: "opacity"; to: 0; duration: 150 }
                        NumberAnimation { target: cell; property: "scale"; to: 0.94; duration: 150 }
                        NumberAnimation { target: cell; property: "rotation"; to: -2.5; duration: 150 }
                    }
                    ScriptAction { script: music.removeQueue(row.modelData.queueIndex) }
                }
                background: Rectangle {
                    radius: root.wall ? 8 : Theme.rMd
                    color: row.modelData.queueIndex === music.queueIndex ? (root.wall ? Theme.wallChromePressed : Theme.sealSoft) : row.hovered ? (root.wall ? Theme.wallChromeHover : Theme.surfaceHover) : root.wall ? Theme.wallSoftBg : Theme.surface
                    border.width: root.wall || row.modelData.queueIndex === music.queueIndex ? 0 : Theme.hairline
                    border.color: Theme.border
                }
                contentItem: Item {
                    id: cell
                    Cover { x: 10; anchors.verticalCenter: parent.verticalCenter; width: Theme.compactList ? 40 : 44; height: width; radius: Theme.rSm; url: row.modelData.image || ""; pixels: 80 }
                    Column {
                        x: 10 + (Theme.compactList ? 40 : 44) + Theme.s3; anchors.verticalCenter: parent.verticalCenter; width: parent.width - x - 122; spacing: 4
                        CssText { width: parent.width; text: row.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: root.wall ? Theme.fontUi : Theme.fontTitle; font.pixelSize: Theme.fsMd; color: root.wall ? root.ink : row.modelData.queueIndex === music.queueIndex ? Theme.seal : Theme.text }
                        CssText { width: parent.width; text: row.modelData.artist; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; color: root.muted }
                    }
                    Row {
                        anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; spacing: 0
                        QueueIcon { size: "sm"; glyph: "arrow-up"; enabled: row.modelData.queueIndex > 0; Accessible.name: "向前移动"; onClicked: { tracks.capturePositions(); music.moveQueue(row.modelData.queueIndex, row.modelData.queueIndex - 1) } }
                        QueueIcon { size: "sm"; glyph: "arrow-down"; enabled: row.modelData.queueIndex < music.queue.length - 1; Accessible.name: "向后移动"; onClicked: { tracks.capturePositions(); music.moveQueue(row.modelData.queueIndex, row.modelData.queueIndex + 1) } }
                        QueueIcon { size: "sm"; glyph: "list-plus"; Accessible.name: "添加到歌单"; onClicked: root.addToPlaylist(row.modelData) }
                        QueueIcon { objectName: "queueRemove_" + row.modelData.queueIndex; size: "sm"; glyph: "x"; Accessible.name: "移除歌曲"; onClicked: row.dismiss() }
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
