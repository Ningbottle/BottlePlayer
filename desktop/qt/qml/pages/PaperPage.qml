import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import BottleMusic

// 报刊 home: page layout — the Mint masthead (date kicker over a display
// greeting, rule underneath), hero + recommendation rail stage, 每日推荐
// tile grid. `results` flips the page to the search list (the search field
// itself lives in the shell topbar). Paper routes are always the Mint skin,
// so the masthead, hairline rules and serif roles all come from Theme.
Item {
    id: root
    objectName: "paperPage"
    property bool results: false
    signal home()
    signal openPlayer()
    signal openPlaylist(var playlist)
    signal openAlbum(var album)

    readonly property real vw: Window.width
    readonly property real vh: Window.height
    // .home-page: padding s2 / clamp(20px, 3vw, 40px) / s10 (.scroll:has()
    // clears the shell scroll padding for the home page).
    readonly property real homePad: Theme.clamp(Theme.s5, vw * 0.03, Theme.s10)
    // Snapped to device pixels: the whole page column rides this offset, so a
    // fractional inset would put every glyph on the page off-grid at 125%.
    readonly property real inset: Theme.snap(homePad)

    function greeting() {
        var h = new Date().getHours()
        return h < 5 ? "夜深了" : h < 11 ? "早上好" : h < 13 ? "中午好" : h < 18 ? "下午好" : "晚上好"
    }
    // The edition name follows the press day, like the old "LATE EDITION".
    function edition() {
        var h = new Date().getHours()
        return h < 11 ? "早报" : h < 18 ? "午报" : "晚刊"
    }
    function dateline() {
        var d = new Date(), days = ["日", "一", "二", "三", "四", "五", "六"]
        return "星期" + days[d.getDay()] + " · " + (d.getMonth() + 1) + "月" + d.getDate() + "日"
    }
    function feedIndex(hash) {
        for (var i = 0; i < music.feed.length; i++) if (music.feed[i].hash === hash) return i
        return -1
    }
    function isActive(track) { return !!track && !!music.current.hash && track.hash === music.current.hash }

    // .home-kicker: fs-xs --font-meta in --meta-style with --meta-tracking.
    component Kicker: CssText {
        color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs
        font.letterSpacing: Theme.metaTracking; font.capitalization: Theme.metaCase
        font.italic: Theme.metaItalic
    }
    // The bilingual section head — a Song-serif Chinese title with its
    // italic latin byline on the same baseline ("每日推荐 Daily"), plus a
    // trailing slot for the section action.
    component SectionHead: Item {
        id: head
        property string cn: ""
        property string en: ""
        default property alias tail: tailBox.data
        implicitHeight: Theme.controlMd
        CssText {
            id: cnTxt; anchors.verticalCenter: parent.verticalCenter
            text: head.cn; color: Theme.text
            font.family: Theme.fontSerif; font.pixelSize: Theme.fsXl; font.weight: Theme.fwRegular
        }
        CssText {
            anchors.baseline: cnTxt.baseline; anchors.left: cnTxt.right; anchors.leftMargin: Theme.s2
            visible: head.en !== ""; text: head.en; color: Theme.text3
            font.family: Theme.fontMeta; font.pixelSize: Theme.fsMd; font.italic: true
        }
        Item {
            id: tailBox; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            implicitWidth: children.length > 0 ? children[0].implicitWidth : 0
            implicitHeight: children.length > 0 ? children[0].implicitHeight : 0
        }
    }
    // .home-thumb-play / .home-tile-play: --stage-bg at 45% over the art; the
    // tile badge is a --play-bg disc with a --play-fg glyph (Mint inks both).
    component PlayVeil: Rectangle {
        property real glyphSize: 16
        property bool disc: false
        color: Qt.rgba(Theme.stageBg.r, Theme.stageBg.g, Theme.stageBg.b, 0.45)
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
        Rectangle {
            anchors.centerIn: parent; visible: parent.disc
            width: Theme.s8; height: Theme.s8; radius: width / 2; color: Theme.playBg
        }
        Icon {
            anchors.centerIn: parent; anchors.horizontalCenterOffset: Theme.mediaOffset("play",width); name: Theme.mediaStyle.play.glyph; stroke: Theme.mediaStyle.play.stroke; filled: true; size: parent.glyphSize
            color: parent.disc ? Theme.playFg : Theme.stageText
        }
    }

    Loader {
        id: viewLoader
        anchors.fill: parent
        sourceComponent: root.results ? searchView : homeView
    }
    ScrollFade {
        anchors.fill: parent
        flickable: !root.results && viewLoader.item ? viewLoader.item : null
    }

    Component {
        id: homeView
        Flickable {
            id: scroller
            objectName: "homeScroller"
            readonly property var flickable: scroller
            clip: true; contentWidth: width; contentHeight: page.height
            boundsBehavior: Flickable.StopAtBounds
            HoverHandler { id: scrollHover }
            ScrollBar.vertical: ThinScrollBar { areaHovered: scrollHover.hovered }
            SmoothWheel {
                id: wheel; flickable: scroller
                onScrollInput: { magnet.stop(); magnetIdle.stop() }
            }
            // Magnetic first two screens (stage, playlist ring): once motion
            // settles above the daily section, glide to the nearer screen
            // edge, biased toward the direction of travel. Below it the long
            // page scrolls freely.
            property real magnetFrom: 0
            function magnetize() {
                if (stacked || !carousel.visible) return
                const stops = [0, Theme.snap(page.y + carousel.y - Theme.s6), Theme.snap(page.y + dailySection.mapToItem(page, 0, 0).y - Theme.s6)]
                const y = contentY
                if (y >= stops[2] - 1) { magnetFrom = y; return }
                var i = 0
                while (i < stops.length - 2 && y >= stops[i + 1]) i++
                const t = (y - stops[i]) / Math.max(1, stops[i + 1] - stops[i])
                const down = y >= magnetFrom
                var target = (down ? t > 0.12 : t > 0.88) ? stops[i + 1] : stops[i]
                target = Math.max(0, Math.min(target, contentHeight - height))
                magnetFrom = target
                if (Math.abs(target - y) < 1) return
                magnet.to = target; magnet.restart()
            }
            NumberAnimation { id: magnet; objectName: "homeMagnet"; target: scroller; property: "contentY"; duration: music.reducedMotion ? 0 : 560; easing.type: Easing.OutQuint }
            Timer {
                id: magnetIdle; interval: 110
                onTriggered: if (!scroller.dragging && !scroller.flicking && !wheel.glide.running && !magnet.running) scroller.magnetize()
            }
            onContentYChanged: if (!magnet.running) magnetIdle.restart()
            onDraggingChanged: if (dragging) magnet.stop()
            SnapSettle { flickable: scroller }
            property int railOffset: 0
            property bool dailyExpanded: false
            function toggleDaily() {
                dailyExpanded = !dailyExpanded
                Qt.callLater(function() {
                    if (!scroller.dailyExpanded) {
                        const top = dailySection.mapToItem(scroller, 0, 0).y + scroller.contentY
                        scroller.contentY = Math.min(scroller.contentY, Math.max(0, top - Theme.s3))
                    }
                    scroller.contentY = Math.max(0, Math.min(scroller.contentY, scroller.contentHeight - scroller.height))
                })
            }

            readonly property real contentW: width - root.inset * 2
            readonly property bool stacked: root.vw <= 1100
            readonly property var hero: music.hero
            readonly property bool heroIsCurrent: root.isActive(hero)
            // Enough rows to fill the first-screen plate (about 50px each).
            readonly property int railRows: stacked ? 6 : Math.max(6, Math.min(10, Math.floor((height - Theme.rem(20)) / 50)))
            readonly property var railTracks: {
                if (music.radio) return music.queue.slice(music.queueIndex + 1, music.queueIndex + 1 + railRows)
                var out = [], count = Math.min(railRows, music.feed.length)
                for (var i = 0; i < count; i++) out.push(music.feed[(railOffset + i) % music.feed.length])
                return out
            }
            // --home-tile-min: 160px Mint / 168px Aurora, 136px below 1280px.
            readonly property real tileMin: root.vw < 1280 ? 136 : Theme.mint ? 160 : 168
            readonly property int columns: Math.max(1, Math.floor((contentW + Theme.s5) / (tileMin + Theme.s5)))
            // Daily pool size: the daily list keeps 30
            // tracks; the grid collapses to two rows and offers 展开全部.
            readonly property var daily: music.feed.slice(0, 30)
            readonly property int collapsed: Math.min(daily.length, columns * 2)

            Column {
                id: page
                x: root.inset; y: Theme.s2
                width: scroller.contentW
                spacing: Theme.clamp(Theme.s8, root.vh * 0.04, Theme.s10)
                height: implicitHeight + Theme.s2 + Theme.s10

                // The masthead: edition eyebrow and dateline over the
                // bilingual display line, then the folio double rule —
                // "LATE EDITION · 晚刊 / 为你精选 For You / 星期日 · 10月18日".
                Column {
                    width: parent.width
                    spacing: 0
                    Item {
                        width: parent.width
                        height: Math.max(editionKicker.height, datelineCol.implicitHeight) + Theme.s2
                        Kicker { id: editionKicker; anchors.left: parent.left; anchors.bottom: parent.bottom; text: root.edition() }
                        Column {
                            id: datelineCol; anchors.right: parent.right; anchors.bottom: parent.bottom; spacing: 0
                            CssText {
                                anchors.right: parent.right; text: root.dateline(); color: Theme.text2
                                font.family: Theme.fontTitle; font.pixelSize: Theme.fsSm
                            }
                            CssText {
                                anchors.right: parent.right; text: root.greeting(); color: Theme.text3
                                font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: true
                            }
                        }
                    }
                    Item {
                        width: parent.width; height: mastCn.height
                        CssText {
                            id: mastCn
                            readonly property real exact: Theme.greetSize(root.vw)
                            clamp: true; lineBox: exact * 1.1
                            text: "为你精选"; color: Theme.text
                            font.family: Theme.fontSerifDisplay; font.pixelSize: Math.round(exact); font.weight: Theme.fwRegular
                        }
                    }
                    Item { width: 1; height: Theme.s4 }
                    DoubleRule { visible: Theme.rules; width: parent.width }
                }

                // .home-stage: hero (1.5fr) + rail (minmax(17rem, 1fr)).
                Grid {
                    id: stage
                    width: parent.width
                    columns: scroller.stacked ? 1 : 2
                    columnSpacing: Theme.snap(Theme.clamp(Theme.s6, root.vw * 0.03, Theme.s10))
                    rowSpacing: columnSpacing
                    verticalItemAlignment: Grid.AlignTop
                    // Hero and rail share one row height side by side.
                    // Side by side they fill the first screen under the masthead.
                    readonly property real rowH: scroller.stacked ? 0
                        : Math.max(heroInner.implicitHeight + Theme.s6 * 2, rail.implicitHeight + Theme.s5 * 2, scroller.height - y - Theme.s8)
                    // .feature: 1.15fr hero beside the 1fr side-list.
                    readonly property real railW: scroller.stacked ? width
                        : Math.max(Theme.rem(17), (width - columnSpacing) * (1.0 / 2.15))
                    readonly property real heroW: scroller.stacked ? width : width - columnSpacing - railW

                    Item {
                        id: hero
                        width: stage.heroW; height: heroPlate.height
                        readonly property real coverSize: scroller.hero.hash ? Theme.snap(Theme.clamp(148, root.vw * 0.13, 240)) : 0
                        // The headline runs on a press plate — --paper-2 fill,
                        // translucent-ink hairline, the dashed plate mark inset
                        // 5px, and the vinyl print cut by the lower-right edge.
                        PlateFrame {
                            id: heroPlate; width: parent.width
                            height: Math.max(heroInner.implicitHeight + Theme.s6 * 2, stage.rowH)
                            innerRule: true; clip: true
                            VinylMark {
                                z: -1
                                size: Theme.clamp(120, heroPlate.height * 0.68, 200)
                                x: parent.width - width + Theme.snap(10)
                                y: parent.height - height + Theme.snap(10)
                            }
                            Item {
                                id: heroInner
                                x: Theme.s6; y: Theme.snap((heroPlate.height - implicitHeight) / 2); width: parent.width - Theme.s6 * 2
                                implicitHeight: Math.max(cover.height, info.implicitHeight)
                        Item {
                            id: cover
                            visible: !!scroller.hero.hash
                            width: hero.coverSize; height: width
                            anchors.verticalCenter: parent.verticalCenter
                            // .home-hero-glow: the cover again, blurred 40px,
                            // saturate 1.5, --hero-glow-opacity (0 in Mint).
                            Image {
                                id: glowArt
                                x: -0.06 * cover.width; y: 0.08 * cover.height
                                width: cover.width * 1.12; height: cover.height * 1.02
                                source: Theme.artwork(scroller.hero.image || "", 200)
                                sourceSize.width: 200; sourceSize.height: 200
                                fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: false
                                visible: false
                            }
                            MultiEffect {
                                anchors.fill: glowArt; source: glowArt
                                visible: Theme.heroGlowOpacity > 0 && glowArt.status === Image.Ready
                                blurEnabled: true; blur: 1.0; blurMax: 64; saturation: 0.5
                                opacity: Theme.heroGlowOpacity; autoPaddingEnabled: true
                            }
                            Cover {
                                anchors.fill: parent; radius: Theme.rXs; pixels: 400
                                url: scroller.hero.image || ""; title: ""
                                elevation: 2
                            }
                            // Pressed into the plate: the photo keeps the
                            // press's halftone screen and an inset ink edge —
                            // no floating black border.
                            Halftone { anchors.fill: parent; opacity: 0.14 }
                            Rectangle {
                                anchors.fill: parent; radius: Theme.rXs; color: "transparent"
                                border.width: Theme.hairline
                                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.22)
                            }
                        }
                        Column {
                            id: info
                            x: cover.visible ? cover.width + Theme.clamp(Theme.s5, root.vw * 0.025, Theme.s8) : 0
                            width: heroInner.width - x
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.s3
                            bottomPadding: Theme.s2
                            // "HEADLINE · 头条" on the left, the playback state
                            // dot on the right — the deck line of the card.
                            Item {
                                width: parent.width; height: Theme.s4
                                Kicker { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "头条" }
                                Row {
                                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: Theme.s2
                                    Item {
                                        width: Theme.s2; height: Theme.s2; anchors.verticalCenter: parent.verticalCenter
                                        Rectangle {
                                            anchors.centerIn: parent; width: Theme.s4; height: width; radius: width / 2
                                            color: Theme.sealSoft; visible: music.playing && scroller.heroIsCurrent
                                        }
                                        Rectangle {
                                            anchors.fill: parent; radius: width / 2
                                            color: music.playing && scroller.heroIsCurrent ? Theme.seal : Theme.text3
                                        }
                                    }
                                    Kicker {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: !scroller.hero.hash ? "还没有开始播放"
                                            : scroller.heroIsCurrent ? (music.playing ? "正在播放" : "已暂停") : "今日推荐"
                                    }
                                }
                            }
                            CssText {
                                id: heroTitle
                                objectName: "heroTitle"
                                // --display-lg (37.12px at 1280), 1.15 line box.
                                readonly property real exact: Theme.displayLg(root.vw)
                                readonly property int size: Math.round(exact)
                                clamp: true; lineBox: exact * 1.15
                                width: parent.width
                                text: scroller.hero.title || "选一首歌，开始聆听"; color: Theme.text
                                font.family: Theme.fontDisplay; font.pixelSize: size; font.weight: Theme.fwDisplay
                                font.letterSpacing: Theme.displayTracking * size
                                // No elide here: with elide set, QQuickText also
                                // elides vertically — a 45.7px natural line does
                                // not fit the 42.69px clamped box and the whole
                                // title is truncated away. maximumLineCount
                                // alone still caps the layout at two lines.
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere; maximumLineCount: 2
                            }
                            CssText {
                                width: parent.width; visible: !!scroller.hero.hash
                                readonly property string album: (scroller.hero.album || "").trim()
                                text: (scroller.hero.artist || "")
                                    + (album && album !== (scroller.hero.title || "").trim() ? "  ·  " + album : "")
                                color: Theme.text2; font.family: Theme.fontTitle; font.pixelSize: Theme.fsLg
                                elide: Text.ElideRight
                            }
                            // .home-hero-actions: margin-top 8 on top of the 12px grid gap.
                            Row {
                                topPadding: Theme.s2
                                spacing: Theme.s3
                                AbstractButton {
                                    // .hero-cta: the ink pill carrying the
                                    // cinnabar record — .pp disc + tracked copy.
                                    id: heroPlay
                                    objectName: "heroPlay"
                                    visible: !!scroller.hero.hash
                                    implicitHeight: Theme.s10
                                    leftPadding: Theme.s1 + 2; rightPadding: Theme.s5
                                    implicitWidth: leftPadding + rightPadding + ctaRow.implicitWidth
                                    hoverEnabled: true
                                    enabled: !(scroller.heroIsCurrent && music.loading && !music.playing)
                                    Accessible.name: ctaLabel.text
                                    onClicked: {
                                        if (scroller.heroIsCurrent) { music.toggle(); return }
                                        var index = root.feedIndex(scroller.hero.hash)
                                        if (index >= 0) music.playFrom("feed", index)
                                    }
                                    background: Rectangle {
                                        radius: height / 2
                                        color: !heroPlay.enabled ? Theme.accentSoft : heroPlay.down ? Theme.accentPressed : Theme.customized && (heroPlay.hovered || heroPlay.visualFocus) ? Theme.accentHover : Theme.accent
                                        PixelSnap.fit: true
                                        Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover } }
                                    }
                                    contentItem: Row {
                                        id: ctaRow
                                        spacing: Theme.s2
                                        Rectangle {
                                            // .pp — the cinnabar disc.
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: heroPlay.height - (heroPlay.leftPadding * 2); height: width; radius: width / 2
                                            // A custom tone makes seal equal the pill, so invert the disc.
                                            color: Theme.customAccent.length > 0 ? Theme.accentContrast : Theme.seal
                                            Icon {
                                                // The play triangle's mass sits left of its box.
                                                anchors.centerIn: parent; anchors.horizontalCenterOffset: name === "play" ? 1 : 0
                                                name: scroller.heroIsCurrent && music.playing ? "pause" : "play"
                                                filled: true; size: Theme.rem(0.7)
                                                color: Theme.customAccent.length > 0 ? Theme.accent : Theme.accentContrast
                                            }
                                        }
                                        Text {
                                            id: ctaLabel; anchors.verticalCenter: parent.verticalCenter
                                            text: !scroller.heroIsCurrent ? "播放" : music.loading ? "正在加载…" : music.playing ? "暂停" : "播放"
                                            color: heroPlay.enabled ? Theme.accentContrast : Theme.text3
                                            font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; font.weight: Theme.fwMedium
                                            font.letterSpacing: Theme.rem(0.05)
                                        }
                                    }
                                }
                                ActionButton {
                                    visible: !!scroller.hero.hash
                                    size: "lg"; bgRadius: Theme.rFull
                                    leftPadding: Theme.s5 + Theme.hairline; rightPadding: leftPadding
                                    restFill: "transparent"; edgeColor: Theme.borderStrong
                                    text: "打开播放页"; onClicked: root.openPlayer()
                                }
                                ActionButton {
                                    objectName: "heroRefresh"
                                    visible: !scroller.hero.hash
                                    text: "获取推荐"
                                    enabled: !music.loading
                                    onClicked: music.refreshFeed()
                                }
                            }
                            // .home-radio-entry: margin-top 1.25rem (17.5px)
                            // ADDS to the 12px flex gap — the whole hero block
                            // is grid-centered, so this margin places it.
                            Column {
                                topPadding: Theme.rem(1.25)
                                AbstractButton {
                                    id: radioStart
                                    objectName: "heroRadio"
                                    readonly property bool active: music.radio && !!music.current.hash
                                    implicitWidth: radioRow.implicitWidth + Theme.rem(0.9) * 2
                                    implicitHeight: radioRow.implicitHeight + Theme.rem(0.65) * 2
                                    hoverEnabled: true
                                    scale: down ? 0.98 : 1
                                    Behavior on scale { NumberAnimation { duration: 200 } }
                                    enabled: music.ready
                                    onClicked: radioStart.active || music.radioPending ? music.stopRadio() : music.startRadio()
                                    Accessible.name: radioLabel.text
                                    background: Rectangle {
                                        radius: Theme.rem(0.6)
                                        color: radioStart.hovered && radioStart.enabled ? Theme.surfaceActive : Theme.radioSoft
                                        Behavior on color { ColorAnimation { duration: 200 } }
                                    }
                                    contentItem: Item {
                                        Row {
                                            id: radioRow; anchors.centerIn: parent; spacing: Theme.rem(0.5)
                                            // Centre the heart on the CJK ideographic box (~0.4em above
                                            // the baseline), not the line box, snapped to device pixels.
                                            Icon {
                                                name: "heart"; filled: true; size: 14; color: Theme.radioAccent
                                                y: Math.round((radioLabel.baselineOffset - radioLabel.font.pixelSize * 0.4 - height / 2) * Theme.dpr) / Theme.dpr + Theme.hairline
                                            }
                                            Text {
                                                id: radioLabel
                                                text: radioStart.active ? "退出红心推荐" : music.radioPending ? "取消准备推荐" : "开启红心推荐"; color: Theme.radioAccent
                                                font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; font.weight: Theme.fwMedium
                                            }
                                        }
                                    }
                                }
                                Text {
                                    topPadding: Theme.rem(0.6)
                                    text: "先听公共推荐 · 登录后使用账号推荐"; color: Theme.text3
                                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                                    lineHeightMode: Text.FixedHeight; lineHeight: Math.round(Theme.fsXs * 1.5)
                                }
                            }
                        }
                            }
                        }
                    }

                    // The trending rail: the side-list plate — translucent
                    // paper under a hairline, "每周飙升 Trending" head, and
                    // numbered serif rows parted by dotted leaders.
                    Item {
                        width: stage.railW
                        height: railPlate.height
                        PlateFrame {
                            id: railPlate; width: parent.width
                            height: Math.max(rail.implicitHeight + Theme.s5 * 2, stage.rowH)
                            fill: Theme.paperVeil
                            Column {
                                id: rail
                                x: Theme.s5; y: Theme.s5; width: parent.width - Theme.s5 * 2
                                spacing: 0
                                Column {
                                    width: rail.width; spacing: Theme.s1; bottomPadding: Theme.s1
                                    SectionHead {
                                        width: rail.width
                                        cn: music.radio ? "接下来播放" : "每周飙升"
                                                                                IconButton {
                                            objectName: "railReshuffle"
                                            visible: !music.radio
                                            glyph: "refresh-cw"; tint: Theme.text3
                                            // .home-reshuffle:hover svg: rotate(90deg).
                                            glyphRotation: hovered ? 90 : 0
                                            enabled: music.feed.length > scroller.railRows
                                            Accessible.name: "换一批"
                                            onClicked: scroller.railOffset = (scroller.railOffset + scroller.railRows) % Math.max(1, music.feed.length)
                                        }
                                    }
                                    Hairline { width: rail.width }
                                }
                                Grid {
                                    id: railGrid
                                    width: rail.width
                                    columns: scroller.stacked ? 2 : 1
                                    columnSpacing: Theme.s4; rowSpacing: 0
                                    Repeater {
                                        model: scroller.railTracks
                                        AbstractButton {
                                            id: row
                                            required property var modelData
                                            required property int index
                                            objectName: "railTrack" + index
                                            width: (railGrid.width - (railGrid.columns - 1) * railGrid.columnSpacing) / railGrid.columns
                                            // .side-list li: 6px row padding
                                            // over the two-line copy.
                                            height: Theme.s3 + rowCopy.implicitHeight
                                            hoverEnabled: true
                                            Accessible.name: "播放 " + (modelData.title || "") + "，" + (modelData.artist || "")
                                            onClicked: {
                                                if (music.radio) { music.playQueue(music.queueIndex + 1 + index); return }
                                                var at = root.feedIndex(modelData.hash)
                                                if (at >= 0) music.playFrom("feed", at)
                                            }
                                            background: Rectangle {
                                                // li:hover — the 2% ink wash, not a surface card.
                                                color: row.hovered ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.035) : "transparent"
                                                Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
                                                DotRule { y: parent.height - height; width: parent.width; visible: scroller.stacked || row.index < scroller.railTracks.length - 1 }
                                            }
                                            contentItem: Item {
                                                CssText {
                                                    id: num
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    width: Theme.s6
                                                    text: String(row.index + 1).padStart(2, "0"); color: Theme.text3
                                                    font.family: Theme.fontMeta; font.pixelSize: Theme.fsMd; font.italic: true
                                                    font.features: { "tnum": 1 }
                                                }
                                                Column {
                                                    id: rowCopy
                                                    x: num.width + Theme.s2
                                                    width: time.x - x - Theme.s3
                                                    anchors.verticalCenter: parent.verticalCenter; spacing: 0
                                                    CssText {
                                                        width: parent.width
                                                        text: row.modelData.title || ""; elide: Text.ElideRight
                                                        color: root.isActive(row.modelData) ? Theme.seal : Theme.text
                                                        font.family: Theme.fontSerif; font.pixelSize: Theme.fsSm
                                                    }
                                                    CssText {
                                                        width: parent.width; lineBox: Theme.fsXs * Theme.lhNormal
                                                        text: row.modelData.artist || ""; elide: Text.ElideRight
                                                        color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                                                    }
                                                }
                                                CssText {
                                                    id: time
                                                    anchors.right: parent.right
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: row.modelData.duration > 0 ? Theme.duration(row.modelData.duration) : ""
                                                    color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: true
                                                    font.features: { "tnum": 1 }
                                                }
                                            }
                                        }
                                    }
                                }
                                Text {
                                    visible: scroller.railTracks.length === 0
                                    topPadding: Theme.s4; bottomPadding: Theme.s4
                                    text: music.ready ? "暂时没有推荐，稍后再试" : "正在准备推荐…"
                                    color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
                                }
                            }
                        }
                    }
                }

                PlaylistCarousel {
                    id: carousel; objectName: "homeCarousel"
                    visible: homeState.playlists.length > 0 && !scroller.stacked
                    width: parent.width; height: implicitHeight
                    title: "编辑推荐"; collections: homeState.playlists
                    onOpenPlaylist: playlist => root.openPlaylist(playlist)
                }
                HomeCollections { visible: scroller.stacked; width: parent.width; title: "编辑推荐"; collections: homeState.playlists; onOpenPlaylist: playlist => root.openPlaylist(playlist) }
                // .home-section 每日推荐: auto-fill minmax(--home-tile-min, 1fr),
                // gap 24/20. Mint: rule first (border-top), THEN 12px
                // padding-top, then head + tiles.
                Column {
                    id: dailySection
                    width: parent.width; spacing: 0
                    visible: music.feed.length > 0
                    Hairline { visible: Theme.rules; width: parent.width; color: Theme.ruleInk }
                    Column {
                        width: parent.width; spacing: Theme.s4
                        topPadding: Theme.s3
                        SectionHead {
                            width: parent.width
                            cn: "每日推荐"
                            ActionButton {
                                objectName: "dailyExpand"
                                visible: scroller.daily.length > scroller.collapsed
                                quiet: true; size: "sm"; restInk: Theme.text3
                                text: scroller.dailyExpanded ? "收起到两行" : "展开全部 · " + scroller.daily.length + " 首"
                                trailingGlyph: "chevron-down"
                                trailingRotation: scroller.dailyExpanded ? 180 : 0
                                onClicked: scroller.toggleDaily()
                            }
                        }
                        Grid {
                            id: tiles
                            objectName: "dailyGrid"
                            width: parent.width; columns: scroller.columns
                            columnSpacing: Theme.s5; rowSpacing: Theme.s6
                            readonly property real tileW: (width - (columns - 1) * columnSpacing) / columns
                            Repeater {
                                objectName: "dailyRepeater"
                                model: scroller.dailyExpanded ? scroller.daily : scroller.daily.slice(0, scroller.collapsed)
                                AbstractButton {
                                    id: tile
                                    required property var modelData
                                    required property int index
                                    objectName: "dailyTile" + index
                                    width: tiles.tileW; height: tileText.y + tileText.implicitHeight
                                    hoverEnabled: true
                                    Accessible.name: "播放 " + (modelData.title || "") + "，" + (modelData.artist || "")
                                    onClicked: music.playFrom("feed", index)
                                    background: null
                                    contentItem: Item {
                                        Cover {
                                            id: art; width: tile.width; height: width
                                            radius: Theme.rXs; pixels: 300; url: tile.modelData.image || ""; title: ""
                                            transform: Translate {
                                                y: tile.hovered ? -Theme.s1 : 0
                                                Behavior on y { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
                                            }
                                            elevation: tile.hovered ? 2 : 0
                                            PlayVeil { anchors.fill: parent; radius: Theme.rXs; disc: true; opacity: tile.hovered || tile.visualFocus ? 1 : 0 }
                                            Rectangle {
                                                anchors.fill: parent; radius: Theme.rXs; color: "transparent"
                                                border.width: Theme.hairline
                                                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.16)
                                            }
                                        }
                                        // .home-tile: the cover's 8px margin plus the 4px grid
                                        // gap, then title (--font-title) and sub (inherited
                                        // --font-ui), both at the button's 1.5 line-height.
                                        Column {
                                            id: tileText; y: art.height + Theme.s2 + Theme.s1; width: tile.width; spacing: Theme.s1
                                            Text {
                                                width: parent.width; height: Theme.fsMd * Theme.lhNormal; verticalAlignment: Text.AlignVCenter
                                                text: tile.modelData.title || ""; elide: Text.ElideRight
                                                color: root.isActive(tile.modelData) ? Theme.seal : Theme.text
                                                font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd
                                            }
                                            Text {
                                                width: parent.width; height: Theme.fsXs * Theme.lhNormal; verticalAlignment: Text.AlignVCenter
                                                text: tile.modelData.artist || ""; elide: Text.ElideRight
                                                color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                HomeCollections { width: parent.width; title: "推荐专辑"; collections: homeState.recommendedAlbums; onOpenAlbum: album => root.openAlbum(album) }
                HomeCollections { width: parent.width; title: "新歌与发现"; collections: homeState.albums; onOpenPlaylist: playlist => root.openPlaylist(playlist) }
            }
        }
    }

    Component {
        id: searchView
        ColumnLayout {
            spacing: Theme.s4
            Item { Layout.preferredHeight: Theme.s5 - Theme.s4; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: root.inset; Layout.rightMargin: root.inset
                Text {
                    readonly property int size: Math.round(Theme.displayLg(root.vw))
                    text: "搜索音乐"; color: Theme.text
                    font.family: Theme.fontDisplay; font.pixelSize: size
                    font.weight: Theme.fwDisplay; font.letterSpacing: Theme.displayTracking * size
                    renderType: Text.NativeRendering
                }
                Item { Layout.fillWidth: true }
                ActionButton { quiet: true; glyph: "arrow-left"; text: "回到首页"; onClicked: root.home() }
            }
            Text {
                Layout.leftMargin: root.inset
                text: music.searchError || (music.searching ? "正在搜索…" : "找到 " + music.searchTotal + " 条 · 已加载 " + music.searchResults.length + " 条")
                color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic
            }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.leftMargin: root.inset - Theme.s1; Layout.rightMargin: root.inset - Theme.s1
            ListView {
                id: list; clip: true; model: music.searchResults
                anchors.fill: parent
                HoverHandler { id: listHover }
                ScrollBar.vertical: ThinScrollBar { areaHovered: listHover.hovered }
                SmoothWheel { flickable: list }
                // .song-row: 12px 4px padding, hairline, --r-sm, fs-sm.
                delegate: AbstractButton {
                    id: songRow
                    required property var modelData; required property int index
                    objectName: "searchRow" + index
                    width: list.width; height: 50 + Theme.s3 * 2
                    hoverEnabled: true
                    onDoubleClicked: music.playFrom("search", index)
                    background: Rectangle {
                        radius: Theme.rSm
                        color: root.isActive(songRow.modelData) ? Theme.sealSoft
                            : songRow.hovered ? Theme.surfaceHover : Theme.clear(Theme.surfaceHover)
                        Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
                        Hairline { y: parent.height - Theme.hairline; width: parent.width }
                    }
                    contentItem: RowLayout {
                        spacing: Theme.s2
                        Text {
                            Layout.preferredWidth: Theme.s8; Layout.leftMargin: Theme.s1
                            text: String(songRow.index + 1).padStart(2, "0"); color: Theme.text3
                            font.family: Theme.fontMeta; font.pixelSize: Theme.fsSm; font.italic: true; font.features: { "tnum": 1 }
                        }
                        Cover {
                            Layout.preferredWidth: 50; Layout.preferredHeight: 50; Layout.rightMargin: Theme.s2
                            pixels: 200; radius: Theme.rXs; url: songRow.modelData.image || ""; title: songRow.modelData.title || ""
                        }
                        // grid-template-columns: 2.2fr 1.2fr 1.2fr.
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 22
                            text: songRow.modelData.title || ""; elide: Text.ElideRight
                            color: root.isActive(songRow.modelData) ? Theme.seal : Theme.text
                            font.family: Theme.fontTitle; font.pixelSize: Theme.fsSm
                        }
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 12
                            text: songRow.modelData.artist || ""; elide: Text.ElideRight; color: Theme.text2
                            font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
                        }
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 12
                            text: songRow.modelData.album || ""; elide: Text.ElideRight; color: Theme.text2
                            font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
                        }
                        Text {
                            Layout.preferredWidth: Theme.s10
                            horizontalAlignment: Text.AlignRight
                            text: Theme.duration(songRow.modelData.duration); color: Theme.text3
                            font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; font.features: { "tnum": 1 }
                        }
                        Item {
                            Layout.preferredWidth: Math.ceil(searchPlay.width); Layout.preferredHeight: Math.ceil(searchPlay.height)
                            IconButton {
                                id: searchPlay; objectName: "searchPlay" + songRow.index
                                media: "play"; mediaBox: 28; mediaGlyph: 14; anchors.centerIn: parent
                                Accessible.name: "播放 " + (songRow.modelData.title || "")
                                onClicked: music.playFrom("search", songRow.index)
                            }
                        }
                    }
                }
                footer: Item {
                    width: list.width; height: 70
                    ActionButton {
                        objectName: "searchMore"; anchors.centerIn: parent
                        text: music.searching ? "加载中…" : music.searchError ? "重试搜索" : music.searchResults.length < music.searchTotal ? "继续加载更多" : "已经到底了"
                        enabled: !music.searching && (!!music.searchError || music.searchResults.length < music.searchTotal)
                        onClicked: music.search(music.searchTerm, music.searchResults.length > 0)
                    }
                }
            }
            ScrollFade { anchors.fill: parent; flickable: list }
        }
    }
}
}
