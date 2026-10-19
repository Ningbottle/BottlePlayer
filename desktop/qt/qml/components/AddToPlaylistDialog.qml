import QtQuick
import QtQuick.Controls
import BottleMusic

Popup {
    id: root
    objectName: "addToPlaylistDialog"
    property var track: ({})
    modal: true; focus: true; anchors.centerIn: parent
    width: Math.min(440, parent.width - 48); height: Math.min(480, parent.height - 80)
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.bgElevated; radius: Theme.rLg; border.color: Theme.borderStrong; border.width: Theme.hairline }
    onOpened: libraryState.refreshPlaylists()
    contentItem: Column {
        spacing: 16
        Row {
            width: parent.width
            Text { width: parent.width - close.width; text: "添加到歌单"; font.family: Theme.fontTitle; font.pixelSize: Theme.fsXl; color: Theme.text }
            IconButton { id: close; size: "sm"; glyph: "x"; onClicked: root.close() }
        }
        Text { width: parent.width; text: root.track.title || ""; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Theme.fontMeta; font.pixelSize: Theme.fsSm; color: Theme.text3 }
        Text { visible: !accountState.loggedIn; text: "请先登录，再添加到个人歌单。"; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.text2 }
        Text { visible: libraryState.playlistsLoading; text: "正在读取个人歌单…"; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.text3 }
        Text { visible: libraryState.playlistsError.length > 0; width: parent.width; text: libraryState.playlistsError; wrapMode: Text.Wrap; color: Theme.warning; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
        ListView {
            id: dialogList
            width: parent.width; height: Math.max(0, root.availableHeight - y); clip: true
            SnapSettle { flickable: dialogList }
            model: libraryState.playlists
            ScrollBar.vertical: ThinScrollBar { }
            delegate: AbstractButton {
                required property var modelData
                required property int index
                objectName: "addToPlaylist_" + index
                width: ListView.view.width; height: 52; hoverEnabled: true
                enabled: accountState.loggedIn && /^[0-9]+$/.test(String(modelData.listid || ""))
                background: Rectangle { color: parent.hovered ? Theme.surfaceHover : "transparent"; radius: Theme.rSm }
                contentItem: Text { text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd; color: Theme.text }
                onClicked: { favoriteState.addToPlaylist(modelData, root.track); root.close() }
            }
        }
    }
}
