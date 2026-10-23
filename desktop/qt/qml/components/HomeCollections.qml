import QtQuick
import QtQuick.Controls
import BottleMusic

Column {
    id: root
    property var collections: []
    property string title: "精选歌单"
    // The latin byline sits on the Chinese head's baseline ("编辑推荐 Editor's Picks").
    property string en: ""
    signal openPlaylist(var playlist)
    signal openAlbum(var album)
    property int offset: 0
    readonly property var shown: collections.slice(offset).concat(collections.slice(0, offset)).slice(0, 12)
    spacing: 20
    Column {
        width: parent.width; spacing: Theme.s3
        Hairline { visible: Theme.rules; width: parent.width; color: Theme.ruleInk }
        Item {
            width: parent.width; height: Theme.controlMd
            CssText {
                id: cnTxt; anchors.verticalCenter: parent.verticalCenter
                text: root.title; color: Theme.text
                font.family: Theme.fontSerif; font.pixelSize: Theme.fsXl; font.weight: Theme.fwRegular
            }
            CssText {
                anchors.baseline: cnTxt.baseline; anchors.left: cnTxt.right; anchors.leftMargin: Theme.s2
                visible: root.en !== ""; text: root.en; color: Theme.text3
                font.family: Theme.fontMeta; font.pixelSize: Theme.fsMd; font.italic: true
            }
            ActionButton {
                id: refresh; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                text: root.collections.length > 12 ? "换一批" : "刷新"; glyph: "refresh-cw"; size: "sm"; quiet: true
                enabled: !homeState.loading
                onClicked: { if (root.collections.length > 12) root.offset = (root.offset + 12) % root.collections.length; else homeState.refresh() }
            }
        }
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
                    Item {
                        width: card.width; height: width
                        // .card .cover: ink hairline frame, the press's
                        // halftone screen over the art, corner stamp.
                        Cover { anchors.fill: parent; pixels: 300; radius: Theme.rXs; url: card.modelData.image || ""; title: card.modelData.name || "" }
                        Halftone { anchors.fill: parent; opacity: card.modelData.image ? 0.14 : 0 }
                        Rectangle {
                            anchors.fill: parent; radius: Theme.rXs; color: "transparent"
                            border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: Theme.ruleInk
                            PixelSnap.fit: true
                        }
                        Rectangle {
                            x: Theme.s2; y: Theme.s2
                            width: stamp.implicitWidth + Theme.s1; height: stamp.implicitHeight + Theme.hairline * 2
                            color: Theme.bgElevated
                            border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: Theme.seal
                            CssText {
                                id: stamp; anchors.centerIn: parent
                                text: "精选"; color: Theme.seal
                                font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                            }
                        }
                    }
                    CssText { width: parent.width; text: card.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd; color: card.hovered ? Theme.text : Theme.text2 }
                    CssText { width: parent.width; elide: Text.ElideRight; text: card.modelData.artist || (Number(card.modelData.count || 0) > 0 ? card.modelData.count + " 首" : "发现新的旋律"); font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; color: Theme.text3 }
                }
            }
        }
    }
    Text { visible: root.collections.length === 0; text: homeState.loading ? "正在读取推荐歌单…" : "推荐歌单暂不可用，可刷新重试"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsSm }
}
