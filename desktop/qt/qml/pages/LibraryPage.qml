import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

Item {
    id: root; objectName: "libraryPage"
    property bool catalog: true
    signal openPlaylist(string id)
    signal openAccount()
    signal addToPlaylist(var track)
    readonly property var currentPlaylist: libraryState.playlists.find(row => row.id === libraryState.playlistId) || ({})
    readonly property real inset: width <= 700 ? Theme.s5 : Theme.s8
    Column {
        id: head; x: root.inset; y: Theme.s6; width: parent.width - x * 2; spacing: Theme.s3
        Text { text: "BOTTLEMUSIC / LIBRARY"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.letterSpacing: 1.2 }
        Text { width: parent.width; text: root.catalog ? "我的歌单" : libraryState.playlistName || (libraryState.album ? "专辑" : "个人歌单"); textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: Theme.fs2xl }
        Row {
            spacing: Theme.s2
            ActionButton { objectName: "libraryRefresh"; visible: root.catalog; text: "刷新歌单"; glyph: "refresh-cw"; enabled: accountState.loggedIn && !libraryState.playlistsLoading; onClicked: libraryState.refreshPlaylists() }
            ActionButton { objectName: "playlistPlayAll"; visible: !root.catalog; text: "播放全部"; media: "play"; glyphFilled: true; filled: true; enabled: libraryState.complete && libraryState.tracks.length > 0; onClicked: libraryState.play(0) }
            ActionButton { objectName: "playlistReload"; visible: !root.catalog; text: "重新加载"; glyph: "refresh-cw"; enabled: (accountState.loggedIn || libraryState.publicPlaylist) && !libraryState.loading; onClicked: libraryState.retryTracks() }
            ActionButton { objectName: "playlistStopLoading"; visible: !root.catalog && libraryState.loading; text: "停止加载"; quiet: true; onClicked: libraryState.cancelTracks() }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
            text: !accountState.loggedIn && !libraryState.publicPlaylist ? "登录后查看个人歌单"
                : root.catalog ? (libraryState.playlistsLoading ? "正在读取歌单…" : libraryState.playlists.length + " 个歌单")
                : "已加载 " + libraryState.tracks.length + (libraryState.total > 0 ? " / " + libraryState.total : "") + " 首" + (libraryState.loading ? "，正在继续加载…" : !libraryState.complete ? "，尚未完整加载" : "")
        }
        Text {
            visible: text.length > 0; width: parent.width; wrapMode: Text.Wrap; color: Theme.danger; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
            text: root.catalog ? libraryState.playlistsError : libraryState.error
        }
        Hairline { width: parent.width; color: Theme.ruleInk }
    }
    ListView {
        id: rows; objectName: "libraryRows"
        x: root.inset; y: head.y + head.height + Theme.s4
        width: parent.width - x * 2; height: Math.max(0, parent.height - y - Theme.s5); clip: true
        model: root.catalog ? libraryState.playlists : libraryState.tracks
        boundsBehavior: Flickable.StopAtBounds; cacheBuffer: 160
        ScrollBar.vertical: ThinScrollBar { areaHovered: hover.hovered }
        HoverHandler { id: hover }
        SmoothWheel { flickable: rows }
        delegate: AbstractButton {
            id: entry; required property var modelData; required property int index
            objectName: root.catalog ? "userPlaylist_" + index : "playlistTrack_" + index
            width: rows.width; height: 58; hoverEnabled: true; focusPolicy: Qt.TabFocus
            onClicked: root.catalog ? root.openPlaylist(modelData.id) : libraryState.play(index)
            Accessible.name: root.catalog ? modelData.name : modelData.title
            background: Rectangle { radius: Theme.rSm; color: entry.hovered || entry.visualFocus ? Theme.surfaceHover : "transparent" }
            contentItem: RowLayout {
                spacing: Theme.s3
                Cover { Layout.preferredWidth: 40; Layout.preferredHeight: 40; radius: Theme.rCard; url: entry.modelData.image || ""; title: root.catalog ? entry.modelData.name : entry.modelData.title; pixels: 100 }
                Column {
                    Layout.fillWidth: true; spacing: 2
                    Text { width: parent.width; text: root.catalog ? entry.modelData.name : entry.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.text; font.family: Theme.fontTitle; font.pixelSize: Theme.fsLg }
                    Text { width: parent.width; text: root.catalog ? entry.modelData.count + " 首" : entry.modelData.artist; textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs }
                }
                Text { text: root.catalog ? "" : Theme.duration(entry.modelData.duration); color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; Layout.rightMargin: Theme.s3 }
                Icon { visible: root.catalog; name: "chevron-right"; color: Theme.text2; Layout.rightMargin: Theme.s3 }
                IconButton { visible: !root.catalog; size: "sm"; glyph: "plus"; Accessible.name: "添加到歌单"; onClicked: root.addToPlaylist(entry.modelData) }
                IconButton {
                    visible: !root.catalog && !libraryState.album && /^[0-9]+$/.test(String(root.currentPlaylist.listid || ""))
                    size: "sm"; glyph: "x"; Accessible.name: "从歌单移除"
                    onClicked: { removeConfirmation.track = entry.modelData; removeConfirmation.playlist = root.currentPlaylist; removeConfirmation.owner = accountState.userId; removeConfirmation.open() }
                }
            }
        }
        footer: ActionButton { visible: root.catalog && libraryState.playlistsMore; text: "加载更多歌单"; enabled: !libraryState.playlistsLoading; onClicked: libraryState.morePlaylists() }
        Column {
            visible: rows.count === 0 && !(root.catalog ? libraryState.playlistsLoading : libraryState.loading)
            anchors.centerIn: parent; spacing: Theme.s3
            Text { text: accountState.loggedIn ? root.catalog ? "还没有个人歌单" : "这里还没有可播放的曲目" : "登录后，让你的歌单回到这里"; color: Theme.text2; font.family: Theme.fontTitle; font.pixelSize: Theme.fsLg }
            ActionButton { visible: !accountState.loggedIn; text: "登录账户"; filled: true; onClicked: root.openAccount() }
        }
    }
    ConfirmDialog {
        id: removeConfirmation; property var track: ({}); property var playlist: ({}); property string owner: ""
        title: "从歌单移除"
        message: "将「" + (removeConfirmation.track.title || "") + "」从当前歌单移除？"
        onAccepted: if (owner === accountState.userId) favoriteState.removeFromPlaylist(playlist, track)
    }
}
