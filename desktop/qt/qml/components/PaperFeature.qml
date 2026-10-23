import QtQuick
import QtQuick.Controls
import BottleMusic

// One reading order: recommendation, artwork and metadata, then actions.
PlateFrame {
    id: root
    objectName: "paperFeature"
    property var track: ({})
    property bool current: false
    property real viewportWidth: 1280
    property real minimumHeight: 0
    signal play()
    signal openPlayer()
    readonly property bool hasTrack: !!track.hash
    readonly property real inset: Theme.s6
    readonly property real artworkSize: hasTrack ? Theme.snap(Theme.clamp(160, width * .38, 208)) : 0
    implicitHeight: copy.implicitHeight + inset * 2
    height: Math.max(implicitHeight, minimumHeight)

    Column {
        id: copy
        x: root.inset; y: root.inset; width: parent.width - root.inset * 2
        spacing: Theme.s5
        Item {
            width: parent.width; height: 20
            Text {
                anchors.verticalCenter: parent.verticalCenter; text: "今日推荐"
                font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; color: Theme.text3
            }
            Text {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                text: root.current ? music.loading ? "正在加载" : music.playing ? "正在播放" : "已暂停" : ""
                visible: text.length > 0
                font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                color: root.current ? Theme.mark : Theme.text3
            }
        }
        Item {
            width: parent.width; height: Math.max(artwork.height, metadata.implicitHeight)
            Cover {
                id: artwork; objectName: "heroArtwork"
                visible: root.hasTrack; width: root.artworkSize; height: width
                radius: Theme.rXs; pixels: 400; url: root.track.image || ""; title: root.track.title || ""
            }
            Column {
                id: metadata
                x: artwork.visible ? artwork.width + Theme.s5 : 0
                width: parent.width - x; spacing: Theme.s2
                CssText {
                    objectName: "heroTitle"; width: parent.width
                    text: root.track.title || (music.loading ? "正在加载…" : "暂无推荐"); textFormat: Text.PlainText
                    font.family: Theme.fontDisplay; font.pixelSize: Theme.fs2xl
                    font.weight: Theme.fwDisplay; color: Theme.text
                    lineBox: font.pixelSize * 1.3; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 3; elide: Text.ElideRight
                }
                Text {
                    objectName: "heroArtist"; width: parent.width; visible: root.hasTrack
                    text: root.track.artist || "未知歌手"; textFormat: Text.PlainText
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; color: Theme.text2
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere; maximumLineCount: 2; elide: Text.ElideRight
                    lineHeightMode: Text.FixedHeight; lineHeight: 21
                }
                Text {
                    objectName: "heroAlbum"; width: parent.width
                    visible: root.hasTrack && !!text && text !== root.track.title
                    text: root.track.album || ""; textFormat: Text.PlainText
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; color: Theme.text3
                    elide: Text.ElideRight
                }
                Text {
                    visible: root.hasTrack && root.track.duration > 0
                    text: Theme.duration(root.track.duration)
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.features: {"tnum":1}; color: Theme.text3
                }
            }
        }
        Hairline { width: parent.width }
        Row {
            width: parent.width; spacing: Theme.s3
            ActionButton {
                objectName: "heroPlay"; visible: root.hasTrack
                size: "lg"; filled: true; implicitWidth: Math.max(112, implicitContentWidth + leftPadding + rightPadding)
                bgRadius: Theme.rSm; media: root.current && music.playing ? "pause" : "play"; glyphSize: 18
                text: root.current ? music.loading ? "正在加载…" : music.playing ? "暂停" : "继续播放" : "播放这首"
                enabled: !(root.current && music.loading && !music.playing)
                Accessible.name: text + " " + (root.track.title || "")
                onClicked: root.play()
            }
            ActionButton {
                objectName: "heroOpenPlayer"; visible: root.hasTrack
                size: "lg"; bgRadius: Theme.rSm; text: "查看歌词"; onClicked: root.openPlayer()
            }
            ActionButton {
                objectName: "heroRefresh"; visible: !root.hasTrack
                size: "lg"; text: "获取推荐"; enabled: !music.loading; onClicked: music.refreshFeed()
            }
        }
        Item {
            width: parent.width; height: radioButton.height
            ActionButton {
                id: radioButton; objectName: "heroRadio"
                readonly property bool running: music.radio && !!music.current.hash
                text: running ? "退出红心推荐" : music.radioPending ? "取消加载" : "红心推荐"
                quiet: true; glyph: "heart"; glyphFilled: running; glyphSize: 16
                implicitHeight: 36; leftPadding: 0; rightPadding: Theme.s2
                restInk: running ? Theme.radioAccent : Theme.text2
                restFill: "transparent"; enabled: music.ready
                onClicked: running || music.radioPending ? music.stopRadio() : music.startRadio()
            }
        }
    }
}
