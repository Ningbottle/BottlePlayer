import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

// Current page-view card grid, with the requested artwork left / lyrics right.
Item {
    id: root
    objectName: "playerPage"
    property real viewportWidth: width
    property real viewportHeight: height
    property bool queueOpen: false
    property bool editing: false
    property bool shelfOpen: false
    property bool toolsAwake: true
    property string notice: ""
    property point lastPointerScenePosition: Qt.point(-1, -1)
    readonly property var layout: playerLayoutState
    readonly property var cards: layout.cards
    readonly property var cardIds: ["lyrics", "cover", "info", "progress", "quality"]
    readonly property var cardNames: ({lyrics: "歌词", cover: "封面", info: "歌曲信息", progress: "进度与时间", quality: "音质信息"})
    readonly property bool hasTrack: music.queueIndex >= 0
    // Shared-element flight: while the bar cover travels here the real
    // artwork stays hidden; coverSlot is its destination rect in root coords.
    property bool hideCover: false
    readonly property bool coverReady: cover.ready
    function coverSlotRect() {
        if (!hasTrack) return Qt.rect(0, 0, 0, 0)
        const p = cover.mapToItem(root, 0, 0)
        return Qt.rect(p.x, p.y, cover.width, cover.height)
    }
    readonly property bool narrow: viewportWidth <= 900
    readonly property real gridGap: narrow ? Theme.s5 : Theme.s3
    readonly property var nextTrack: music.queueIndex + 1 < music.queue.length ? music.queue[music.queueIndex + 1] : null
    signal back()
    signal openHome()
    signal openSearch()
    signal openImmersive()
    signal toggleQueue()

    function touches(a,b) { return a && b && a.y < b.y+b.h && b.y < a.y+a.h }
    readonly property bool lyricsRight: !!cards.cover && !!cards.lyrics && cards.cover.x+cards.cover.w === cards.lyrics.x && touches(cards.cover,cards.lyrics)
    readonly property bool lyricsLeft: !!cards.cover && !!cards.lyrics && cards.lyrics.x+cards.lyrics.w === cards.cover.x && touches(cards.cover,cards.lyrics)
    readonly property bool lyricsAdjacent: lyricsRight || lyricsLeft
    readonly property bool infoAligned: !!cards.cover && !!cards.info && cards.info.x === cards.cover.x && cards.info.w === cards.cover.w
    readonly property bool infoBelow: infoAligned && cards.cover.y+cards.cover.h === cards.info.y
    readonly property bool infoAbove: infoAligned && cards.info.y+cards.info.h === cards.cover.y

    function wakeTools() { toolsAwake = true; toolsIdle.restart() }
    function pointerMoved(position) {
        const local = mapFromItem(null, position.x, position.y)
        if (local.x < 0 || local.y < 0 || local.x > width || local.y > height) return
        if (Math.abs(position.x - lastPointerScenePosition.x) <= 0.1
            && Math.abs(position.y - lastPointerScenePosition.y) <= 0.1) return
        lastPointerScenePosition = position
        wakeTools()
    }
    function startEditing() { shelfOpen = false; editing = true; wakeTools() }
    function finishEditing() { editing = false; editor.cancelDrag(); wakeTools() }
    onShelfOpenChanged: if (!shelfOpen) {
        shelf.focus = false
        forceActiveFocus()
        wakeTools()
    }
    function showNotice(text) { notice = text; clearNotice.restart() }
    Timer { id: toolsIdle; objectName: "playerToolsIdle"; interval: 3000; onTriggered: root.toolsAwake = false }
    Timer { id: clearNotice; interval: 2400; onTriggered: root.notice = "" }
    // Hover state also changes when a button fades out under a stationary
    // pointer. Only a new scene position should restart the idle timer.
    HoverHandler {
        onPointChanged: if (hovered) root.pointerMoved(point.scenePosition)
        onHoveredChanged: if (hovered) root.pointerMoved(point.scenePosition)
    }
    Component.onCompleted: wakeTools()

    Column {
        objectName: "playerEmptyState"; visible: !root.hasTrack
        x: Theme.s8; width: Math.max(0, parent.width - Theme.s8 * 2)
        anchors.verticalCenter: parent.verticalCenter; spacing: Theme.s4
        CssText { text: "正在播放"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic; font.letterSpacing: Theme.metaTracking; lineBox: Theme.fsXs * Theme.lhNormal }
        CssText {
            width: Math.min(parent.width, 22 * Theme.fs2xl * 0.55); wrapMode: Text.Wrap
            text: "选择一首歌，歌词会在这里展开"; color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: Theme.fs2xl; font.weight: Font.DemiBold
            lineBox: Theme.fs2xl * Theme.lhTight
        }
        CssText { text: "从首页的每日推荐或搜索结果开始播放。"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd }
        Row {
            spacing: Theme.s2
            ActionButton { objectName: "playerEmptyHome"; text: "回到首页"; glyph: "house"; filled: true; onClicked: root.openHome() }
            ActionButton { objectName: "playerEmptySearch"; text: "搜索歌曲"; glyph: "search"; onClicked: root.openSearch() }
        }
    }

    Item {
        id: grid; objectName: "playerCardGrid"; visible: root.hasTrack
        x: root.narrow ? Theme.s4 : Theme.s8; y: Theme.s4
        width: parent.width - x * 2
        height: Math.max(0, parent.height - y - Theme.controlSm - Theme.s3 * 2)
        readonly property real cellWidth: Math.max(0, (width - root.gridGap * 11) / 12)
        readonly property real cellHeight: Math.max(0, (height - root.gridGap * 5) / 6)

        component Card: Item {
            property string cardId
            readonly property var rect: root.cards[cardId] || {x:0, y:0, w:0, h:0}
            x: rect.x * (grid.cellWidth + root.gridGap); y: rect.y * (grid.cellHeight + root.gridGap)
            width: Math.max(0, rect.w * grid.cellWidth + (rect.w - 1) * root.gridGap)
            height: Math.max(0, rect.h * grid.cellHeight + (rect.h - 1) * root.gridGap)
            visible: !!root.cards[cardId]; opacity: root.editing ? 0.62 : 1
            Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase } }
        }
        Card {
            id: artCard; cardId: "cover"; objectName: "playerCoverCard"
            PlayerArtwork {
                id: cover; objectName: "playerCoverArt"
                x: !lyricView.centered && root.lyricsRight ? parent.width-10-width : !lyricView.centered && root.lyricsLeft ? 10 : (parent.width-width)/2
                y: root.infoBelow ? parent.height-10-height : root.infoAbove ? 10 : (parent.height-height)/2
                width: Math.max(0, Math.min(parent.width, parent.height) - 20); height: width
                enabled: !root.editing; onOpenImmersive: root.openImmersive()
                opacity: root.hideCover ? 0 : 1
            }
        }
        Card {
            id: infoCard; cardId: "info"; objectName: "playerInfoCard"; clip: true
            readonly property bool compact: height <= 72
            readonly property real titleSize: height <= 64 ? Theme.playerTitleCompactSize : width <= 220 ? Theme.fsLg : Theme.playerTitleSize
            Column {
                x: root.infoBelow || root.infoAbove ? cover.x-10 : 0
                width: root.infoBelow || root.infoAbove ? cover.width+20 : parent.width; anchors.verticalCenter: parent.verticalCenter; spacing: 0
                CssText {
                    width: parent.width; visible: infoCard.height >= 82
                    text: "曲目 " + String(music.queueIndex + 1).padStart(2, "0") + " / " + String(music.queue.length).padStart(2, "0")
                    color: Theme.text3; elide: Text.ElideRight
                    font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic
                    font.letterSpacing: Theme.metaTracking; lineBox: Theme.fsXs * Theme.lhNormal
                }
                Item {
                    width: parent.width; height: title.height + (infoCard.height <= 64 ? Theme.s1 : Theme.s1 * 2)
                    CssText {
                        id: title; objectName: "playerTrackTitle"
                        y: infoCard.height <= 64 ? 0 : Theme.s1; width: parent.width
                        text: music.current.title || ""; color: Theme.text; wrapMode: Text.WrapAnywhere
                        maximumLineCount: infoCard.compact ? 1 : 2; elide: Text.ElideRight
                        font.family: Theme.fontDisplay; font.pixelSize: infoCard.titleSize; font.weight: Theme.fwDisplay
                        font.letterSpacing: Theme.displayTracking * infoCard.titleSize; lineBox: infoCard.titleSize * Theme.lhTight
                    }
                }
                CssText { visible: !infoCard.compact; width: parent.width; text: music.current.artist || ""; color: Theme.text2; elide: Text.ElideRight; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd }
                CssText {
                    objectName: "playerAlbum"; visible: !infoCard.compact
                    width: parent.width; text: music.current.album || "未知专辑"; color: Theme.text3; elide: Text.ElideRight
                    font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic; lineBox: Theme.fsXs * Theme.lhNormal
                }
                RowLayout {
                    objectName: "playerCompactMetadata"; visible: infoCard.compact
                    width: parent.width; spacing: Theme.s2
                    CssText {
                        Layout.maximumWidth: infoCard.width * 0.48; Layout.alignment: Qt.AlignVCenter
                        text: music.current.artist || ""; color: Theme.text2; elide: Text.ElideRight
                        font.family: Theme.fontUi; font.pixelSize: infoCard.height <= 64 ? Theme.fsXs : Theme.fsMd
                    }
                    Text { text: "·"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; Layout.alignment: Qt.AlignVCenter }
                    CssText {
                        objectName: "playerCompactAlbum"; Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter
                        text: music.current.album || "未知专辑"; color: Theme.text3; elide: Text.ElideRight
                        font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic
                        lineBox: Theme.fsXs * Theme.lhNormal
                    }
                }
                Item {
                    visible: !!root.nextTrack && infoCard.height > 150
                    width: parent.width; height: Theme.s4 + Theme.s3 + Theme.fsSm * Theme.lhNormal
                    Hairline { y: Theme.s4; width: parent.width; color: Theme.border }
                    RowLayout {
                        y: Theme.s4 + Theme.s3; width: parent.width; spacing: Theme.s2
                        Text { text: "下一首"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic }
                        Text { Layout.fillWidth: true; text: root.nextTrack ? root.nextTrack.title : ""; elide: Text.ElideRight; color: Theme.text; font.family: Theme.fontTitle; font.pixelSize: Theme.fsSm; font.weight: Theme.fwMedium }
                        Text { Layout.maximumWidth: infoCard.width * 0.3; text: root.nextTrack ? root.nextTrack.artist : ""; elide: Text.ElideRight; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
                    }
                }
            }
        }
        Card {
            id: lyricCard; cardId: "lyrics"; objectName: "playerLyricCard"
            LyricView {
                id: lyricView; anchors.fill: parent; enabled: !root.editing; viewportHeight: root.viewportHeight
                leadingInset: centered ? Theme.lyricFreeInset(width) : root.lyricsRight ? Theme.lyricGapInset(cover.width,root.gridGap) : -1
                trailingInset: centered ? Theme.lyricFreeInset(width) : root.lyricsLeft ? Theme.lyricGapInset(cover.width,root.gridGap) : -1
                alignTrailing: !centered && root.lyricsLeft
                focusY: root.lyricsAdjacent ? Theme.clamp(0.30*listHeight,artCard.y+cover.y+cover.height/2-lyricCard.y,0.62*listHeight) : -1
                centered: lyricCard.rect.w >= 8 && Math.abs(lyricCard.rect.x + lyricCard.rect.w / 2 - 6) <= 1
            }
        }
        Card {
            id: progressCard; cardId: "progress"; objectName: "playerProgressCard"; clip: true
            Column {
                width: parent.width; anchors.verticalCenter: parent.verticalCenter; spacing: parent.height <= 72 ? Theme.s1 : Theme.s2
                Row {
                    spacing: Theme.s2
                    CssText { id: clock; text: Theme.time(music.position); color: Theme.text; font.family: Theme.fontMono; font.pixelSize: progressCard.height <= 72 || progressCard.width <= 200 ? Theme.fsLg : Theme.fs2xl; lineBox: font.pixelSize * Theme.lhTight; font.features: { "tnum": 1 } }
                    CssText { text: "/ " + Theme.time(music.duration); color: Theme.text3; font.family: Theme.fontMono; font.pixelSize: Theme.fsXs; font.features: { "tnum": 1 }; anchors.baseline: clock.baseline }
                }
                SeekBar { width: parent.width; showTimes: false; enabled: !root.editing }
            }
        }
        Card {
            id: qualityCard; cardId: "quality"; objectName: "playerQualityCard"; clip: true
            readonly property bool switchable: music.ready && music.queueIndex >= 0 && !String(music.current.hash).startsWith("file:")
            Column {
                width: parent.width; anchors.verticalCenter: parent.verticalCenter; spacing: Theme.s1
                CssText { visible: qualityCard.height > 110; text: qualityCard.switchable ? "音质 · 点击切换" : "音质"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic }
                CssText {
                    width: parent.width; text: music.qualityText(music.queueIndex < 0 ? music.quality : music.actualQuality); color: Theme.text; elide: Text.ElideRight
                    font.family: Theme.fontDisplay; font.pixelSize: qualityCard.height <= 64 || qualityCard.width <= 160 ? Theme.fsLg : qualityCard.height <= 110 ? Theme.fsXl : Theme.fs2xl
                    lineBox: font.pixelSize * Theme.lhTight
                }
                CssText { text: music.preview ? "当前为试听音源" : music.actualQuality === "local" ? "本地音源" : music.actualQuality === "128" || music.actualQuality === "320" ? music.actualQuality + " kbps" : music.actualQuality === "unknown" ? "等待音源信息" : "由当前音源确认"; color: Theme.text2; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic }
            }
            MouseArea {
                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                enabled: qualityCard.switchable && !root.editing
                onClicked: playerQualityMenu.popup()
            }
            QualityMenu { id: playerQualityMenu; objectName: "playerQualityMenu" }
        }
        Text {
            objectName: "playerGridEmpty"; visible: !Object.keys(root.cards).length
            anchors.centerIn: parent; width: Math.min(parent.width, 400); wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
            text: root.editing ? "页面已清空，点下方的卡片名称把它们加回来" : "页面是空的，点右下角「编辑布局」添加卡片"
            color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd
        }
        PlayerLayoutEditor {
            id: editor; objectName: "playerLayoutEditor"; anchors.fill: parent
            visible: root.editing; enabled: visible
            layout: root.layout; gap: root.gridGap; cellWidth: grid.cellWidth; cellHeight: grid.cellHeight; cardNames: root.cardNames
            onDone: root.finishEditing(); onRejected: root.showNotice("空间不足，无法放置卡片")
        }
    }

    FocusScope {
        id: toolbar; objectName: "playerToolbar"; visible: root.hasTrack
        x: grid.x; y: parent.height - Theme.s2 - height
        width: grid.width; height: Theme.controlSm
        opacity: !root.shelfOpen && (root.editing || root.toolsAwake || activeFocus) ? 1 : 0; enabled: opacity > 0
        Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase } }
        Row {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; spacing: Theme.s2; visible: !root.editing
            ActionButton { objectName: "playerFullscreenButton"; size: "sm"; text: "页面全屏"; glyph: "maximize-2"; onClicked: root.openImmersive() }
            ActionButton { objectName: "playerShelfButton"; size: "sm"; text: "封面架"; glyph: "gallery-horizontal-end"; onClicked: { root.shelfOpen = true; shelf.forceActiveFocus() } }
            ActionButton { objectName: "playerQueueButton"; size: "sm"; text: "队列"; glyph: "list-music"; active: root.queueOpen; onClicked: root.toggleQueue() }
        }
        ActionButton { objectName: "playerEditLayout"; size: "sm"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; visible: !root.editing; text: "编辑布局"; glyph: "layout-dashboard"; restFill: Theme.surfaceHover; onClicked: root.startEditing() }
        Row {
            id: additions; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; spacing: Theme.s2; visible: root.editing
            Text { text: "编辑布局"; height: Theme.controlSm; verticalAlignment: Text.AlignVCenter; color: Theme.text; font.family: Theme.fontTitle; font.pixelSize: Theme.fsSm; font.weight: Font.DemiBold }
            Repeater {
                model: root.cardIds.filter(id => !root.cards[id])
                ActionButton {
                    required property string modelData; objectName: "playerAdd_" + modelData
                    text: root.cardNames[modelData]; glyph: "plus"; size: "sm"
                    onClicked: if (!root.layout.add(modelData)) root.showNotice("空间不足，无法添加卡片")
                }
            }
        }
        Row {
            id: doneActions; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: Theme.s2; visible: root.editing
            ActionButton { objectName: "playerResetLayout"; text: "恢复默认"; glyph: "rotate-ccw"; size: "sm"; quiet: true; onClicked: { editor.cancelDrag(); root.layout.reset() } }
            ActionButton { objectName: "playerDoneLayout"; text: "完成"; glyph: "check"; size: "sm"; filled: true; onClicked: root.finishEditing() }
        }
        Text {
            x: additions.width + Theme.s2; width: Math.max(0, toolbar.width - additions.width - doneActions.width - Theme.s2 * 2); height: toolbar.height; visible: root.editing
            text: root.notice || "拖动移动 · 拖右下角缩放 · Esc 完成"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            color: root.notice ? Theme.danger : Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic
        }
    }
    PlayerCoverShelf {
        id: shelf; objectName: "playerCoverShelf"
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        viewportHeight: root.viewportHeight; visible: root.hasTrack && root.shelfOpen
        onClose: { root.shelfOpen = false; root.wakeTools() }
    }
}
