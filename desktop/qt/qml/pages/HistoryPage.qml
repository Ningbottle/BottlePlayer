import QtQuick
import QtQuick.Controls
import BottleMusic

Item {
    id: root
    objectName: "historyPage"
    signal addToPlaylist(var track)
    Component.onCompleted: historyState.setActive(true)
    Component.onDestruction: historyState.setActive(false)
    readonly property real inset: width <= 700 ? 24 : 32
    Column {
        id: head; x: root.inset; y: 24; width: parent.width - x * 2; spacing: 12
        CssText { text: "LISTENING JOURNAL / RECENT"; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; color: Theme.text3; font.letterSpacing: 1 }
        Row {
            width: parent.width
            CssText { width: parent.width - refresh.width; text: "最近播放"; font.family: Theme.fontDisplay; font.pixelSize: Theme.fs2xl; color: Theme.text }
            ActionButton { id: refresh; objectName: "historyRefresh"; text: historyState.loading ? "同步中…" : "刷新"; glyph: "refresh-cw"; enabled: !historyState.loading; onClicked: historyState.refresh() }
        }
        CssText { width: parent.width; text: "在这里，重新遇见听过的旋律。"; font.family: Theme.fontMeta; font.italic: Theme.metaItalic; font.pixelSize: Theme.fsSm; color: Theme.text3 }
        Segmented { objectName: "historyScope"; value: historyState.scope; options: [{value:"week",label:"本周"},{value:"month",label:"本月"},{value:"all",label:"全部"}]; onSelected: value => historyState.scope = value }
        Hairline { width: parent.width; color: Theme.ruleInk }
        CssText { visible: historyState.error.length > 0; width: parent.width; text: historyState.error; wrapMode: Text.Wrap; color: Theme.warning; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
    }
    TrackRows {
        objectName: "historyTracks"; x: root.inset; y: head.y + head.height + 20
        width: parent.width - x * 2; height: Math.max(0, parent.height - y - 24)
        tracks: historyState.tracks; showDate: true
        onPlay: index => historyState.play(index)
        onAddToPlaylist: track => root.addToPlaylist(track)
    }
    Text {
        visible: historyState.tracks.length === 0 && !historyState.loading
        anchors.centerIn: parent; text: "播放一首歌，从这里开始记录。"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsMd
    }
}
