import QtQuick
import QtQuick.Controls
import BottleMusic

// Recommendation tile for the curtain: --card-radius surface + --shadow-1,
// hand-set (WenKai) 1rem title for album recommendations, .75rem meta.
AbstractButton {
    id: root
    property var track: ({})
    property bool card: false
    property bool playEnabled: true
    property bool compact: false
    signal play()
    implicitHeight: compact ? 64 : width + 76
    padding: 0; hoverEnabled: true; focusPolicy: Qt.TabFocus
    Accessible.name: "播放 " + (track.title || "歌曲") + "，" + (track.artist || "")
    onClicked: if (playEnabled) play()
    background: Rectangle {
        radius: Theme.rSm; color: root.hovered || root.visualFocus ? Theme.surfaceHover : "transparent"
        border.width: root.visualFocus ? 2 : 0; border.color: Theme.focus
    }

    Plate {
        anchors.fill: parent
        visible: root.card
        color: Theme.bgElevated
        radius: Theme.rCard
        elevation: 2
    }
    Cover {
        id: image
        x: root.card ? Theme.s3 : 0; y: root.compact ? 8 : root.card ? Theme.s3 : 0
        width: root.compact ? 48 : root.width - (root.card ? Theme.s3 * 2 : 0); height: width
        radius: root.card ? Theme.rSm : Theme.rMd
        url: root.track.image || ""; title: root.track.title || ""
    }
    Text {
        x: root.compact ? 64 : image.x; y: root.compact ? 10 : image.y + image.height + Theme.rem(0.85)
        width: root.width - x - (root.compact ? 48 : Theme.controlSm + Theme.s1)
        text: root.track.title || "等待推荐"; color: Theme.text
        font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; font.weight: Theme.fwMedium
        font.preferTypoLineMetrics: Theme.lyricTypoMetrics
        elide: Text.ElideRight
    }
    Text {
        x: root.compact ? 64 : image.x; y: root.compact ? 36 : image.y + image.height + Theme.rem(0.85) + 22
        width: root.width - x - (root.compact ? 48 : Theme.controlSm + Theme.s1)
        text: root.track.artist || ""; color: Theme.text2
        font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75)); elide: Text.ElideRight
    }
    IconButton {
        anchors.right: parent.right; anchors.rightMargin: root.card ? Theme.s3 : 0
        y: root.compact ? 12 : image.y + image.height + Theme.s3
        implicitWidth: root.compact ? 40 : Theme.controlSm; implicitHeight: root.compact ? 40 : Theme.controlSm
        size: "sm"; glyph: "play"
        enabled: root.playEnabled
        Accessible.name: "播放 " + (root.track.title || "歌曲")
        onClicked: root.play()
    }
}
