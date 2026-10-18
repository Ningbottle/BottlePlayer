import QtQuick
import QtQuick.Controls
import BottleMusic

ListView {
    id: root
    property var tracks: []
    property bool showDate: false
    signal play(int index)
    signal addToPlaylist(var track)
    signal removeTrack(var track)
    property bool removable: false
    model: tracks
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: height
    ScrollBar.vertical: ThinScrollBar { }
    delegate: AbstractButton {
        id: row
        required property var modelData
        required property int index
        width: root.width; height: Theme.compactList ? 48 : 62
        hoverEnabled: true; focusPolicy: Qt.TabFocus
        onClicked: root.play(index)
        Accessible.name: modelData.title + "，" + modelData.artist
        background: Rectangle {
            color: row.hovered || row.visualFocus ? Theme.surfaceHover : String(row.modelData.hash) === String(music.current.hash) ? Theme.accentSoft : "transparent"
            radius: Theme.rSm
        }
        contentItem: Item {
            Text {
                x: 8; width: 28; anchors.verticalCenter: parent.verticalCenter
                text: String(row.index + 1).padStart(2, "0"); color: Theme.text3
                font.family: Theme.fontMono; font.pixelSize: Theme.fsXs
            }
            Cover { x: 44; anchors.verticalCenter: parent.verticalCenter; width: 40; height: 40; pixels: 80; url: row.modelData.image || "" }
            Column {
                x: 100; anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - x - actions.width - 24); spacing: 4
                Text {
                    width: parent.width; text: row.modelData.title || "未知歌曲"; textFormat: Text.PlainText; elide: Text.ElideRight
                    color: String(row.modelData.hash) === String(music.current.hash) ? Theme.accent : Theme.text
                    font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd; font.weight: Theme.fwMedium
                }
                Text {
                    width: parent.width; text: (row.modelData.artist || "未知歌手") + (row.modelData.album ? " · " + row.modelData.album : "")
                    textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.text3
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                }
            }
            Row {
                id: actions; anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                Text {
                    visible: root.showDate && root.width >= 680
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.playedAt > 0 ? Qt.formatDateTime(new Date(Number(row.modelData.playedAt)), "MM/dd HH:mm") : "时间未知"
                    font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; color: Theme.text3
                }
                Text { anchors.verticalCenter: parent.verticalCenter; text: music.timeText(Number(row.modelData.duration || 0) * 1000); color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.features: { "tnum": 1 } }
                IconButton { size: "sm"; glyph: "plus"; Accessible.name: "添加到歌单"; onClicked: root.addToPlaylist(row.modelData) }
                IconButton { visible: root.removable; size: "sm"; glyph: "x"; Accessible.name: "从歌单移除"; onClicked: root.removeTrack(row.modelData) }
            }
        }
    }
}
