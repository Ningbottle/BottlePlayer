import QtQuick
import QtQuick.Layouts
import BottleMusic

// Stable transport targets; the solid glyph has its own optical offset.
Row {
    id: root
    spacing: Theme.s3
    property alias playButton: playBtn

    IconButton {
        objectName: "prevButton"; media: "prev"; mediaBox: 36; mediaGlyph: 20; anchors.verticalCenter: parent.verticalCenter; tint: Theme.text
        enabled: music.queueIndex >= 0 && !music.radio
        Accessible.name: "上一首"; onClicked: music.previous()
    }
    IconButton {
        id: playBtn
        objectName: "playButton"
        variant: "primary"; radius: Theme.rFull; mediaBox: 44; mediaGlyph: 22; anchors.verticalCenter: parent.verticalCenter
        primaryFill: Theme.playBg; primaryHoverFill: Theme.playHoverBg; primaryDownFill: Theme.playPressedBg
        tint: Theme.inkOn(down ? primaryDownFill : lit ? primaryHoverFill : primaryFill); hoverTint: tint
        media: music.playing ? "pause" : "play"
        enabled: !music.loading && (music.queueIndex >= 0 || music.feed.length > 0)
        scale: 1

        Accessible.name: music.playing ? "暂停" : "播放"; onClicked: music.toggle()
        shadowLevel: 4
    }
    IconButton {
        objectName: "nextButton"; media: "next"; mediaBox: 36; mediaGlyph: 20; anchors.verticalCenter: parent.verticalCenter; tint: Theme.text
        enabled: music.queueIndex >= 0
        Accessible.name: "下一首"; onClicked: music.next()
    }
}
