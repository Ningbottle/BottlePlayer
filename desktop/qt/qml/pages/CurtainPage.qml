import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

// 幕布 stage: Curtain page and hero layout (aurora light, --stage-home
// ladder) — artist → vertical hairline → song → album → lyric cue → round
// bordered play + status → RadioLauncher on the left, the AlbumWall cover
// collage on the right. The stage occupies the viewport; discovery is the
// next section, as in the approved Folia curtain page layout.
// All rem values use 1rem = 14px (theme base font size).
Flickable {
    id: root
    signal openPlaylist(var playlist)
    signal openAlbum(var album)
    objectName: "curtainPage"
    clip: true; contentWidth: width; contentHeight: stage.height + discovery.height
    boundsBehavior: Flickable.StopAtBounds
    readonly property bool compactStage: stage.height < 650
    readonly property real frameInset: Theme.snap(Theme.clamp(24, width * 0.06, 98))
    readonly property real frameWidth: width - frameInset * 2
    readonly property real columnGap: width < 1100 ? 32 : 42
    readonly property real titleSize: width < 1100 ? 34 : width < 1440 ? 48 : 56
    readonly property real cueSize: compactStage ? 24 : width < 1440 ? 32 : 36
    readonly property var activeCue: music.lyricIndex >= 0 ? music.lyrics[music.lyricIndex] || ({}) : ({})
    property int selected: 0
    property var hero: music.hero
    // CurtainHome focusIndex: the wall opens on the hero's print and is
    // re-centred only when the hero changes; browsing is a separate camera.
    readonly property string heroHash: hero.hash || ""
    readonly property int feedCount: music.feed.length
    function heroIndex() {
        for (var i = 0; i < music.feed.length; i++) if (music.feed[i].hash === heroHash) return i
        return 0
    }
    onHeroHashChanged: selected = heroIndex()
    onFeedCountChanged: if (selected >= feedCount) selected = heroIndex()
    Component.onCompleted: selected = heroIndex()
    signal wall()
    signal listen()
    signal player()

    // .poster-wall-entry / .curtain-panel-trigger: stage-card chips with
    // .7rem/.9rem padding inside a 1px (one device pixel) border, .65rem
    // radius, .75rem/1.5 type, 1rem glyphs: 36.95px tall at 125%.
    component StageChip: AbstractButton {
        id: chip
        property string glyph: ""
        property bool elevated: false
        implicitHeight: 40
        leftPadding: Theme.rem(0.9) + Theme.hairline; rightPadding: leftPadding
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.Button
        Accessible.name: text
        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: Theme.rem(0.65); color: Theme.stageCard
                border.width: Theme.hairline; border.color: Theme.stageCardBorder
                layer.enabled: chip.elevated && Theme.hasShadow(1)
                layer.effect: ShadowEffect { level: 1 }
                PixelSnap.fit: true
            }
            Rectangle {
                anchors.fill: parent; radius: Theme.rem(0.65)
                color: chip.hovered ? Theme.surfaceHover : Theme.clear(Theme.surfaceHover)
                Behavior on color { ColorAnimation { duration: 200 } }
                PixelSnap.fit: true
            }
        }
        contentItem: Row {
            spacing: Theme.rem(0.5)
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: chip.glyph; size: Theme.rem(1)
                color: chip.hovered ? Theme.text : Theme.text2
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text; color: chip.hovered ? Theme.text : Theme.text2
                font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75))
                Behavior on color { ColorAnimation { duration: 200 } }
            }
        }
    }
    HoverHandler { id: scrollHover }
    ScrollBar.vertical: ThinScrollBar { areaHovered: scrollHover.hovered }
    SmoothWheel { flickable: root }
    function play(index) {
        if (music.radio) { if (music.feed[index] && music.feed[index].hash === music.current.hash) music.toggle(); return }
        music.playFrom("feed", index)
    }

    Item {
        id: stage; objectName: "curtainStage"; width: root.width
        height: Math.max(480, root.height)
        // FoliaHomeBackdrop: paper, the hero art over the left 88% at
        // --stage-home-art-opacity, then .curtain-wash — a linear fade to
        // paper (transparent 18% → paper 88%) under an accent-soft ellipse
        // at (8%, 28%) that clears at 64% of its farthest-corner size. The
        // wall side ends in plain paper, which its edge fades rely on.
        Rectangle { anchors.fill: parent; color: Theme.bg }
        Image {
            width: parent.width * 0.88; height: parent.height
            source: root.hero.image || ""
            sourceSize.width: 400; sourceSize.height: 400; asynchronous: true; cache: false
            fillMode: Image.PreserveAspectCrop; opacity: Theme.stageArtOpacity
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.18; color: Theme.clear(Theme.bg) }
                GradientStop { position: 0.88; color: Theme.bg }
            }
        }
        Canvas {
            // Keep the wash texture fixed; resize only its GPU transform.
            width: 512; height: 256
            transform: Scale { xScale: stage.width / 512; yScale: stage.height / 256 }
            readonly property color tint: Theme.accentSoft
            onTintChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var cx = width * 0.08, cy = height * 0.28
                var rx = Math.max(cx, width - cx) * Math.SQRT2, ry = Math.max(cy, height - cy) * Math.SQRT2
                ctx.translate(cx, cy)
                ctx.scale(1, ry / rx)
                var g = ctx.createRadialGradient(0, 0, 0, 0, 0, rx)
                g.addColorStop(0, tint)
                g.addColorStop(0.64, Theme.clear(tint))
                ctx.fillStyle = g
                ctx.fillRect(-cx, -cy * rx / ry, width, height * rx / ry)
            }
        }

        // .stage-hero-copy: vertically centered in the 43fr grid column.
        Column {
            id: copy
            z: 2
            objectName: "curtainHeroCopy"
            x: root.frameInset
            width: (root.frameWidth - root.columnGap) * 0.43
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 0
            spacing: 0
            PixelSnap.enabled: true
            readonly property bool empty: !root.hero.hash

            Text {
                readonly property real size: root.width < 1100 ? 16 : 20
                width: parent.width; elide: Text.ElideRight
                text: copy.empty ? "今天想听些什么" : root.hero.artist
                color: Theme.text2
                font.family: Theme.fontUi
                font.pixelSize: size
                font.letterSpacing: 0
                lineHeightMode: Text.FixedHeight; lineHeight: Math.round(size * 1.7)
            }
            // .stage-hairline: 1px vertical rule, text-3 at 45%, .65rem above
            // and 1.25rem below.
            Item { width: 1; height: root.compactStage ? 6 : 9 }
            Hairline {
                vertical: true; color: Theme.text3; opacity: 0.45
                height: root.compactStage ? 21 : 48
            }
            Item { width: 1; height: root.compactStage ? 11 : 18 }
            Row {
                width: parent.width; spacing: Theme.rem(0.3)
                Text {
                    id: stageSong
                    objectName: "curtainTitle"
                    readonly property real size: root.titleSize
                    // .stage-song: a flex item capped at 13ch, so a short
                    // title shrinks to its text and the open-player arrow
                    // follows it.
                    readonly property real chUnit: titleMetrics.advanceWidth("0")
                    width: Math.min(implicitWidth, parent.width - openPlayer.width - parent.spacing, 13 * chUnit)
                    text: copy.empty ? "选一首歌，\n开始聆听" : root.hero.title
                    color: Theme.text; font.family: Theme.fontUi; font.weight: Theme.fwSemiBold
                    font.pixelSize: size
                    font.letterSpacing: 0
                    lineHeightMode: Text.FixedHeight; lineHeight: Math.round(size * 1.25)
                    wrapMode: Text.Wrap; maximumLineCount: root.compactStage ? 2 : 3; elide: Text.ElideRight
                    FontMetrics { id: titleMetrics; font: stageSong.font }
                }
                IconButton {
                    id: openPlayer
                    visible: !copy.empty
                    y: Theme.rem(0.2) // .stage-open-player: margin-top .2rem
                    glyph: "arrow-up-right"; tint: Theme.text2; opacity: hovered ? 1 : 0.45
                    Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
                    Accessible.name: "打开播放页"; onClicked: root.player()
                }
            }
            Text {
                visible: !copy.empty
                readonly property string album: (root.hero.album || "").trim()
                width: parent.width * 0.9; elide: Text.ElideRight
                text: album !== (root.hero.title || "").trim() ? album : ""
                height: text ? (root.compactStage ? 20 : implicitHeight + Theme.rem(0.5)) : 0
                verticalAlignment: Text.AlignBottom
                color: Theme.text3; font.family: Theme.fontUi
                font.pointSize: Theme.pt(Theme.rem(0.8))
                lineHeightMode: Text.FixedHeight; lineHeight: Math.round(Theme.rem(0.8) * 1.6)
            }
            // .stage-cue: clamp(2rem,10cqh,6rem) above, clamp(1.5rem,5cqh,3rem)
            // below, min-height 6rem reserved even for a one-line cue.
            Item {
                id: cueBlock
                readonly property real marginTop: root.compactStage ? 21 : 42
                readonly property real marginBottom: root.compactStage ? 21 : 35
                width: parent.width
                height: visible ? marginTop + (root.compactStage ? 0 : 32) + Math.round(root.cueSize * 1.6) * 2 + (root.compactStage ? 28 : 36) + marginBottom : 0
                visible: !copy.empty
                Column {
                    id: cueCol
                    y: cueBlock.marginTop
                    // .stage-cue p: max-width 22ch.
                    width: parent.width
                    // .cue-previous: .9rem/1.8 text3, margin-bottom .65rem.
                    Text {
                        id: cuePrevious
                        visible: !root.compactStage
                        width: parent.width
                        height: visible ? 32 : 0
                        text: music.previousCue
                        color: Theme.text3
                        font.family: Theme.fontUi; font.pixelSize: 12
                        lineHeightMode: Text.FixedHeight; lineHeight: 20
                        elide: Text.ElideRight
                    }
                    // .cue-current: 500 clamp(1.25rem,2.6cqw,2.6rem)/1.6, 3-line clamp.
                    Text {
                        id: cueCurrent
                        width: parent.width
                        readonly property real size: root.cueSize
                        height: Math.round(size * 1.6) * 2
                        text: music.cue || (music.current.hash === root.hero.hash ? "让音乐慢慢展开" : "下一段声音，等你开启")
                        color: Theme.text
                        font.family: Theme.fontUi; font.weight: Theme.fwMedium
                        font.pixelSize: size
                        lineHeightMode: Text.FixedHeight; lineHeight: Math.round(size * 1.6)
                        wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                    }
                    Text {
                        objectName: "curtainTranslation"
                        width: parent.width; height: root.compactStage ? 28 : 36
                        text: root.activeCue.translation || ""; textFormat: Text.PlainText
                        color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: root.compactStage ? 13 : 16
                        wrapMode: Text.Wrap; maximumLineCount: 1; elide: Text.ElideRight
                    }
                }
            }
            // .stage-transport: round bordered play + live-dot status.
            Row {
                visible: !copy.empty
                spacing: 8
                IconButton {
                    objectName: "curtainPlay"
                    glyph: music.playing && music.current.hash === root.hero.hash ? "pause" : "play"
                    implicitWidth: 44; implicitHeight: 44; glyphSize: 22
                    radius: Theme.rFull; variant: "secondary"; tint: Theme.text2; hoverTint: Theme.text
                    enabled: !music.loading
                    Accessible.name: (music.playing ? "暂停 " : "播放 ") + (root.hero.title || "")
                    onClicked: {
                        if (music.current.hash === root.hero.hash) { music.toggle(); return }
                        var at = -1
                        for (var i = 0; i < music.feed.length; i++) if (music.feed[i].hash === root.hero.hash) { at = i; break }
                        if (at >= 0) music.playFrom("feed", at)
                    }
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter; spacing: Theme.rem(0.5)
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.rem(0.35); height: width; radius: width / 2
                        color: music.playing && music.current.hash === root.hero.hash ? Theme.accent : Theme.text3
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: music.loading ? "加载中…"
                            : music.current.hash === root.hero.hash ? (music.playing ? "正在播放" : "已暂停")
                            : music.radio ? "红心推荐" : "每日推荐"
                        color: Theme.text2
                        font.family: Theme.fontMeta; font.pixelSize: 12
                    }
                }
            }
            // .stage-radio: RadioLauncher 1.25rem under the transport.
            Column {
                topPadding: root.compactStage ? 8 : 18
                AbstractButton {
                    id: radioStart
                    readonly property bool active: music.radio && !!music.current.hash
                    leftPadding: Theme.rem(0.9); rightPadding: leftPadding
                    topPadding: Theme.rem(0.65); bottomPadding: topPadding
                    implicitWidth: radioRow.implicitWidth + leftPadding + rightPadding
                    implicitHeight: 36
                    hoverEnabled: true; enabled: !active
                    scale: down ? 0.98 : 1
                    Behavior on scale { NumberAnimation { duration: 200 } }
                    onClicked: music.startRadio()
                    Accessible.name: radioLabel.text
                    background: Rectangle {
                        radius: Theme.rem(0.6)
                        color: radioStart.hovered && radioStart.enabled ? Theme.surfaceActive : Theme.radioSoft
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                    contentItem: Row {
                        id: radioRow; spacing: Theme.rem(0.5)
                        Icon { name: "heart"; filled: true; size: Theme.rem(1); color: Theme.radioAccent; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            id: radioLabel; anchors.verticalCenter: parent.verticalCenter
                            text: radioStart.active ? "红心推荐" : "开启红心推荐"; color: Theme.radioAccent
                            font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75)); font.weight: Theme.fwMedium
                        }
                    }
                }
                Text {
                    topPadding: Theme.rem(0.6)
                    visible: !root.compactStage
                    text: accountState.loggedIn ? "为你推荐，继续发现喜欢的声音" : "公共推荐 · 登录后同步你的音乐"; color: Theme.text3
                    font.family: Theme.fontUi; font.pixelSize: 11
                }
            }
            ActionButton {
                objectName: "curtainRetry"
                visible: copy.empty
                topPadding: Theme.rem(2)
                text: "获取推荐"; enabled: !music.loading
                onClicked: music.refreshFeed()
            }
        }

        // AlbumWall (57fr region). The wall field stops 2.8rem above the
        // stage bottom, where .discovery-link lives (39.2px at 125%): wall
        // navigation anchors to this band, not the stage edge.
        Item {
            id: covers
            objectName: "curtainCoverField"
            x: copy.x + copy.width + root.columnGap; width: stage.width - root.frameInset - x
            y: 56; height: stage.height - 136; clip: true
            // Album wall frames: the focused print centred at
            // (56%, 47%) and six neighbours on a pitch grid; scale and
            // opacity fall off with distance from the focus, z follows scale.
            // First hex ring from Folia hexViewport: a central cell and six
            // neighbours. Keep the mounted window bounded while browsing.
            readonly property var slots: [
                { x: 0, y: 0 }, { x: -1, y: 0 },
                { x: -0.5, y: -1 }, { x: 0.5, y: -1 },
                { x: 1, y: 0 }, { x: 0.5, y: 1 }, { x: -0.5, y: 1 }
            ]
            // Latest Folia GridView breakpoint boxes are 220×330, 250×375,
            // 280×420. Cap the box by available height for short Qt windows.
            readonly property real printWidth: Math.min(stage.width < 1440 ? 220 : stage.width < 2000 ? 250 : 280, height / 2.15)
            readonly property real pitchX: printWidth * 1.14
            readonly property real pitchY: printWidth * 1.45
            readonly property real maxDistance: stage.width < 1440 ? 500 : stage.width < 2000 ? 580 : 660
            property real panX: 0
            property real panY: 0
            // Prints glide only after a selection; resizes and drags move
            // them directly (.dragging .wall-print { transition: none }).
            property bool gliding: false
            function frame(slot) {
                var p = slots[Math.min(slot, slots.length - 1)]
                var x = p.x * pitchX + panX, y = p.y * pitchY + panY
                var distance = Math.min(1, Math.hypot(x, y) / maxDistance)
                return { x: x, y: y, scale: 1.1 - 0.65 * distance, opacity: 1 - 0.6 * distance }
            }
            function select(index) {
                gliding = true; glideTimer.restart()
                panX = 0; panY = 0
                root.selected = index
            }
            function move(delta) {
                var count = music.feed.length
                if (count > 1) select((root.selected + delta + count) % count)
            }
            // A drag pans the whole window; releasing focuses the print that
            // ended nearest the focus point.
            function release() {
                var count = music.feed.length, best = 0, bestDistance = Infinity
                for (var slot = 0; slot < Math.min(count, slots.length); slot++) {
                    var f = frame(slot), distance = Math.hypot(f.x, f.y)
                    if (distance < bestDistance) { bestDistance = distance; best = slot }
                }
                select((root.selected + best) % Math.max(1, count))
            }
            Timer { id: glideTimer; interval: 520; onTriggered: covers.gliding = false }
            DragHandler {
                target: null
                enabled: music.feed.length > 1
                dragThreshold: 8
                grabPermissions: PointerHandler.CanTakeOverFromAnything
                onActiveChanged: active ? covers.gliding = false : covers.release()
                onTranslationChanged: if (active) { covers.panX = translation.x; covers.panY = translation.y }
            }
            activeFocusOnTab: true
            Keys.onLeftPressed: covers.move(-1)
            Keys.onRightPressed: covers.move(1)

            Repeater {
                model: music.feed.length
                Item {
                    id: print
                    required property int index
                    readonly property int count: music.feed.length
                    readonly property int slot: (index - root.selected + count) % count
                    readonly property bool inWindow: slot < covers.slots.length
                    readonly property var f: covers.frame(slot)
                    readonly property var track: music.feed[index] || ({})
                    // Prints entering the window appear in place; only prints
                    // already on the wall glide to their new slot.
                    property bool settled: false
                    function settle() { settled = inWindow }
                    onInWindowChanged: Qt.callLater(settle)
                    Component.onCompleted: settle()
                    property real fx: f.x
                    property real fy: f.y
                    property real fs: f.scale
                    visible: inWindow
                    opacity: f.opacity
                    z: Math.round(f.scale * 10)
                    Behavior on fx { enabled: print.settled && covers.gliding; NumberAnimation { duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.2, 0.7, 0.25, 1, 1, 1] } }
                    Behavior on fy { enabled: print.settled && covers.gliding; NumberAnimation { duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.2, 0.7, 0.25, 1, 1, 1] } }
                    Behavior on fs { enabled: print.settled && covers.gliding; NumberAnimation { duration: 480; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.2, 0.7, 0.25, 1, 1, 1] } }
                    Behavior on opacity { enabled: print.settled && covers.gliding; NumberAnimation { duration: 360; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1, 1, 1] } }
                    Loader {
                        active: print.inWindow
                        x: covers.width * 0.56 + print.fx - width / 2
                        y: covers.height * 0.47 + print.fy - height / 2
                        sourceComponent: WallPrint {
                            s: print.fs; baseWidth: covers.printWidth; compact: root.compactStage
                            objectName: print.slot === 0 ? "curtainFocusedPrint" : "curtainNeighborPrint_" + print.slot
                            track: print.track
                            current: !!print.track.hash && print.track.hash === music.current.hash
                            playing: music.playing; busy: music.loading
                            playEnabled: !music.radio || print.track.hash === music.current.hash
                            onPicked: covers.select(print.index)
                            onPlay: root.play(print.index)
                        }
                    }
                }
            }
            Rectangle {
                z: 9; width: parent.width; height: parent.height * 0.08
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.bg }
                    GradientStop { position: 1; color: Theme.clear(Theme.bg) }
                }
            }
            Rectangle {
                z: 9; y: parent.height * 0.92; width: parent.width; height: parent.height * 0.08
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.clear(Theme.bg) }
                    GradientStop { position: 1; color: Theme.bg }
                }
            }
        }
        // .wall-navigation: flex footer inset auto 1.5rem .5rem of the wall —
        // rail label + counter left, prev/next/refresh controls right, all
        // centred on the 34px control row.
        Item {
            anchors.left: covers.left; anchors.right: covers.right; anchors.bottom: covers.bottom
            anchors.leftMargin: 12; anchors.rightMargin: 12; anchors.bottomMargin: -44
            height: 40
            Row {
                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "随心推荐"; color: Theme.text2
                    font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.8))
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: Theme.rem(0.5); visible: music.feed.length > 0
                    text: (root.selected + 1) + " / " + music.feed.length; color: Theme.text3
                    font.family: Theme.fontMeta; font.pointSize: Theme.pt(Theme.rem(0.65))
                }
            }
            Row {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.rem(0.2)
                IconButton { glyph: "chevron-left"; Accessible.name: "上一张封面"; enabled: music.feed.length >= 2; onClicked: covers.move(-1) }
                IconButton { glyph: "chevron-right"; Accessible.name: "下一张封面"; enabled: music.feed.length >= 2; onClicked: covers.move(1) }
                IconButton { glyph: "refresh-cw"; Accessible.name: "刷新推荐"; enabled: !music.loading; onClicked: music.refreshFeed() }
            }
        }

        StageChip {
            objectName: "wallEntry"; glyph: "panels-top-left"; text: "海报墙"
            anchors.right: listenEntry.left; anchors.rightMargin: 10; anchors.top: parent.top; anchors.topMargin: 21
            onClicked: root.wall()
        }
        StageChip {
            id: listenEntry; objectName: "listenEntry"; glyph: "panel-right-open"; text: "聆听"
            anchors.right: parent.right; anchors.rightMargin: 14; anchors.top: parent.top; anchors.topMargin: 21
            onClicked: root.listen()
        }
        AbstractButton {
            objectName: "curtainDiscoverLink"; text: "下滑，发现更多"
            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 12
            implicitWidth: 160; implicitHeight: 40; hoverEnabled: true
            contentItem: Row {
                spacing: 8; anchors.centerIn: parent
                Text { text: "下滑，发现更多"; font.family: Theme.fontUi; font.pixelSize: 10; color: Theme.text3; anchors.verticalCenter: parent.verticalCenter }
                Icon { name: "arrow-down"; size: 14; color: Theme.text3; anchors.verticalCenter: parent.verticalCenter }
            }
            onClicked: root.contentY = stage.height
        }
    }

    Item {
        id: discovery; objectName: "curtainDiscovery"; y: stage.height; width: root.width
        height: Math.max(root.height, discoveryContent.y + discoveryContent.height + 36)
        Column {
            id: discoveryContent
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: root.frameInset; anchors.rightMargin: root.frameInset; anchors.topMargin: 12
            spacing: 16
            RowLayout {
                width: parent.width
                Text {
                    readonly property real size: 20
                    text: "继续探索"; color: Theme.text
                    font.family: Theme.fontUi; font.weight: Theme.fwSemiBold
                    font.preferTypoLineMetrics: Theme.lyricTypoMetrics
                    font.pixelSize: size
                    font.letterSpacing: -0.02 * size
                    lineHeightMode: Text.FixedHeight; lineHeight: Math.round(size * 1.4)
                }
                Item { Layout.fillWidth: true }
                AbstractButton {
                    id: back
                    topPadding: Theme.rem(0.4); bottomPadding: topPadding
                    implicitWidth: backRow.implicitWidth; implicitHeight: backRow.implicitHeight + topPadding + bottomPadding
                    hoverEnabled: true
                    onClicked: root.contentY = 0
                    contentItem: Row {
                        id: backRow; spacing: Theme.rem(0.4)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "回到舞台"; color: back.hovered ? Theme.text : Theme.text2
                            font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.7)); font.letterSpacing: Theme.rem(0.7) * 0.08
                        }
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "arrow-up"; size: Theme.rem(1); color: back.hovered ? Theme.text : Theme.text2 }
                    }
                }
            }
            Grid {
                id: grid; width: parent.width
                objectName: "curtainRecommendations"
                columns: Math.max(4, Math.min(8, Math.floor((width + 16) / 164)))
                spacing: 16
                Repeater {
                    model: music.feed.slice(0, grid.columns)
                    SongCard {
                        required property var modelData; required property int index
                        objectName: "curtainRecommendation_" + index
                        width: (grid.width - grid.spacing * (grid.columns - 1)) / grid.columns
                        track: modelData; playEnabled: !music.radio; onPlay: root.play(index)
                    }
                }
            }
            Text {
                visible: music.feed.length === 0
                text: music.loading || homeState.loading ? "正在加载推荐…" : "暂时没有推荐，可刷新重试"
                font.family: Theme.fontUi; font.pixelSize: 13; color: Theme.text2
            }
            Row {
                visible: homeState.error !== ""; width: parent.width; spacing: 12
                Text { width: parent.width - retryFeed.width - parent.spacing; wrapMode: Text.Wrap; text: homeState.error; color: Theme.danger; font.family: Theme.fontUi; font.pixelSize: 13 }
                ActionButton { id: retryFeed; text: "重试"; implicitHeight: 40; onClicked: homeState.refresh() }
            }
            HomeCollections { width: parent.width; title: "精选歌单"; collections: homeState.playlists; onOpenPlaylist: playlist => root.openPlaylist(playlist) }
            HomeCollections { width: parent.width; title: "推荐专辑"; collections: homeState.recommendedAlbums; onOpenAlbum: album => root.openAlbum(album) }
            HomeCollections { width: parent.width; title: "新歌与发现"; collections: homeState.albums; onOpenPlaylist: playlist => root.openPlaylist(playlist) }
        }
    }

}
