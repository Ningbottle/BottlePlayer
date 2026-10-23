import QtQuick
import QtQuick.Layouts
import BottleMusic

// PlayerBar transport: 34px ghost skips with filled 18px MediaGlyphs in
// --text, a 40px --play-bg circle with a 22px --play-fg glyph (Aurora: accent
// with --fx-glow; Mint: rule ink, no glow) that grows 6% on hover.
Row {
    id: root
    spacing: Theme.s3
    property alias playButton: playBtn

    IconButton {
        objectName: "prevButton"; media: "prev"; mediaBox: 34; mediaGlyph: 18; anchors.verticalCenter: parent.verticalCenter; tint: Theme.text
        enabled: music.queueIndex >= 0 && !music.radio
        Accessible.name: "上一首"; onClicked: music.previous()
    }
    IconButton {
        id: playBtn
        objectName: "playButton"
        variant: "primary"; radius: Theme.rFull; mediaBox: 44; mediaGlyph: 22; anchors.verticalCenter: parent.verticalCenter
        primaryFill: Theme.playBg; primaryHoverFill: Theme.playHoverBg; primaryDownFill: Theme.playPressedBg
        tint: Theme.playFg; hoverTint: Theme.playFg
        media: music.playing ? "pause" : "play"
        enabled: !music.loading && (music.queueIndex >= 0 || music.feed.length > 0)
        scale: pressed ? Theme.pressScale : hovered && enabled ? 1.06 : 1
        Behavior on scale { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeBounce } }
        Accessible.name: music.playing ? "暂停" : "播放"; onClicked: music.toggle()
        shadowLevel: 4
    }
    IconButton {
        objectName: "nextButton"; media: "next"; mediaBox: 34; mediaGlyph: 18; anchors.verticalCenter: parent.verticalCenter; tint: Theme.text
        enabled: music.queueIndex >= 0
        Accessible.name: "下一首"; onClicked: music.next()
    }
}
