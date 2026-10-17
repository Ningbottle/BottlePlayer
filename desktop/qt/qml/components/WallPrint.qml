import QtQuick
import QtQuick.Controls
import BottleMusic

// Folia 7701347 PolaroidCard: inset square artwork, printed title/artist,
// album/duration and a round action within the paper frame. Size the parts
// directly rather than scaling a texture containing native-rendered text.
Item {
    id: card
    property var track: ({})
    property real s: 1
    property real baseWidth: 220
    property bool current: false
    property bool playing: false
    property bool busy: false
    property bool playEnabled: true
    property bool compact: false
    readonly property bool detail: s >= 0.95
    readonly property real labelScale: ((track.title || "").length + (track.artist || "").length + (track.album || "").length) > 100 ? 1.18
        : ((track.title || "").length + (track.artist || "").length + (track.album || "").length) > 65 ? 1.12
        : ((track.title || "").length + (track.artist || "").length + (track.album || "").length) > 35 ? 1.06 : 1
    readonly property real pad: Theme.snap(12 * s)
    readonly property real labelGap: Theme.snap(4 * s)
    signal picked()
    signal play()
    width: Theme.snap(baseWidth * s * labelScale)
    height: Theme.snap(Math.max(width * 1.5, labels.y + labels.height + pad / 2 + Math.max(26 * s, detail ? 44 : 0) + pad))
    Accessible.role: Accessible.Button
    Accessible.name: "查看 " + (track.title || "") + "，" + (track.artist || "")
    activeFocusOnTab: true
    PixelSnap.enabled: true
    Keys.onReturnPressed: picked()
    Keys.onEnterPressed: picked()
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: card.picked() }
    Plate {
        objectName: card.detail ? "curtainFocusedPaper" : ""
        anchors.fill: parent; color: Theme.stageCard; radius: Theme.snap(12 * card.s)
        borderColor: Theme.stageCardBorder; elevation: 2
    }
    Cover {
        id: art
        objectName: card.detail ? "curtainFocusedArt" : ""
        x: card.pad; y: card.pad; width: card.width - card.pad * 2; height: width
        radius: Theme.snap(8 * card.s); pixels: card.detail ? 800 : 400
        url: card.track.image || ""; title: card.track.title || ""
    }
    Column {
        id: labels
        x: card.pad; y: art.y + art.height + card.pad; width: art.width; spacing: card.labelGap
        PixelSnap.enabled: true
        Text {
            width: parent.width
            height: Math.min(implicitHeight, Math.round(24 * card.s) * (card.compact ? 2 : 4))
            text: card.track.title || ""; textFormat: Text.PlainText
            color: Theme.text; font.family: Theme.fontUi; font.pixelSize: Math.round(16 * card.s); font.weight: Theme.fwBold
            renderType: Text.NativeRendering
            lineHeightMode: Text.FixedHeight; lineHeight: Math.round(24 * card.s)
            wrapMode: Text.Wrap; maximumLineCount: card.compact ? 2 : 4; elide: Text.ElideRight
        }
        Text {
            width: parent.width; height: Theme.snap(15 * card.s)
            text: card.track.artist || ""
            textFormat: Text.PlainText; elide: Text.ElideRight
            color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Math.round(10 * card.s)
            renderType: Text.NativeRendering
        }
    }
    Column {
        x: card.pad; y: card.height - card.pad - height
        width: card.width - card.pad * 2 - (card.detail ? playButton.width + card.labelGap : 0)
        spacing: 0; PixelSnap.enabled: true
        Text {
            width: parent.width; height: Math.round(13 * card.s)
            text: card.track.album || ""; textFormat: Text.PlainText; elide: Text.ElideRight
            color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Math.round(9 * card.s)
            renderType: Text.NativeRendering
        }
        Text {
            height: Math.round(13 * card.s)
            text: Theme.duration(card.track.duration || 0); color: Theme.text3
            font.family: Theme.fontUi; font.pixelSize: Math.round(9 * card.s); font.features: { "tnum": 1 }
            renderType: Text.NativeRendering
        }
    }
    IconButton {
        id: playButton
        visible: card.detail
        x: card.width - card.pad - width; y: card.height - card.pad - height; width: 44; height: 44
        glyph: card.current && card.playing ? "pause" : "play"; glyphSize: 20
        radius: 22; variant: "secondary"
        tint: Theme.text; hoverTint: Theme.text
        enabled: card.playEnabled && !(card.busy && card.current)
        Accessible.name: (card.current && card.playing ? "暂停 " : "播放 ") + (card.track.title || "")
        onClicked: card.play()
    }
    Rectangle {
        anchors.fill: parent; radius: Theme.snap(12 * card.s); color: "transparent"
        visible: card.activeFocus; border.width: 2; border.color: Theme.focus
    }
}
