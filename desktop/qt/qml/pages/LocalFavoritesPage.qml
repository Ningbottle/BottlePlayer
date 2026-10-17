import QtQuick
import BottleMusic

Item {
    id: root; objectName: "localFavoritesPage"
    signal addToPlaylist(var track)
    signal play(var tracks, int index)
    readonly property real inset: width <= 700 ? 24 : 32
    Column {
        id: head; x: root.inset; y: 24; width: parent.width - x * 2; spacing: 12
        CssText { text: "SAVED LOCALLY / FAVORITES"; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; color: Theme.text3; font.letterSpacing: 1 }
        CssText { text: "本地收藏"; font.family: Theme.fontDisplay; font.pixelSize: Theme.fs2xl; color: Theme.text }
        Text { width: parent.width; text: "访客期间的收藏保存在这台设备。登录后可在账户页同步到云歌单。"; wrapMode: Text.Wrap; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
        ActionButton { text: "播放全部"; media: "play"; filled: true; enabled: favoriteState.localTracks.length > 0; onClicked: root.play(favoriteState.localTracks, 0) }
        Hairline { width: parent.width; color: Theme.ruleInk }
    }
    TrackRows {
        objectName: "localFavoriteTracks"; x: root.inset; y: head.y + head.height + 20
        width: parent.width - x * 2; height: Math.max(0, parent.height - y - 24)
        tracks: favoriteState.localTracks; removable: true
        onPlay: index => root.play(tracks, index)
        onAddToPlaylist: track => root.addToPlaylist(track)
        onRemoveTrack: track => favoriteState.removeLocal(String(track.hash))
    }
    Text { visible: !favoriteState.localTracks.length; anchors.centerIn: parent; text: "喜欢一首歌，点亮它旁边的红心。"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsMd }
}
