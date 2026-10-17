import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes
import BottleMusic

// PosterWallView / PosterCard / PosterTransport port: dark artwork field,
// caption type formula, wall-glass chrome (round back button, now-playing
// card, locate/lights tools, listening trigger) and the current-track ring.
Item {
    id: root
    objectName: "posterWallPage"
    clip: true
    signal back()
    signal listen()
    signal player()
    property var tracks: music.queue.length ? music.queue : music.feed
    readonly property string source: music.queue.length ? "queue" : "recommended"
    readonly property int currentIndex: music.queue.length ? music.queueIndex : -1
    property bool lights: true
    readonly property bool compact: width <= 1100
    readonly property real inset: compact ? Theme.s4 : Theme.s5
    // Poster wall geometry: 92px pitch, 4px gap, 16 posters per 12 × 8-pitch block.
    readonly property real pitch: 92
    readonly property real blockW: 12 * pitch
    readonly property real blockH: 8 * pitch
    // The expanded PosterSlot and its block repack (slot → rect), or null.
    property var selected: null
    readonly property var expansion: selected ? expandPoster(tracks.length, selected) : null

    function modulo(value, divisor) { return (value % divisor + divisor) % divisor }
    // Each 6 × 4 quadrant holds a tall or wide cover plus two squares;
    // reflections keep neighbouring seams varied.
    function cellShape(column, row, slot) {
        var quadrant = Math.floor(slot / 4)
        var vertical = modulo(column + row + quadrant, 2) === 0
        var shape = (vertical ? [[0, 0, 2, 4], [2, 0, 4, 2], [2, 2, 2, 2], [4, 2, 2, 2]]
                              : [[0, 0, 6, 2], [0, 2, 2, 2], [2, 2, 2, 2], [4, 2, 2, 2]])[slot % 4]
        var flipX = modulo(column * 3 + row + quadrant, 3) !== 0
        var flipY = modulo(column + row * 3 + quadrant, 3) === 0
        return [quadrant % 2 * 6 + (flipX ? 6 - shape[0] - shape[2] : shape[0]),
                Math.floor(quadrant / 2) * 4 + (flipY ? 4 - shape[1] - shape[3] : shape[1]), shape[2], shape[3]]
    }
    function period(count) {
        var blocks = Math.ceil(Math.max(1, count) / 16)
        var columns = Math.max(1, Math.ceil(Math.sqrt(blocks * 1.7)))
        return { columns: columns, rows: Math.ceil(blocks / columns) }
    }
    function makeSlot(count, blockX, blockY, slot) {
        var p = period(count), localX = modulo(blockX, p.columns), localY = modulo(blockY, p.rows)
        var cell = cellShape(localX, localY, slot)
        return { id: blockX + ":" + blockY + ":" + slot, blockX: blockX, blockY: blockY, slot: slot,
                 queueIndex: ((localY * p.columns + localX) * 16 + slot) % Math.max(1, count),
                 x: (blockX * 12 + cell[0]) * pitch, y: (blockY * 8 + cell[1]) * pitch,
                 w: cell[2] * pitch - 4, h: cell[3] * pitch - 4 }
    }
    // Repack only the selected block: a 6 × 6 cover plus fifteen squares beside it.
    function expandPoster(count, poster) {
        var originX = poster.blockX * blockW, originY = poster.blockY * blockH
        var right = poster.x + poster.w / 2 - originX >= blockW / 2
        var bottom = poster.y + poster.h / 2 - originY >= blockH / 2
        var largeCol = right ? 6 : 0, largeRow = bottom ? 2 : 0
        var cells = [], col, row
        for (row = 0; row < 8; row += 2) for (col = 0; col < 6; col += 2) cells.push([col + (right ? 0 : 6), row, 2, 2])
        for (col = 0; col < 6; col += 2) cells.push([col + largeCol, bottom ? 0 : 6, 2, 2])
        var rects = [], cursor = 0
        for (var slot = 0; slot < 16; slot++) {
            var cell = slot === poster.slot ? [largeCol, largeRow, 6, 6] : cells[cursor++]
            rects.push({ x: originX + cell[0] * pitch, y: originY + cell[1] * pitch,
                         w: cell[2] * pitch - 4, h: cell[3] * pitch - 4 })
        }
        return rects
    }
    // The repeated copy of a queue index closest to a world point.
    function nearestPoster(count, index, point) {
        if (count <= 0 || index < 0 || index >= count) return null
        var p = period(count), slots = p.columns * p.rows * 16, best = null, distance = Infinity
        for (var address = index; address < slots; address += count) {
            var block = Math.floor(address / 16), slot = address % 16
            var localX = block % p.columns, localY = Math.floor(block / p.columns)
            var base = makeSlot(count, localX, localY, slot)
            var repeatX = Math.round((point.x - base.x - base.w / 2) / (p.columns * blockW))
            var repeatY = Math.round((point.y - base.y - base.h / 2) / (p.rows * blockH))
            var poster = makeSlot(count, localX + repeatX * p.columns, localY + repeatY * p.rows, slot)
            var d = Math.hypot(poster.x + poster.w / 2 - point.x, poster.y + poster.h / 2 - point.y)
            if (d < distance) { best = poster; distance = d }
        }
        return best
    }
    function focusRect(rect, animate) {
        if (field.width <= 0 || field.height <= 0) return
        camera.stop()
        var toX = (field.originX + rect.x + rect.w / 2) * field.zoom - field.width / 2
        var toY = (field.originY + rect.y + rect.h / 2) * field.zoom - field.height / 2
        if (!animate) { field.contentX = toX; field.contentY = toY; return }
        panX.to = toX; panY.to = toY; camera.start()
    }
    function expand(poster) {
        if (selected && selected.id === poster.id) return
        selected = poster
        focusRect(expandPoster(tracks.length, poster)[poster.slot], true)
    }
    function focusPoster(index) {
        var poster = nearestPoster(tracks.length, index, field.center())
        if (!poster) return
        selected = poster
        focusRect(expandPoster(tracks.length, poster)[poster.slot], true)
    }
    function focusCurrent(animate) {
        var poster = nearestPoster(tracks.length, Math.max(0, currentIndex), field.center())
        if (!poster) return
        selected = currentIndex >= 0 ? poster : null
        focusRect(selected ? expandPoster(tracks.length, poster)[poster.slot] : poster, animate !== false)
    }
    function canPlay(address) { return !music.radio || address === currentIndex }
    function play(address) {
        if (!canPlay(address)) return
        if (address === currentIndex) { music.toggle(); return }
        if (music.queue.length) music.playQueue(address); else music.playFrom("feed", address)
    }
    function hue(hash) {
        var h = 0
        for (var i = 0; i < (hash || "").length; i++) h = (h * 31 + hash.charCodeAt(i)) % 360
        return h
    }

    // .wall-glass: 1px wall border, --r-md, glass fill, --wall-glass-shadow.
    component Glass: Rectangle {
        property bool lit: false
        radius: Theme.rMd
        color: lit ? Theme.wallHover : Theme.wallGlassBg
        border.color: Theme.wallBorder
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true; shadowBlur: 1.0; blurMax: Theme.wallGlassBlur
            shadowColor: Theme.wallGlassShadow; shadowVerticalOffset: 6
        }
    }
    component GlassButton: AbstractButton {
        id: glassButton
        property string glyph: ""
        property real glyphSize: 18
        property bool round: false
        implicitWidth: 40; implicitHeight: 40
        hoverEnabled: true
        opacity: enabled ? 1 : 0.4
        Accessible.role: Accessible.Button
        background: Glass { radius: glassButton.round ? width / 2 : Theme.rMd; lit: glassButton.hovered && glassButton.enabled }
        contentItem: Item {
            Icon { anchors.centerIn: parent; name: glassButton.glyph; size: glassButton.glyphSize; color: Theme.wallInk }
        }
    }
    // PosterTransport buttons: --wall-control-width × --control-lg, 18px glyph.
    component WallControl: AbstractButton {
        id: control
        property string glyph: ""
        property string media: ""
        readonly property var mediaSpec: Theme.mediaStyle[media] || Theme.mediaStyle.play
        implicitWidth: Theme.wallControlWidth; implicitHeight: Theme.controlLg
        hoverEnabled: true
        opacity: enabled ? 1 : 0.3
        Accessible.role: Accessible.Button
        background: Rectangle { radius: 8; color: control.hovered && control.enabled ? Theme.wallControlHover : "transparent" }
        contentItem: Item {
            Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: control.media ? Theme.mediaOffset(control.media,width*field.zoom)/field.zoom : 0; objectName: control.objectName ? control.objectName+"_glyph" : ""; name: control.media ? control.mediaSpec.glyph : control.glyph; filled: control.media !== ""; stroke: control.media ? control.mediaSpec.stroke : Theme.iconStroke; stroked: control.media ? control.mediaSpec.stroked : true; size: 18; color: Theme.wallInk; rasterScale: field.zoom }
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.wallBg }
    ParallelAnimation {
        id: camera
        NumberAnimation { id: panX; target: field; property: "contentX"; duration: 260; easing.type: Easing.OutCubic }
        NumberAnimation { id: panY; target: field; property: "contentY"; duration: 260; easing.type: Easing.OutCubic }
    }
    Flickable {
        id: field; objectName: "wallField"; anchors.fill: parent; clip: true
        // usePosterCamera: scale = clamp(.6, min(w / 1280, h / 780), 1). The
        // layout is periodic, so a very large content area stands in for the
        // unbounded camera and any poster can be centred.
        readonly property real zoom: Math.max(0.6, Math.min(1, width / 1280, height / 780))
        readonly property real originX: 96 * root.blockW
        readonly property real originY: 144 * root.blockH
        contentWidth: originX * 2 * zoom; contentHeight: originY * 2 * zoom
        boundsBehavior: Flickable.StopAtBounds
        pressDelay: 0
        onMovementStarted: camera.stop()

        // Viewport in world units, with a two-pitch preload margin for culling.
        readonly property real viewLeft: contentX / zoom - originX
        readonly property real viewTop: contentY / zoom - originY
        readonly property real viewRight: viewLeft + width / zoom
        readonly property real viewBottom: viewTop + height / zoom
        readonly property real margin: 2 * root.pitch
        function center() {
            return Qt.point((contentX + width / 2) / zoom - originX, (contentY + height / 2) / zoom - originY)
        }

        // Delegates cover the blocks around the centre block. Each pool column
        // keeps its block until it falls off one edge, so panning by a block
        // only reassigns one column (or row) of posters.
        readonly property int centerBlockX: Math.floor((viewLeft + viewRight) / 2 / root.blockW)
        readonly property int centerBlockY: Math.floor((viewTop + viewBottom) / 2 / root.blockH)
        readonly property int spanX: Math.max(1, Math.ceil((width / zoom / 2 + margin) / root.blockW))
        readonly property int spanY: Math.max(1, Math.ceil((height / zoom / 2 + margin) / root.blockH))
        readonly property int poolColumns: spanX * 2 + 1
        readonly property int poolRows: spanY * 2 + 1

        // A resize keeps the same world point centred, as usePosterCamera.measure does.
        property point anchor: Qt.point(0, 0)
        property bool settling: false
        property bool measured: false
        onContentXChanged: if (!settling) anchor = center()
        onContentYChanged: if (!settling) anchor = center()
        function settle() {
            if (width <= 0 || height <= 0) return
            settling = true
            contentX = (originX + anchor.x) * zoom - width / 2
            contentY = (originY + anchor.y) * zoom - height / 2
            settling = false
            if (!measured) { measured = true; refocusTimer.restart() }
        }
        onWidthChanged: settle()
        onHeightChanged: settle()
        onZoomChanged: settle()

        Item {
            width: field.originX * 2; height: field.originY * 2
            scale: field.zoom; transformOrigin: Item.TopLeft
            Repeater {
                model: root.tracks.length ? field.poolColumns * field.poolRows * 16 : 0
                Item {
                    id: tile
                    required property int index
                    readonly property int slot: index % 16
                    readonly property int pool: Math.floor(index / 16)
                    readonly property int blockX: field.centerBlockX - field.spanX
                        + root.modulo(pool % field.poolColumns - field.centerBlockX + field.spanX, field.poolColumns)
                    readonly property int blockY: field.centerBlockY - field.spanY
                        + root.modulo(Math.floor(pool / field.poolColumns) - field.centerBlockY + field.spanY, field.poolRows)
                    readonly property var poster: root.makeSlot(root.tracks.length, blockX, blockY, slot)
                    readonly property bool inSelectedBlock: !!root.selected
                        && root.selected.blockX === blockX && root.selected.blockY === blockY
                    readonly property var rect: inSelectedBlock ? root.expansion[slot] : poster
                    readonly property int queueAddress: poster.queueIndex
                    readonly property var track: root.tracks[queueAddress] || ({})
                    readonly property bool expanded: inSelectedBlock && root.selected.slot === slot
                    readonly property bool activeSong: queueAddress === root.currentIndex
                    readonly property real titleSize: expanded ? 54 : Math.min(30, Math.max(21, Math.min(width * 0.10, height * 0.12)))
                    x: field.originX + rect.x; y: field.originY + rect.y; width: rect.w; height: rect.h
                    z: expanded ? 5 : 0
                    visible: rect.x < field.viewRight + field.margin && rect.x + rect.w > field.viewLeft - field.margin
                        && rect.y < field.viewBottom + field.margin && rect.y + rect.h > field.viewTop - field.margin
                    Loader {
                        anchors.fill: parent; active: tile.visible
                        sourceComponent: Component {
                            Item {
                                clip: true
                                // Loading wash: linear-gradient(145deg, hsl(h 30% 36%), hsl(h+30 24% 12%)).
                                Rectangle {
                                    id: wash
                                    anchors.fill: parent
                                    readonly property real h: root.hue(tile.track.hash) / 360
                                    gradient: Gradient {
                                        orientation: Gradient.Vertical
                                        GradientStop { position: 0; color: Qt.hsla(wash.h, 0.30, 0.36, 1) }
                                        GradientStop { position: 1; color: Qt.hsla((wash.h + 30 / 360) % 1, 0.24, 0.12, 1) }
                                    }
                                }
                                Cover {
                                    anchors.fill: parent; radius: 0; color: "transparent"
                                    glyphScale: 0.25; glyphColor: Qt.rgba(1, 1, 1, 0.18)
                                    opacity: root.lights ? 1 : 0.45
                                    url: tile.track.image || ""; pixels: tile.expanded ? 800 : 400
                                }
                                // --wall-shade floor gradient keeps captions readable.
                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        GradientStop { position: 0.1; color: Qt.rgba(0, 0, 0, 0.08) }
                                        GradientStop { position: 0.4; color: Qt.rgba(0, 0, 0, 0.12) }
                                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.78) }
                                    }
                                }
                                MouseArea {
                                    id: open; anchors.fill: parent; hoverEnabled: true
                                    onClicked: root.expand(tile.poster)
                                }
                                Rectangle { anchors.fill: parent; color: Theme.wallPosterHover; visible: open.containsMouse }
                                // .poster-number: 4px 7px pad, 500 10px, .02em tracking, square.
                                Rectangle {
                                    x: Theme.s3; y: Theme.s3
                                    width: badge.implicitWidth + 14; height: badge.implicitHeight + 8
                                    color: tile.activeSong ? Theme.wallCurrentBg : Theme.wallBadgeBg
                                    Text {
                                        id: badge; anchors.centerIn: parent
                                        renderType: Text.QtRendering
                                        text: (tile.activeSong ? "正在播放 · " : "") + String(tile.queueAddress + 1).padStart(2, "0")
                                        color: tile.activeSong ? Theme.wallInk : Theme.wallCurrentBorder
                                        font.family: Theme.fontUi; font.pixelSize: 10; font.weight: Theme.fwMedium
                                        font.letterSpacing: 0.2; lineHeightMode: Text.FixedHeight; lineHeight: 12
                                    }
                                }
                                Column {
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    anchors.leftMargin: tile.expanded ? 28 : 14; anchors.rightMargin: anchors.leftMargin
                                    anchors.bottomMargin: tile.expanded ? Theme.wallTitleClearance : 14
                                    spacing: 5
                                    // --wall-caption-shadow as a 1px text style; layering the
                                    // column would rasterize (and blur) the glyphs.
                                    Text {
                                        width: parent.width; text: tile.track.title || ""; color: Theme.wallInk
                                        renderType: Text.QtRendering
                                        font.family: Theme.fontUi; font.pixelSize: tile.titleSize
                                        font.weight: 650; font.letterSpacing: -0.035 * tile.titleSize
                                        lineHeight: 1.08; maximumLineCount: 2
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere; elide: Text.ElideRight
                                        style: Text.Sunken; styleColor: Theme.wallCaptionShadow
                                    }
                                    Text {
                                        width: parent.width; text: tile.track.artist || ""; color: Theme.wallInkSecondary
                                        renderType: Text.QtRendering
                                        font.family: Theme.fontUi; font.pixelSize: tile.expanded ? 14 : 11
                                        lineHeight: 1.4; elide: Text.ElideRight
                                        style: Text.Sunken; styleColor: Theme.wallCaptionShadow
                                    }
                                }
                                // PosterLyrics: inset 16% 1.75rem, 600 clamp(24px, 3vw, 40px)/1.45.
                                Text {
                                    x: Theme.rem(1.75); y: parent.height * 0.16; width: parent.width - Theme.rem(1.75) * 2
                                    height: Math.min(implicitHeight, parent.height * 0.25); clip: true
                                    visible: tile.expanded && tile.activeSong && music.cue.length > 0
                                    text: music.cue; color: Theme.wallInk
                                    renderType: Text.QtRendering
                                    font.family: Theme.fontUi; font.weight: Theme.fwSemiBold
                                    font.pixelSize: Math.min(40, Math.max(24, Window.width * 0.03))
                                    lineHeight: 1.45; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    style: Text.Sunken; styleColor: Theme.wallLyricShadow
                                }
                                Column {
                                    visible: tile.expanded
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    anchors.leftMargin: 26; anchors.rightMargin: 26; anchors.bottomMargin: Theme.s6
                                    spacing: Theme.s1
                                    RowLayout {
                                        width: parent.width; spacing: Theme.s1
                                        WallControl {
                                            visible: tile.activeSong && !music.radio; media: "prev"
                                            enabled: !music.loading; Accessible.name: "上一首"; onClicked: music.previous()
                                        }
                                        WallControl {
                                            objectName: "wallPlay" + tile.index
                                            media: tile.activeSong && music.playing ? "pause" : "play"
                                            enabled: root.canPlay(tile.queueAddress)
                                            Accessible.name: (tile.activeSong && music.playing ? "暂停 " : "播放 ") + (tile.track.title || "")
                                            onClicked: root.play(tile.queueAddress)
                                        }
                                        WallControl {
                                            visible: tile.activeSong; media: "next"
                                            enabled: !music.loading; Accessible.name: music.radio ? "换一首" : "下一首"; onClicked: music.next()
                                        }
                                        Item { Layout.fillWidth: true }
                                        Text {
                                            text: (tile.activeSong ? Theme.duration(music.position / 1000) : "0:00") + " / "
                                                + Theme.duration(tile.activeSong ? music.duration / 1000 : tile.track.duration)
                                            color: Theme.wallInkSecondary
                                            font.family: Theme.fontUi; font.pixelSize: 11; font.features: { "tnum": 1 }
                                            renderType: Text.QtRendering
                                        }
                                        WallControl {
                                            glyph: "arrow-up-right"; enabled: !!music.current.hash
                                            Accessible.name: "打开播放页"; onClicked: root.player()
                                        }
                                    }
                                    Slider {
                                        id: wallSeek
                                        width: parent.width; implicitHeight: Theme.s6; padding: 0
                                        from: 0; to: tile.activeSong ? Math.max(1, music.duration) : 1
                                        value: tile.activeSong ? music.position : 0
                                        enabled: tile.activeSong && music.duration > 0
                                        onMoved: music.seek(value)
                                        Accessible.name: "海报播放进度"
                                        background: Item {
                                            Rectangle {
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: parent.width; height: 4; radius: 2; color: Qt.rgba(1, 1, 1, 0.3)
                                                Rectangle { width: wallSeek.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.wallInk }
                                            }
                                        }
                                        handle: Rectangle {
                                            x: wallSeek.visualPosition * (wallSeek.availableWidth - width)
                                            y: (wallSeek.height - height) / 2
                                            width: 14; height: 14; radius: 7; color: Theme.wallInk
                                            opacity: wallSeek.enabled ? 1 : 0.5
                                        }
                                    }
                                    Text {
                                        visible: !root.canPlay(tile.queueAddress)
                                        text: "红心模式会继续补充推荐"; color: Theme.wallInkSecondary
                                        font.family: Theme.fontUi; font.pixelSize: 11
                                    }
                                }
                                // .is-current: inset 2px ring in --wall-current-border.
                                Rectangle {
                                    anchors.fill: parent; visible: tile.activeSong
                                    color: "transparent"
                                    border.color: Theme.wallCurrentBorder; border.width: 2
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // --wall-vignette: radial-gradient(ellipse at center, transparent 68%, black 30% at 100%).
    // A square shape stretched to the viewport turns the circle into the CSS
    // farthest-corner ellipse.
    Shape {
        width: root.width; height: root.width
        transform: Scale { yScale: root.height / Math.max(1, root.width) }
        ShapePath {
            strokeWidth: -1
            fillGradient: RadialGradient {
                centerX: root.width / 2; centerY: root.width / 2; focalX: centerX; focalY: centerY
                centerRadius: root.width / 2 * Math.SQRT2; focalRadius: 0
                GradientStop { position: 0.68; color: "transparent" }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.30) }
            }
            PathRectangle { width: root.width; height: root.width }
        }
    }

    // .poster-wall-header: round 40px back button + source label.
    Row {
        x: root.inset; y: root.inset; spacing: Theme.s3
        GlassButton {
            objectName: "wallBack"; glyph: "chevron-left"; round: true
            Accessible.name: "返回幕布舞台"; onClicked: root.back()
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter; spacing: 3
            Text {
                text: music.radio ? "红心推荐" : root.source === "recommended" ? "推荐预览" : "播放队列"
                color: Theme.wallInk; font.family: Theme.fontUi; font.pixelSize: 12
                style: Text.Sunken; styleColor: Theme.wallHeaderShadow
            }
            Text {
                text: "海报墙"; color: Theme.wallInkMuted; font.family: Theme.fontUi; font.pixelSize: 10
                style: Text.Sunken; styleColor: Theme.wallHeaderShadow
            }
        }
    }

    // .wall-now-playing: 216px glass card, 44px art, 10px/12px/10px stack.
    AbstractButton {
        id: nowPlaying
        visible: !!music.current.hash
        x: root.inset; y: root.height - height - root.inset
        width: 216; height: Math.max(64, nowColumn.implicitHeight + 20)
        hoverEnabled: true
        Accessible.name: "定位当前歌曲"
        onClicked: root.focusCurrent()
        background: Glass { lit: nowPlaying.hovered }
        contentItem: Item {
            Cover {
                id: nowArt; x: 10; anchors.verticalCenter: parent.verticalCenter
                width: 44; height: 44; radius: 5; pixels: 200
                color: Theme.wallSoftBg; glyphScale: 24 / 44; glyphColor: Theme.wallMuted
                url: music.current.image || ""
            }
            Column {
                id: nowColumn
                x: nowArt.x + nowArt.width + 10; width: parent.width - x - 10
                anchors.verticalCenter: parent.verticalCenter; spacing: 3
                Text {
                    width: parent.width; text: music.radio ? "红心推荐" : "正在聆听"
                    color: Theme.wallInkMuted; font.family: Theme.fontUi; font.pixelSize: 10; elide: Text.ElideRight
                }
                Text {
                    width: parent.width; text: music.current.title || ""
                    color: Theme.wallInk; font.family: Theme.fontUi; font.pixelSize: 12; font.weight: Theme.fwSemiBold; elide: Text.ElideRight
                }
                Text {
                    width: parent.width; text: music.current.artist || ""
                    color: Theme.wallInkMuted; font.family: Theme.fontUi; font.pixelSize: 10; elide: Text.ElideRight
                }
            }
        }
    }
    // .wall-preview-note when nothing is playing yet.
    Glass {
        visible: !nowPlaying.visible && root.tracks.length > 0
        x: root.inset; y: root.height - height - root.inset
        width: previewColumn.implicitWidth + 28; height: previewColumn.implicitHeight + 24
        Column {
            id: previewColumn; anchors.centerIn: parent; spacing: 5
            Text { text: "推荐预览 · " + root.tracks.length + " 首"; color: Theme.wallInk; font.family: Theme.fontUi; font.pixelSize: 12 }
            Text { text: "点击海报展开，再点播放"; color: Theme.wallInkMuted; font.family: Theme.fontUi; font.pixelSize: 10 }
        }
    }

    // .wall-tools: locate + lights, stacked 4.25rem (4rem when compact) above the floor.
    Column {
        anchors.right: parent.right; anchors.rightMargin: root.inset
        anchors.bottom: parent.bottom; anchors.bottomMargin: root.compact ? 64 : 68
        spacing: Theme.s2
        GlassButton {
            objectName: "wallLocate"; glyph: "locate-fixed"; enabled: root.currentIndex >= 0
            Accessible.name: "定位当前歌曲"; onClicked: root.focusCurrent()
        }
        GlassButton {
            objectName: "wallLights"; glyph: root.lights ? "eye" : "eye-off"; checkable: true; checked: !root.lights
            Accessible.name: root.lights ? "熄灯" : "亮灯"; onClicked: root.lights = !root.lights
        }
    }
    // CurtainPanel trigger in wall mode: 40px glass square, sliders glyph.
    GlassButton {
        objectName: "wallListen"; glyph: "sliders-horizontal"; glyphSize: 16
        anchors.right: parent.right; anchors.rightMargin: root.inset
        anchors.bottom: parent.bottom; anchors.bottomMargin: root.inset
        Accessible.name: "展开聆听面板"; onClicked: root.listen()
    }
    // A new queue or song collapses the selection and re-centres the current poster.
    // A page-owned timer (not Qt.callLater) so nothing fires after the page unmounts.
    Timer { id: refocusTimer; interval: 0; onTriggered: root.focusCurrent(false) }
    function refocus() { selected = null; refocusTimer.restart() }
    onTracksChanged: refocus()
    onCurrentIndexChanged: refocus()
    Component.onCompleted: field.settle()
}
