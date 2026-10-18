import QtQuick
import QtQuick.Controls
import QtQml.Models
import QtQuick.Effects
import BottleMusic

Item {
    id: root
    objectName: "lyricView"
    property bool immersive: false
    property real viewportHeight: height
    property bool centered: false
    property real textSize: immersive ? music.lyricSize : Theme.playerLyricSize * Theme.pageLyricScale(music.lyricSize) * (width <= 380 ? 0.82 : 1)
    property real leadingInset: -1
    property real trailingInset: -1
    property bool alignTrailing: false
    property real focusY: -1
    readonly property real listHeight: lines.height
    readonly property real focusLine: focusY >= 0 ? focusY : lines.height/2
    readonly property real rowHalf: immersive ? textSize*0.74+3 : textSize*0.75+4
    readonly property real horizontalInset: leadingInset >= 0 ? leadingInset : immersive ? 0 : Theme.lyricFreeInset(width)
    readonly property real rightInset: trailingInset >= 0 ? trailingInset : immersive ? 0 : width <= 380 ? Theme.s4 : Theme.s8
    property bool followPlayback: true
    property bool positioning: false
    property bool modelReady: false
    readonly property bool hasLyrics: lyricModel.count > 0
    ListModel { id: lyricModel }
    LyricGeometry { id: lyricGeometry }
    property real displayPosition: music.position
    property double sampledAt: Date.now()
    function sampleClock() { displayPosition = music.position; sampledAt = Date.now() }
    Timer {
        objectName: "lyricFillClock"
        interval: 33; repeat: true
        running: root.visible && root.Window.window && root.Window.window.active && root.Window.window.visibility !== Window.Minimized && root.hasLyrics && music.playing && !music.loading && !music.reducedMotion
        onTriggered: root.displayPosition = music.position + Math.max(0, Math.min(200, Date.now() - root.sampledAt))
    }
    function syncLyrics() {
        modelReady = false
        lyricModel.clear()
        // Detach Qt's reference sequence while handling lyricsChanged. Reading
        // an alias's length/index would otherwise call its C++ getter again.
        // The fill clock only needs this row's cached boundary.
        const rows = music.lyrics.slice()
        for (let index = 0; index < rows.length; ++index) {
            const row = rows[index]
            lyricModel.append({ lyric: String(row.text || ""), translated: String(row.translation || ""), timestamp: Number(row.time), nextTimestamp: index + 1 < rows.length ? Number(rows[index + 1].time) : -1, timedWords: JSON.stringify(row.words || []) })
        }
        modelReady = true
        reflow.restart()
    }
    Component.onCompleted: syncLyrics()
    function resumeFollow() {
        if (positioning || !modelReady) return
        followPlayback = true
        positioning = true
        // Resetting the ListModel can set ListView's index to zero even when
        // the audio cue did not change. Re-establish its audio-owned binding.
        if (lines.currentIndex !== music.lyricIndex)
            lines.currentIndex = Qt.binding(function() { return music.lyricIndex })
        if (lines.currentIndex < 0) { positioning=false; return }
        if (!lines.currentItem) {
            lines.positionViewAtIndex(lines.currentIndex, ListView.Center); positioning = false
            if (root.visible && lines.width>0 && lines.height>0) reflow.restart()
            return
        }
        // Read the laid-out delegate after a width/font change. Index-based
        // positioning can retain the old origin while its cached rows reflow.
        const target = lines.currentItem.y + lines.currentItem.height / 2 - root.focusLine
        const bounded = Math.max(lines.originY, Math.min(target, Math.max(lines.originY, lines.originY + lines.contentHeight - lines.height)))
        if (immersive && !music.reducedMotion) {
            if (!followMotion.running || Math.abs(followMotion.to-bounded)>0.1) { followMotion.stop(); followMotion.to=bounded; followMotion.start() }
        } else { followMotion.stop(); lines.contentY=bounded }
        positioning = false
    }
    NumberAnimation { id: followMotion; target: lines; property: "contentY"; duration: 420; easing.type: Easing.OutCubic }
    Timer { id: resumeTimer; interval: root.immersive ? 5000 : 900; onTriggered: root.resumeFollow() }
    // Reflow after delegate geometry is committed, never from inside the
    // ListView's own content-height notification (which can leave stale rows).
    Timer {
        id: reflow; interval: 0
        onTriggered: { lines.forceLayout(); if (root.followPlayback) root.resumeFollow() }
    }
    onTextSizeChanged: reflow.restart()
    onFocusLineChanged: reflow.restart()
    ListView {
        id: lines
        objectName: root.immersive ? "immersiveLyricList" : "lyricList"
        x: root.horizontalInset; y: 0
        width: Math.max(0, parent.width - x - root.rightInset); height: Math.max(0, parent.height - (root.immersive ? 0 : 42)); clip: true
        model: root.modelReady ? lyricModel : null; currentIndex: music.lyricIndex
        spacing: root.immersive ? Math.max(24, root.textSize * 0.8) : Theme.s4 * (root.height <= 300 ? 0.6 : 1)
        cacheBuffer: height * 0.5
        header: Item { height: Math.max(0,root.focusLine-root.rowHalf) }
        footer: Item { height: Math.max(0,lines.height-root.focusLine-root.rowHalf) }
        // Audio owns the current index. StrictlyEnforceRange also changes it
        // during viewport/reflow changes, which can align a different cue.
        // Both presentations follow the measured current row instead.
        highlightRangeMode: ListView.NoHighlightRange
        onCurrentIndexChanged: if (root.followPlayback && currentIndex >= 0) root.resumeFollow()
        onHeightChanged: reflow.restart()
        onWidthChanged: reflow.restart()
        onContentHeightChanged: if (root.followPlayback && currentIndex >= 0) root.resumeFollow()
        onCurrentItemChanged: if (root.followPlayback && currentIndex >= 0) root.resumeFollow()
        // Automatic centering/resize also emits movementStarted. Only user
        // input suspends following; otherwise resize creates a resume loop.
        // Highlight/range positioning emits movementStarted too. Only a user
        // drag or flick should suspend following, in either presentation.
        onMovementStarted: if (!root.positioning && (dragging || flicking)) { followMotion.stop(); root.followPlayback = false; resumeTimer.restart() }
        onMovementEnded: if (!root.followPlayback) resumeTimer.restart()
        WheelHandler {
            target: null
            onWheel: event => { followMotion.stop(); root.followPlayback = false; resumeTimer.restart(); event.accepted = false }
        }
        delegate: Item {
            id: line
            required property string lyric
            required property string translated
            required property real timestamp
            required property real nextTimestamp
            required property string timedWords
            required property int index
            objectName: "lyricLine_" + index
            readonly property bool activeLine: index === music.lyricIndex
            readonly property int distance: Math.abs(index - Math.max(0, music.lyricIndex))
            readonly property real centerY: y - lines.contentY + height / 2
            readonly property bool fullyInside: y-lines.contentY >= -0.5 && y-lines.contentY+height <= lines.height+0.5
            readonly property real edgeOpacity: root.immersive
                ? Math.min(1, Math.max(0, centerY / 64), Math.max(0, (lines.height - centerY) / 72))
                : Theme.lyricFocus === "stage" ? Math.min(1, Math.max(0, centerY / Math.max(1, lines.height * 0.1)), Math.max(0, (lines.height - centerY) / Math.max(1, lines.height * 0.1))) : (activeLine || fullyInside ? 1 : 0)
            width: root.immersive ? lines.width : Math.min(lines.width, zero.advanceWidth * 48)
            x: !root.immersive && root.centered ? (lines.width-width)/2 : root.alignTrailing ? lines.width-width : 0
            height: words.height + (root.immersive ? 6 : Theme.s1 * 2)
            onHeightChanged: reflow.restart()
            opacity: edgeOpacity * (activeLine ? 1 : root.immersive ? (distance === 1 ? 0.53 : 0.32) : distance <= 1 ? 1 : distance <= 3 ? 0.8 : 0.5)
            Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : 280 } }
            Item {
                id: words
                objectName: "lyricFill_" + line.index
                x: root.immersive ? 0 : Theme.s3; y: root.immersive ? 0 : Theme.s1
                width: parent.width - x * 2
                readonly property real gap: root.immersive ? 10 : 6
                height: lyricText.height + (translation.visible ? gap + translation.height : 0)
                // The active line is never rendered into a scaled/blurred layer.
                layer.enabled: root.immersive && !line.activeLine && !music.reducedMotion && line.distance > 1
                layer.effect: MultiEffect { blurEnabled: true; blurMax: 8; blur: 0.32 }
                Text {
                    id: lyricText
                    objectName: line.activeLine ? "activeLyricText" : ""
                    width: parent.width; text: line.lyric; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    // Bind the qreal property directly. Qt.font(map) rounds
                    // pointSize instead of preserving the requested fractional size.
                    font.family: root.immersive && Theme.lyricFontSetting === "skin" ? Theme.fontUi : Theme.fontLyric
                    font.pointSize: root.textSize * 72 / Math.max(1, root.Screen.logicalPixelDensity * 25.4)
                    font.weight: root.immersive ? Font.DemiBold : Theme.fwRegular
                    font.letterSpacing: root.immersive ? 0 : root.textSize * (Theme.mint ? 0.01 : 0.02)
                    font.preferTypoLineMetrics: root.immersive || Theme.lyricTypoMetrics
                    lineHeightMode: root.immersive ? Text.ProportionalHeight : Text.FixedHeight
                    lineHeight: root.immersive ? 1.22 : root.textSize * 1.5
                    horizontalAlignment: !root.immersive && root.centered ? Text.AlignHCenter : Text.AlignLeft
                    topPadding: root.immersive ? 0 : Theme.lineBaseline(metrics.ascent, metrics.descent, root.textSize * 1.5) - metrics.ascent
                    bottomPadding: -topPadding
                    color: root.immersive ? "white" : line.activeLine ? Theme.text : line.distance === 1 ? Theme.text2 : Theme.text3
                    renderType: Text.NativeRendering
                    Accessible.name: text
                    FontMetrics { id: metrics; font: lyricText.font }
                    TextMetrics { id: zero; font: lyricText.font; text: "0" }
                }
                // The same native text is clipped over the base glyphs. Only
                // the active cue reads the fill clock; layout and translations
                // stay fixed, including wrapped CJK and English lines.
                readonly property var timing: JSON.parse(line.timedWords)
                readonly property var shaped: line.activeLine ? lyricGeometry.shape(line.lyric, lyricText.font, lyricText.width, root.immersive ? root.textSize * 1.22 : root.textSize * 1.5) : []
                readonly property real fillCursor: {
                    if (!line.activeLine) return 0
                    const next = line.nextTimestamp >= 0 ? line.nextTimestamp : Math.max(line.timestamp + 4000, music.duration)
                    const clock = root.displayPosition
                    if (!timing.length) return line.lyric.length * Math.max(0, Math.min(1, (clock - line.timestamp) / Math.max(50, next - line.timestamp)))
                    var cursor = 0
                    for (const word of timing) {
                        if (clock < Number(word.time)) break
                        const end = Number(word.end) > Number(word.time) ? Number(word.end) : next
                        cursor = Math.max(cursor, Number(word.start) + Number(word.length) * Math.max(0, Math.min(1, (clock - Number(word.time)) / Math.max(50, end - Number(word.time)))))
                    }
                    return cursor
                }
                readonly property var fillBoxes: {
                    var boxes = []
                    for (const shapedLine of shaped) {
                        if (fillCursor <= shapedLine.start) continue
                        const length = Math.min(shapedLine.end - shapedLine.start, fillCursor - shapedLine.start)
                        const whole = Math.floor(length), fraction = length - whole
                        const positions = shapedLine.positions
                        const edge = Number(positions[whole]) + (whole + 1 < positions.length ? (Number(positions[whole + 1]) - Number(positions[whole])) * fraction : 0)
                        const centerOffset = !root.immersive && root.centered ? (lyricText.width - Number(positions[positions.length - 1])) / 2 : 0
                        boxes.push({x: Math.min(Number(positions[0]), edge) + centerOffset, y: shapedLine.y, w: Math.abs(edge - Number(positions[0])), h: shapedLine.height})
                    }
                    return boxes
                }
                Repeater {
                    model: line.activeLine ? words.fillBoxes : []
                    Item {
                        required property var modelData
                        x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h; clip: true
                        Text {
                            x: -parent.x; y: -parent.y; width: lyricText.width
                            text: lyricText.text; textFormat: Text.PlainText; wrapMode: lyricText.wrapMode; font: lyricText.font
                            lineHeightMode: lyricText.lineHeightMode; lineHeight: lyricText.lineHeight
                            horizontalAlignment: lyricText.horizontalAlignment; topPadding: lyricText.topPadding; bottomPadding: lyricText.bottomPadding
                            color: Theme.accent; renderType: Text.NativeRendering
                            Accessible.ignored: true
                        }
                    }
                }
                Text {
                    id: translation; y: lyricText.height + words.gap
                    objectName: line.activeLine ? "activeLyricTranslation" : ""
                    width: parent.width; visible: text.length > 0
                    text: line.translated; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    font.family: Theme.fontUi; font.pixelSize: root.textSize * 0.58
                    font.weight: Font.Normal; lineHeight: 1.35
                    color: root.immersive ? "#d9ffffff" : Theme.text2
                    renderType: Text.NativeRendering
                }
            }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: { music.seek(line.timestamp); root.resumeFollow() }
            }
        }
    }
    Text {
        anchors.centerIn: parent; visible: !root.hasLyrics
        width: parent.width * 0.9; wrapMode: Text.Wrap
        text: music.loading ? "正在寻找歌词…" : root.immersive ? "让旋律，自由流淌。\n\n这首歌暂无同步歌词" : "暂无歌词，继续享受音乐"
        horizontalAlignment: root.immersive ? Text.AlignLeft : Text.AlignHCenter
        font.family: Theme.fontUi; font.pixelSize: root.immersive ? 24 : Theme.fsMd
        color: root.immersive ? "#bdffffff" : Theme.text2; lineHeight: 1.5
    }
    IconButton {
        objectName: "lyricResumeButton"
        x: root.immersive ? parent.width - width - 8 : (parent.width - width) / 2
        y: parent.height - height - 4
        visible: !root.followPlayback && root.hasLyrics
        glyph: "locate-fixed"; Accessible.name: "回到当前歌词"
        tint: root.immersive ? "white" : Theme.text2
        hoverTint: tint; hoverFill: root.immersive ? "#25ffffff" : Theme.surfaceHover
        onClicked: { resumeTimer.stop(); root.resumeFollow() }
        ToolTip.visible: hovered; ToolTip.text: "回到当前歌词"
    }
    Connections {
        target: music
        function onLyricsChanged() { resumeTimer.stop(); root.followPlayback = true; root.syncLyrics() }
        function onPositionChanged() { root.sampleClock() }
        function onPlaybackChanged() { root.sampleClock() }
        function onAppearanceChanged() { if (music.reducedMotion && root.followPlayback) root.resumeFollow() }
    }
}
