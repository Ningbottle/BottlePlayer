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
    readonly property real inset: homePad

    function greeting() {
        var h = new Date().getHours()
        return h < 5 ? "夜深了" : h < 11 ? "早上好" : h < 13 ? "中午好" : h < 18 ? "下午好" : "晚上好"
    }
    function todayLine() {
        var d = new Date(), days = ["日", "一", "二", "三", "四", "五", "六"]
        return (d.getMonth() + 1) + "月" + d.getDate() + "日 · 星期" + days[d.getDay()]
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
    // .home-section-title: fs-xl --font-display, --display-tracking.
    component SectionTitle: CssText {
        color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: Theme.fsXl
        font.weight: Theme.fwDisplay; font.letterSpacing: Theme.displayTracking * Theme.fsXl
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
            SmoothWheel { flickable: scroller }
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
            readonly property var railTracks: {
                if (music.radio) return music.queue.slice(music.queueIndex + 1, music.queueIndex + 7)
                var out = [], count = Math.min(6, music.feed.length)
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

                // .home-greeting (Mint --greet-flow: column): date kicker over
                // the display greeting, then 16px padding-bottom, then the
                // --rule-ink border-bottom (the padding sits ABOVE the rule).
                Column {
                    width: parent.width
                    spacing: 0
                    Column {
                        width: parent.width
                        bottomPadding: Theme.s4
                        spacing: Theme.s2
                        Kicker { text: root.todayLine() }
                        CssText {
                            id: greetText
                            // --greet-size (51.2px at 1280) with a 1.1 line
                            // box, smaller than Caslon's natural line.
                            readonly property real exact: Theme.greetSize(root.vw)
                            readonly property int size: Math.round(exact)
                            clamp: true; lineBox: exact * 1.1
                            text: root.greeting(); color: Theme.text
                            font.family: Theme.fontDisplay; font.pixelSize: size; font.weight: Theme.fwDisplay
                            font.letterSpacing: Theme.displayTracking * size
                        }
                    }
                    Hairline { visible: Theme.rules; width: parent.width; color: Theme.ruleInk }
                }

                // .home-stage: hero (1.5fr) + rail (minmax(17rem, 1fr)).
                Grid {
                    id: stage
                    width: parent.width
                    columns: scroller.stacked ? 1 : 2
                    columnSpacing: Theme.clamp(Theme.s6, root.vw * 0.03, Theme.s10)
                    rowSpacing: columnSpacing
                    verticalItemAlignment: Grid.AlignVCenter
                    readonly property real heroW: scroller.stacked ? width
                        : width - columnSpacing - Math.max(Theme.rem(17), (width - columnSpacing) * 0.4)
                    readonly property real railW: scroller.stacked ? width : width - columnSpacing - heroW

                    Item {
                        id: hero
                        width: stage.heroW; height: Math.max(cover.height, info.implicitHeight)
                        readonly property real coverSize: scroller.hero.hash ? Theme.clamp(192, root.vw * 0.19, 352) : 0
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
                                anchors.fill: parent; radius: Theme.rCard; pixels: 400
                                url: scroller.hero.image || ""; title: ""
                                elevation: 2
                            }
                            // .home-hero-art outline: --rule-w --rule-ink at
                            // 3×--rule-w offset (printer's frame in Mint).
                            // Four aliased quads — Chromium pixel-snaps the
                            // outline, a bordered+rounded Rectangle smears it
                            // over two device rows at 125% DPI.
                            Item {
                                id: coverFrame
                                anchors.fill: parent
                                visible: Theme.rules
                                readonly property real gap: Math.floor(3 * Theme.dpr) / Theme.dpr
                                readonly property real ext: gap + Theme.hairline
                                Rectangle { x: -coverFrame.ext; y: -coverFrame.ext; width: coverFrame.width + 2 * coverFrame.ext; height: Theme.hairline; color: Theme.ruleInk }
                                Rectangle { x: -coverFrame.ext; y: coverFrame.height + coverFrame.gap; width: coverFrame.width + 2 * coverFrame.ext; height: Theme.hairline; color: Theme.ruleInk }
                                Rectangle { x: -coverFrame.ext; y: -coverFrame.gap; width: Theme.hairline; height: coverFrame.height + 2 * coverFrame.gap; color: Theme.ruleInk }
                                Rectangle { x: coverFrame.width + coverFrame.gap; y: -coverFrame.gap; width: Theme.hairline; height: coverFrame.height + 2 * coverFrame.gap; color: Theme.ruleInk }
                            }
                        }
                        Column {
                            id: info
                            x: cover.visible ? cover.width + Theme.clamp(Theme.s5, root.vw * 0.025, Theme.s8) : 0
                            width: hero.width - x
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.s3
                            bottomPadding: Theme.s2
                            Row {
                                spacing: Theme.s2
                                Item {
                                    width: Theme.s2; height: Theme.s2; anchors.verticalCenter: parent.verticalCenter
                                    Rectangle {
                                        anchors.centerIn: parent; width: Theme.s4; height: width; radius: width / 2
                                        color: Theme.accentSoft; visible: music.playing && scroller.heroIsCurrent
                                    }
                                    Rectangle {
                                        anchors.fill: parent; radius: width / 2
                                        color: music.playing && scroller.heroIsCurrent ? Theme.accent : Theme.text3
                                    }
                                }
                                Kicker {
                                    text: !scroller.hero.hash ? "还没有开始播放"
                                        : scroller.heroIsCurrent ? (music.playing ? "正在播放" : "已暂停") : "今日推荐"
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
                                ActionButton {
                                    objectName: "heroPlay"
                                    visible: !!scroller.hero.hash
                                    filled: true; size: "lg"; bgRadius: Theme.rFull
                                    leftPadding: Theme.s5 + Theme.hairline; rightPadding: leftPadding
                                    media: scroller.heroIsCurrent && (music.playing || music.loading) ? "pause" : "play"
                                    glyphFilled: true
                                    text: !scroller.heroIsCurrent ? "播放" : music.loading ? "正在加载…" : music.playing ? "暂停" : "播放"
                                    enabled: !(scroller.heroIsCurrent && music.loading && !music.playing)
                                    onClicked: {
                                        if (scroller.heroIsCurrent) { music.toggle(); return }
                                        var index = root.feedIndex(scroller.hero.hash)
                                        if (index >= 0) music.playFrom("feed", index)
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
                                            Icon { name: "heart"; filled: true; size: Theme.rem(1); color: Theme.radioAccent; anchors.verticalCenter: parent.verticalCenter }
                                            Text {
                                                id: radioLabel; anchors.verticalCenter: parent.verticalCenter
                                                text: radioStart.active ? "退出红心推荐" : music.radioPending ? "取消准备推荐" : "开启红心推荐"; color: Theme.radioAccent
                                                font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75)); font.weight: Theme.fwMedium
                                                lineHeightMode: Text.FixedHeight; lineHeight: Math.round(Theme.rem(0.75) * 1.4)
                                            }
                                        }
                                    }
                                }
                                Text {
                                    topPadding: Theme.rem(0.6)
                                    text: "先听公共推荐 · 登录后使用账号推荐"; color: Theme.text3
                                    font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.7))
                                    lineHeightMode: Text.FixedHeight; lineHeight: Math.round(Theme.rem(0.7) * 1.6)
                                }
                            }
                        }
                    }

                    // .home-rail: title + reshuffle, six 40px-thumb rows;
                    // --rule-w --border left rule outside the padding.
                    Item {
                        width: stage.railW
                        height: rail.implicitHeight
                        readonly property real railPad: scroller.stacked ? 0 : Theme.clamp(0, root.vw * 0.02, Theme.s8)
                        Hairline {
                            visible: Theme.rules && !scroller.stacked; vertical: true
                            height: parent.height
                        }
                        Column {
                            id: rail
                            x: parent.railPad
                            width: parent.width - parent.railPad
                            spacing: Theme.s2
                            Item {
                                width: rail.width; height: Theme.controlMd
                                SectionTitle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: music.radio ? "接下来播放" : "随心推荐"
                                }
                                IconButton {
                                    objectName: "railReshuffle"
                                    anchors.right: parent.right; visible: !music.radio
                                    glyph: "refresh-cw"; tint: Theme.text3
                                    // .home-reshuffle:hover svg: rotate(90deg).
                                    glyphRotation: hovered ? 90 : 0
                                    enabled: music.feed.length > 6
                                    Accessible.name: "换一批"
                                    onClicked: scroller.railOffset = (scroller.railOffset + 6) % Math.max(1, music.feed.length)
                                }
                            }
                            Grid {
                                id: railGrid
                                width: rail.width
                                columns: scroller.stacked ? 2 : 1
                                columnSpacing: Theme.s2; rowSpacing: 2
                                Repeater {
                                    model: scroller.railTracks
                                    AbstractButton {
                                        id: row
                                        required property var modelData
                                        required property int index
                                        objectName: "railTrack" + index
                                        width: (railGrid.width - (railGrid.columns - 1) * railGrid.columnSpacing) / railGrid.columns
                                        // .home-row: 8px padding around max(40px thumb, copy),
                                        // the copy being 21 + 2 + 18 at the button's 1.5 line-height.
                                        height: Theme.s2 * 2 + Math.max(Theme.s10, rowCopy.implicitHeight)
                                        hoverEnabled: true
                                        Accessible.name: "播放 " + (modelData.title || "") + "，" + (modelData.artist || "")
                                        onClicked: {
                                            if (music.radio) { music.playQueue(music.queueIndex + 1 + index); return }
                                            var at = root.feedIndex(modelData.hash)
                                            if (at >= 0) music.playFrom("feed", at)
                                        }
                                        background: Rectangle {
                                            radius: Theme.rMd
                                            color: row.hovered ? Theme.surfaceHover : Theme.clear(Theme.surfaceHover)
                                            Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
                                        }
                                        contentItem: Item {
                                            Cover {
                                                id: thumb; x: Theme.s2; anchors.verticalCenter: parent.verticalCenter
                                                width: Theme.s10; height: Theme.s10; radius: Theme.rSm; pixels: 120
                                                url: row.modelData.image || ""; title: ""
                                                PlayVeil { anchors.fill: parent; radius: Theme.rSm; opacity: row.hovered ? 1 : 0 }
                                            }
                                            // .home-row-copy: <b> in --font-title, <small> stays
                                            // in the button's inherited --font-ui.
                                            Column {
                                                id: rowCopy
                                                x: thumb.x + thumb.width + Theme.s3
                                                width: time.x - x - Theme.s3
                                                anchors.verticalCenter: parent.verticalCenter; spacing: 2
                                                Text {
                                                    width: parent.width; height: Theme.fsMd * Theme.lhNormal; verticalAlignment: Text.AlignVCenter
                                                    text: row.modelData.title || ""; elide: Text.ElideRight
                                                    color: root.isActive(row.modelData) ? Theme.accent : Theme.text
                                                    font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd; font.weight: Theme.fwMedium
                                                }
                                                Text {
                                                    width: parent.width; height: Theme.fsXs * Theme.lhNormal; verticalAlignment: Text.AlignVCenter
                                                    text: row.modelData.artist || ""; elide: Text.ElideRight
                                                    color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                                                }
                                            }
                                            Text {
                                                id: time
                                                anchors.right: parent.right; anchors.rightMargin: Theme.s2
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: row.modelData.duration > 0 ? Theme.duration(row.modelData.duration) : ""
                                                color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
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
                        Item {
                            width: parent.width; height: Theme.controlSm
                            SectionTitle { anchors.verticalCenter: parent.verticalCenter; text: "每日推荐" }
                            ActionButton {
                                objectName: "dailyExpand"
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
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
                                            radius: Theme.rCard; pixels: 300; url: tile.modelData.image || ""; title: ""
                                            transform: Translate {
                                                y: tile.hovered ? -Theme.s1 : 0
                                                Behavior on y { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
                                            }
                                            elevation: tile.hovered ? 2 : 0
                                            PlayVeil { anchors.fill: parent; radius: Theme.rCard; disc: true; opacity: tile.hovered || tile.visualFocus ? 1 : 0 }
                                        }
                                        // .home-tile: the cover's 8px margin plus the 4px grid
                                        // gap, then title (--font-title) and sub (inherited
                                        // --font-ui), both at the button's 1.5 line-height.
                                        Column {
                                            id: tileText; y: art.height + Theme.s2 + Theme.s1; width: tile.width; spacing: Theme.s1
                                            Text {
                                                width: parent.width; height: Theme.fsMd * Theme.lhNormal; verticalAlignment: Text.AlignVCenter
                                                text: tile.modelData.title || ""; elide: Text.ElideRight
                                                color: root.isActive(tile.modelData) ? Theme.accent : Theme.text
                                                font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd; font.weight: Theme.fwMedium
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
                HomeCollections { width: parent.width; title: "精选歌单"; collections: homeState.playlists; onOpenPlaylist: playlist => root.openPlaylist(playlist) }
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
                        color: root.isActive(songRow.modelData) ? Theme.accentSoft
                            : songRow.hovered ? Theme.surfaceHover : Theme.clear(Theme.surfaceHover)
                        Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
                        Hairline { y: parent.height - Theme.hairline; width: parent.width }
                    }
                    contentItem: RowLayout {
                        spacing: Theme.s2
                        Text {
                            Layout.preferredWidth: Theme.s8; Layout.leftMargin: Theme.s1
                            text: String(songRow.index + 1).padStart(2, "0"); color: Theme.text3
                            font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; font.features: { "tnum": 1 }
                        }
                        Cover {
                            Layout.preferredWidth: 50; Layout.preferredHeight: 50; Layout.rightMargin: Theme.s2
                            pixels: 200; radius: Theme.rXs; url: songRow.modelData.image || ""; title: songRow.modelData.title || ""
                        }
                        // grid-template-columns: 2.2fr 1.2fr 1.2fr.
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 22
                            text: songRow.modelData.title || ""; elide: Text.ElideRight
                            color: root.isActive(songRow.modelData) ? Theme.accent : Theme.text
                            font.family: Theme.fontTitle; font.pixelSize: Theme.fsSm; font.weight: Theme.fwMedium
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
