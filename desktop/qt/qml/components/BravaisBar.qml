import QtQuick
import QtQuick.Controls
import BottleMusic

Rectangle {
    id: root
    objectName: "bravaisBar"
    color: Theme.wallBg
    height: Theme.wallBarH
    property string selectedTab: "playlists"
    property bool searchOpen: false
    property bool queueOpen: false
    property bool toolsOpen: false
    property real trailingInset: 118
    property string statusText: ""
    property bool statusIsError: false
    property bool retryVisible: false
    property bool moreVisible: false
    readonly property bool hasTrack: !!music.current.hash
    readonly property bool showWordmark: width >= 1100 || (!searchOpen && !statusText)
    readonly property real fieldWidth: Theme.clamp(220, width * 0.26, 360)
    readonly property real fixedLeading: 16 + (showWordmark ? wordmark.width + 20 : 0)
        + (searchOpen ? fieldWidth : tabs.width + 8 + 36) + 12
    readonly property real statusActions: (retryVisible ? retry.implicitWidth : 0) + (moreVisible ? more.implicitWidth : 0)
        + (retryVisible && moreVisible ? 8 : 0)
    readonly property real fixedTrailing: 116 + 16 + 76 + (hasTrack ? 36 : 0)
    readonly property real titleSpace: width - fixedLeading - fixedTrailing - trailingInset - 12 - 48 - 4
        - Math.min(status.implicitWidth,120) - statusActions - (statusActions > 0 ? 8 : 0)
    readonly property real titleBudget: hasTrack && titleSpace >= 120 ? Math.min(280,titleSpace) : 0
    readonly property bool nowPlayingVisible: nowPlaying.visible
    readonly property real statusWidth: Math.max(0, width - fixedLeading - trailing.width - trailingInset - 12 - 48 - statusActions
        - (statusActions > 0 ? 8 : 0))
    signal select(string tab)
    signal openSearch()
    signal closeSearch()
    signal query(string term)
    signal retry()
    signal more()
    signal queue()
    signal tools()
    signal player()
    signal account()
    signal settings()
    signal menu()
    signal startWindowMove()
    signal toggleMaximize()
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }
    function resetSearch() { search.text = "" }
    // Track positioner geometry explicitly: mapToItem() alone does not make
    // a binding depend on the Row's later polish/relayout positions.
    readonly property real toolsRight: trailing.x + playGroup.x + toolsButton.x + toolsButton.width
    readonly property real moreRight: trailing.x + appGroup.x + moreButton.x + moreButton.width

    component WallIcon: IconButton {
        boxSize: 36; radius: width / 2; glyphSize: 18
        tint: Theme.wallChromeMuted; hoverTint: Theme.wallChromeInk; checkedTint: Theme.wallChromeInk
        hoverFill: Theme.wallChromeHover; downFill: Theme.wallChromePressed; focusInk: Theme.wallFocus
    }
    component WallAction: ActionButton {
        quiet: true; implicitHeight: 28; typeSize: 13; leftPadding: 8; rightPadding: 8
        restInk: Theme.wallChromeMuted; hoverInk: Theme.wallChromeInk; edgeColor: "transparent"
        hoverFill: Theme.wallChromeHover; downFill: Theme.wallChromePressed; focusInk: Theme.wallFocus
        bgRadius: height / 2
    }
    MouseArea { anchors.fill: parent; onPressed: root.startWindowMove(); onDoubleClicked: root.toggleMaximize() }
    Row {
        id: leading; objectName: "bravaisBarLeading"
        x: 16; anchors.verticalCenter: parent.verticalCenter; spacing: 0
        Text {
            id: wordmark; visible: root.showWordmark
            text: "BottleMusic"; color: Theme.wallChromeMuted; font.family: Theme.fontUi
            font.pixelSize: 13; font.weight: Theme.fwMedium; anchors.verticalCenter: parent.verticalCenter
        }
        Item { visible: wordmark.visible; width: 20; height: 1 }
        Row {
            id: tabs; objectName: "bravaisLibrary"; visible: !root.searchOpen; spacing: 2
            Repeater {
                model: [{key:"recommend",label:"推荐"},{key:"playlists",label:"歌单"},{key:"albums",label:"专辑"},{key:"mine",label:"我的"},{key:"favorites",label:"收藏"}]
                ActionButton {
                    required property var modelData
                    objectName: "bravaisTab_" + modelData.key
                    readonly property bool selected: root.selectedTab === modelData.key
                    text: modelData.label; quiet: true; implicitHeight: 32; typeSize: 14
                    leftPadding: 12; rightPadding: 12; bgRadius: 16; edgeColor: "transparent"
                    restFill: selected ? Theme.wallChromeSurface : "transparent"
                    restInk: selected ? Theme.wallChromeInk : Theme.wallChromeMuted; hoverInk: Theme.wallChromeInk
                    hoverFill: selected ? Theme.wallChromeSurface : Theme.wallChromeHover
                    downFill: Theme.wallChromePressed; focusInk: Theme.wallFocus
                    Accessible.role: Accessible.PageTab; Accessible.selected: selected
                    onClicked: root.select(modelData.key)
                }
            }
        }
        Item {
            visible: root.searchOpen; width: root.fieldWidth; height: 32
            Rectangle { anchors.fill: parent; radius: 16; color: Theme.wallChromeSurface }
            Icon { x: 12; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 16; color: Theme.wallChromeMuted }
            TextField {
                id: search; objectName: "bravaisSearchInput"
                x: 36; width: Math.max(0,parent.width - 68); height: parent.height
                padding: 0; maximumLength: 100; placeholderText: "搜索歌曲、艺人"
                font.family: Theme.fontUi; font.pixelSize: 14; color: Theme.wallChromeInk
                placeholderTextColor: Theme.wallChromeMuted; selectionColor: "#3dffffff"; selectedTextColor: Theme.wallChromeInk
                selectByMouse: true; background: null
                onAccepted: root.query(text)
                Keys.onEscapePressed: root.closeSearch()
            }
            WallIcon {
                objectName: "bravaisSearchClose"; anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 28; implicitHeight: 28; glyphSize: 16; glyph: "x"; Accessible.name: "关闭搜索"
                onClicked: root.closeSearch()
            }
        }
        Item { visible: !root.searchOpen; width: 8; height: 1 }
        WallIcon { objectName: "bravaisGlobalSearch"; visible: !root.searchOpen; glyph: "search"; Accessible.name: "搜索"; onClicked: root.openSearch() }
        Item { width: 12; height: 1 }
        Row {
            spacing: 8; anchors.verticalCenter: parent.verticalCenter
            Text {
                id: status; objectName: "bravaisStatus"; width: Math.min(implicitWidth,root.statusWidth)
                text: root.statusText; textFormat: Text.PlainText; elide: Text.ElideRight; clip: true
                font.family: Theme.fontUi; font.pixelSize: 13
                color: root.statusIsError ? Theme.wallChromeInk : Theme.wallChromeMuted
                anchors.verticalCenter: parent.verticalCenter
                HoverHandler { id: statusHover }
                ToolTip {
                    objectName: "bravaisStatusTip"; visible: statusHover.hovered && status.truncated; delay: 500
                    text: root.statusText; y: (root.height + status.height)/2 + 4; width: Math.min(400,root.width - 32)
                    contentItem: Text { text: root.statusText; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.wallChromeInk; font.family: Theme.fontUi; font.pixelSize: 13 }
                    background: Rectangle { color: Theme.wallChromeSurface; radius: 8 }
                }
            }
            WallAction { id: retry; objectName: "bravaisRetry"; visible: root.retryVisible; text: "重试"; onClicked: root.retry() }
            WallAction { id: more; objectName: "bravaisMoreResults"; visible: root.moreVisible; text: "更多"; onClicked: root.more() }
        }
    }
    Row {
        id: trailing; objectName: "bravaisBarTrailing"
        anchors.right: parent.right; anchors.rightMargin: root.trailingInset + 12
        anchors.verticalCenter: parent.verticalCenter; spacing: 16
        Row {
            id: playGroup
            spacing: 4; anchors.verticalCenter: parent.verticalCenter
            IconButton {
                objectName: "bravaisBarPlay"; visible: root.hasTrack; anchors.verticalCenter: parent.verticalCenter
                media: music.playing ? "pause" : "play"; mediaBox: 32; mediaGlyph: 16; radius: width/2
                variant: "primary"; primaryFill: Theme.wallChromeSurface
                primaryHoverFill: Theme.wallHoverOn(primaryFill); primaryDownFill: Theme.wallDownOn(primaryFill)
                tint: Theme.wallChromeInk; hoverTint: tint; focusInk: Theme.wallFocus
                enabled: !music.loading; Accessible.name: music.playing ? "暂停" : "播放"; onClicked: music.toggle()
            }
            AbstractButton {
                id: nowPlaying; objectName: "bravaisNowPlaying"
                visible: root.hasTrack && root.titleBudget >= 120
                width: visible ? Math.min(root.titleBudget,nowTitle.implicitWidth + nowArtist.implicitWidth + 20 + (nowArtist.text ? separator.width + 8 : 0)) : 0
                height: 32; hoverEnabled: true; focusPolicy: Qt.TabFocus
                Accessible.name: "打开播放页：" + music.current.title; onClicked: root.player()
                background: Rectangle { radius: 16; color: nowPlaying.down ? Theme.wallChromePressed : nowPlaying.hovered ? Theme.wallChromeHover : "transparent"; border.width: nowPlaying.visualFocus ? 2 : 0; border.color: Theme.wallFocus }
                contentItem: Row {
                    x: 10; spacing: 4; anchors.verticalCenter: parent.verticalCenter
                    Text { id: nowTitle; anchors.verticalCenter: parent.verticalCenter; width: Math.max(0,nowPlaying.width - 20 - (nowArtist.text ? nowArtist.width+separator.width+8 : 0)); text: music.current.title || ""; textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.wallChromeInk; font.family: Theme.fontUi; font.pixelSize: 13; font.weight: Theme.fwMedium }
                    Text { id: separator; anchors.verticalCenter: parent.verticalCenter; visible: !!nowArtist.text; text: "·"; color: Theme.wallChromeMuted; font.family: Theme.fontUi; font.pixelSize: 13 }
                    Text { id: nowArtist; anchors.verticalCenter: parent.verticalCenter; visible: !!text; width: text ? Math.min(implicitWidth,Math.max(0,(nowPlaying.width-20-separator.width-8)*0.4)) : 0; text: music.current.artist || ""; textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.wallChromeMuted; font.family: Theme.fontUi; font.pixelSize: 13 }
                }
            }
            WallIcon { objectName: "bravaisQueue"; glyph: "list-music"; checked: root.queueOpen; Accessible.name: "播放队列"; onClicked: root.queue() }
            WallIcon { id: toolsButton; objectName: "bravaisTools"; glyph: "sliders-horizontal"; checked: root.toolsOpen; Accessible.name: "墙面与播放工具"; onClicked: root.tools() }
        }
        Row {
            id: appGroup
            spacing: 4; anchors.verticalCenter: parent.verticalCenter
            WallIcon { objectName: "bravaisAccount"; glyph: "user-round"; Accessible.name: "账户"; onClicked: root.account() }
            WallIcon { objectName: "bravaisSettings"; glyph: "settings"; Accessible.name: "设置"; onClicked: root.settings() }
            WallIcon { id: moreButton; objectName: "bravaisMore"; glyph: "more-horizontal"; Accessible.name: "更多"; onClicked: root.menu() }
        }
    }
}
