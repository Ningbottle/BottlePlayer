import QtQuick
import QtQuick.Controls
import BottleMusic

// A separate search surface; the home wall stays mounted behind it so returning
// restores the same camera and focused cover. Data and audio remain controller-owned.
Item {
    id: root
    objectName: "homeSearchPage"
    property bool opened: false
    property string submittedTerm: ""
    readonly property bool hasSearch: submittedTerm.length > 0 && music.searchTerm === submittedTerm
    readonly property real inset: Math.max(52, (width - 880) / 2)
    readonly property color ink: Theme.wallChromeInk
    readonly property color muted: Theme.wallChromeMuted
    signal close()
    signal query(string term, bool append)
    property real openProgress: opened ? 1 : 0
    width: parent.width; height: parent.height
    y: -height * (1-openProgress)
    visible: opened || slide.running
    enabled: opened
    Behavior on openProgress { NumberAnimation { id: slide; duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
    onOpenedChanged: if (opened) Qt.callLater(focusSearch)
    function focusSearch() { input.forceActiveFocus(); input.selectAll() }
    function submit() { var term=input.text.trim(); if (term && music.ready) root.query(term, false) }
    onSubmittedTermChanged: if (input.text !== submittedTerm) input.text=submittedTerm

    Rectangle { objectName: "homeSearchBackground"; anchors.fill: parent; color: Theme.wallBg }
    MouseArea {
        anchors.fill: parent; acceptedButtons: Qt.AllButtons; preventStealing: true
        onPressed: mouse => mouse.accepted=true
        onWheel: wheel => wheel.accepted=true
    }
    component SearchIcon: IconButton {
        boxSize: 32; radius: 8; glyphSize: 17
        tint: root.muted; hoverTint: root.ink; checkedTint: root.ink
        hoverFill: Theme.wallChromeHover; downFill: Theme.wallChromePressed; focusInk: Theme.wallFocus
    }
    SearchIcon {
        objectName: "homeSearchBack"; x: 18; y: 48; glyph: "arrow-left"
        Accessible.name: "返回封面墙"; onClicked: root.close()
    }
    Column {
        id: heading; x: root.inset; y: root.height < 700 ? 64 : 96
        width: root.width - root.inset*2; spacing: 23
        Text { text: "搜索音乐"; color: root.ink; font.family: Theme.fontUi; font.pixelSize: Theme.fs2xl; font.weight: Font.Medium }
        Item {
            width: parent.width; height: 52
            Icon { x: 0; y: 13; name: "search"; size: 21; color: root.muted }
            TextField {
                id: input; objectName: "bravaisSearchInput"
                x: 36; width: parent.width - x - 48; height: 46; padding: 0
                placeholderText: "输入歌名或歌手"; placeholderTextColor: root.muted
                color: root.ink; font.family: Theme.fontUi; font.pixelSize: Theme.fsXl
                selectionColor: Theme.wallAccent; selectedTextColor: Theme.inkOn(Theme.wallAccent); background: null
                onAccepted: root.submit()
                Accessible.name: "输入歌名或歌手"
            }
            SearchIcon {
                objectName: "homeSearchSubmit"; anchors.right: parent.right; y: 7; glyph: "chevron-right"
                enabled: music.ready && input.text.trim().length > 0
                Accessible.name: "搜索"; onClicked: root.submit()
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: input.activeFocus ? Theme.wallFocus : Theme.wallBorder }
        }
        Text {
            objectName: "homeSearchStatus"; width: parent.width
            text: !music.ready ? "正在连接…" : !root.hasSearch ? "输入名字，找一首想听的歌"
                : music.searching ? "正在搜索…" : music.searchError ? "搜索失败，请重试"
                : music.searchResults.length ? "找到 " + music.searchTotal + " 首歌曲" : "没有找到相关歌曲，换个名字试试"
            color: root.muted; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd
            textFormat: Text.PlainText; wrapMode: Text.Wrap
        }
    }
    ListView {
        id: results; objectName: "homeSearchResults"
        x: root.inset-8; y: heading.y+heading.height+22
        width: root.width-root.inset*2+16; height: Math.max(0,root.height-y-128)
        model: root.hasSearch ? music.searchResults : []
        visible: root.hasSearch; clip: true; currentIndex: -1
        boundsBehavior: Flickable.StopAtBounds
        SnapSettle { flickable: results }
        ScrollBar.vertical: ThinScrollBar { thumbColor: Theme.wallChromeMuted; thumbHoverColor: Theme.wallAccent }
        Keys.onReturnPressed: if (currentIndex>=0) music.playFrom("search",currentIndex)
        delegate: AbstractButton {
            id: song; required property var modelData; required property int index
            objectName: "homeSearchRow"+index
            width: results.width; height: 72; hoverEnabled: true
            Accessible.name: modelData.title+"，"+modelData.artist
            onClicked: results.currentIndex=index
            onDoubleClicked: music.playFrom("search",index)
            background: Rectangle { radius: 7; color: song.hovered || song.visualFocus || song.index===results.currentIndex ? (Theme.wallChromeHover) : "transparent" }
            contentItem: Item {
                Cover { x: 8; y: 12; width: 48; height: 48; radius: 5; pixels: 120; url: song.modelData.image || ""; title: song.modelData.title || "" }
                Column {
                    x: 74; anchors.verticalCenter: parent.verticalCenter; width: parent.width-x-132; spacing: 6
                    Text {
                        width: parent.width; text: song.modelData.title || "未知歌曲"; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: root.ink; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; font.weight: Font.Medium
                    }
                    Text {
                        width: parent.width; text: (song.modelData.artist || "未知歌手")+(song.modelData.album ? " / "+song.modelData.album : "")
                        textFormat: Text.PlainText; elide: Text.ElideRight; color: root.muted; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                    }
                }
                Text { anchors.right: actions.left; anchors.rightMargin: 15; anchors.verticalCenter: parent.verticalCenter; text: Theme.duration(song.modelData.duration); color: root.muted; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs }
                Row {
                    id: actions; anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    SearchIcon { objectName: "homeSearchPlay"+song.index; media: "play"; mediaBox: 30; mediaGlyph: 14; Accessible.name: "播放 "+song.modelData.title; onClicked: music.playFrom("search",song.index) }
                    SearchIcon { objectName: "homeSearchEnqueue"+song.index; glyph: "plus"; enabled: !music.radio; Accessible.name: "加入播放队列"; onClicked: music.enqueueTrack(song.modelData) }
                }
            }
        }
        footer: Item {
            width: results.width; height: root.hasSearch ? 64 : 0
            ActionButton {
                objectName: "homeSearchMore"; anchors.horizontalCenter: parent.horizontalCenter; y: 12
                visible: root.hasSearch && (music.searching || !!music.searchError || music.searchResults.length < music.searchTotal)
                text: music.searching ? "正在加载…" : music.searchError ? "重试搜索" : "加载更多"
                enabled: !music.searching; quiet: true; restInk: root.muted; hoverInk: root.ink
                restFill: "transparent"; hoverFill: Theme.wallChromeHover; downFill: Theme.wallChromePressed; focusInk: Theme.wallFocus
                onClicked: root.query(root.submittedTerm,music.searchResults.length>0)
            }
        }
    }
}
