import QtQuick
import QtQuick.Controls
import BottleMusic

Column {
    id: root
    property var collections: []
    property string title: "精选歌单"
    signal openPlaylist(var playlist)
    signal openAlbum(var album)
    property int offset: 0
    readonly property var shown: collections.slice(offset).concat(collections.slice(0, offset)).slice(0, 12)
    spacing: 20
    Row {
        width: parent.width
        CssText { width: parent.width - refresh.width; text: root.title; color: Theme.text; font.family: Theme.fontTitle; font.pixelSize: Theme.fsXl }
        ActionButton { id: refresh; text: root.collections.length > 12 ? "换一批" : "刷新"; glyph: "refresh-cw"; size: "sm"; quiet: true; enabled: !homeState.loading; onClicked: { if (root.collections.length > 12) root.offset = (root.offset + 12) % root.collections.length; else homeState.refresh() } }
    }
    Grid {
        id: grid; width: parent.width; columns: Math.max(2, Math.floor((width + 20) / 160)); spacing: 20
        Repeater {
            model: root.shown
            AbstractButton {
                id: card
                required property var modelData
                required property int index
                objectName: "homeCollection_" + root.title + "_" + index
                width: (grid.width - grid.spacing * (grid.columns - 1)) / grid.columns; height: width + 56
                hoverEnabled: true; onClicked: modelData.album ? root.openAlbum(modelData) : root.openPlaylist(modelData)
                contentItem: Column {
                    spacing: 8
                    Cover { width: card.width; height: width; pixels: 300; url: card.modelData.image || ""; title: card.modelData.name || "" }
                    Text { width: parent.width; text: card.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd; color: card.hovered ? Theme.accent : Theme.text }
                    Text { width: parent.width; elide: Text.ElideRight; text: card.modelData.artist || (Number(card.modelData.count || 0) > 0 ? card.modelData.count + " 首" : "发现新的旋律"); font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; color: Theme.text3 }
                }
            }
        }
    }
    Text { visible: root.collections.length === 0; text: homeState.loading ? "正在读取推荐歌单…" : "推荐歌单暂不可用，可刷新重试"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsSm }
}
