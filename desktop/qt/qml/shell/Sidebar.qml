import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

// Sidebar geometry: full-height --pane-bg column under the titlebar
// with a --rule-w --border right edge, user block, icon nav rows, a section
// label, and the settings link pinned to the foot. In settings mode the nav
// becomes 返回 + the settings sections.
Rectangle {
    id: root
    objectName: "paperSidebar"
    readonly property bool compactHeight: height < 650
    color: Theme.paneBg
    property string route: "home"
    property bool settingsMode: false
    property string settingsSection: "appearance"
    property var sections: []
    signal navigate(string page)
    signal enterCurtain()
    signal openSettings(string section)
    signal leaveSettings()
    signal openPlaylist(string id)

    // .shell-sidebar border-right: var(--rule-w) solid var(--border).
    Hairline {
        visible: Theme.rules; vertical: true
        anchors.right: parent.right; height: parent.height
    }

    // .nav a: 34px row, 0 12px padding, 12px gap, --r-sm, fs-sm --text-2;
    // hover --surface-hover/--text; active --accent-soft/--accent/500.
    // Background and ink ease over --dur-fast; the active fill cross-fades
    // between rows.
    component NavRow: AbstractButton {
        id: row
        property string glyph: ""
        property bool current: false
        property color restInk: Theme.text2
        readonly property color ink: current ? Theme.accent : hovered || visualFocus ? Theme.text : restInk
        Layout.fillWidth: true
        // Fixed so the eliding label never feeds back into the layout width.
        implicitWidth: Theme.sidebarW - Theme.s3 * 2
        implicitHeight: root.compactHeight ? Theme.controlSm : Theme.controlMd
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        leftPadding: Theme.s3; rightPadding: Theme.s3
        background: Item {
            Rectangle {
                anchors.fill: parent; radius: Theme.rSm
                color: row.hovered && !row.current ? Theme.surfaceHover : Theme.clear(Theme.surfaceHover)
                Behavior on color { ColorAnimation { duration: Theme.durFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
            }
            Rectangle {
                anchors.fill: parent; radius: Theme.rSm; color: Theme.accentSoft
                opacity: row.current ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
            }
            Rectangle {
                anchors.fill: parent; radius: Theme.rSm; color: "transparent"
                border.width: 2; border.color: Theme.focus; visible: row.visualFocus
            }
        }
        contentItem: Row {
            spacing: Theme.s3
            Icon { anchors.verticalCenter: parent.verticalCenter; name: row.glyph; color: row.ink }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: row.availableWidth - Theme.iconSize - Theme.s3
                text: row.text; color: row.ink; elide: Text.ElideRight
                font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
                font.weight: row.current ? Theme.fwMedium : Theme.fwRegular
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
            }
        }
        Accessible.name: text
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.titlebarH + Theme.s2
        anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3; anchors.bottomMargin: Theme.s3
        spacing: Theme.s1

        // .user: 8px padding, 12px gap, --r-md, 12px bottom margin.
        AbstractButton {
            id: user
            objectName: "sidebarUser"
            Layout.fillWidth: true; Layout.bottomMargin: Theme.s3
            implicitWidth: Theme.sidebarW - Theme.s3 * 2
            padding: Theme.s2
            hoverEnabled: true
            onClicked: root.openSettings("account")
            background: Rectangle {
                radius: Theme.rMd
                color: user.hovered ? Theme.surfaceHover : Theme.clear(Theme.surfaceHover)
                Behavior on color { ColorAnimation { duration: Theme.durFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
            }
            contentItem: Row {
                spacing: Theme.s3
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.controlMd; height: width; radius: width / 2; color: Theme.accentSoft
                    Text {
                        anchors.centerIn: parent; text: accountState.loggedIn ? accountState.username.slice(0, 1) : "听"; color: Theme.accent
                        visible: !accountState.avatar.length
                        font.family: Theme.fontDisplay; font.pixelSize: Theme.fsMd; font.weight: Theme.fwDisplay
                    }
                    Cover { anchors.fill: parent; visible: accountState.avatar.length > 0; url: accountState.avatar; radius: width / 2; pixels: 100 }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: user.availableWidth - Theme.controlMd - Theme.s3
                    spacing: 2
                    Text {
                        width: parent.width; height: Theme.fsSm * Theme.lhNormal; verticalAlignment: Text.AlignVCenter
                        text: accountState.username; textFormat: Text.PlainText; color: Theme.text; elide: Text.ElideRight
                        font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; font.weight: Theme.fwMedium
                    }
                    // .user .meta: --font-meta in --meta-style.
                    Text {
                        width: parent.width; height: Theme.fsXs * Theme.lhNormal; verticalAlignment: Text.AlignVCenter
                        text: accountState.loggedIn ? "已登录 · 查看个人歌单" : "扫码登录，查看个人歌单"; color: Theme.text3; elide: Text.ElideRight
                        font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic
                    }
                }
            }
            Accessible.name: "账户与会员"
        }

        ColumnLayout {
            visible: !root.settingsMode
            Layout.fillWidth: true; spacing: 2
            NavRow { objectName: "homeEntry"; glyph: "house"; text: "首页"; current: root.route === "home"; onClicked: root.navigate("home") }
            NavRow { objectName: "playerEntry"; glyph: "disc-3"; text: "正在播放"; current: root.route === "player"; onClicked: root.navigate("player") }
        }
        Text {
            visible: !root.settingsMode && !root.compactHeight
            Layout.fillWidth: true; Layout.topMargin: Theme.s5; Layout.bottomMargin: Theme.s1
            leftPadding: Theme.s3; text: "我的音乐"; color: Theme.text3
            font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic
        }
        ColumnLayout {
            visible: !root.settingsMode; Layout.fillWidth: true; spacing: 2
            Layout.topMargin: root.compactHeight ? 8 : 0
            NavRow { objectName: "libraryEntry"; glyph: "list-music"; text: "我的歌单"; current: root.route === "library"; onClicked: root.navigate("library") }
            NavRow { objectName: "localFavoritesEntry"; glyph: "heart"; text: "本地收藏"; current: root.route === "favorites"; onClicked: root.navigate("favorites") }
            NavRow { objectName: "historyEntry"; glyph: "history"; text: "最近播放"; current: root.route === "history"; onClicked: root.navigate("history") }
            NavRow { objectName: "statsEntry"; glyph: "chart-no-axes-combined"; text: "统计"; current: root.route === "stats"; onClicked: root.navigate("stats") }
        }
        // .section-label: 20px top / 4px bottom margin, fs-xs --font-meta in
        // --meta-style, --meta-tracking, --meta-transform.
        Text {
            visible: !root.settingsMode
            Layout.fillWidth: true; Layout.topMargin: root.compactHeight ? 8 : Theme.s5; Layout.bottomMargin: Theme.s1
            leftPadding: Theme.s3; rightPadding: Theme.s3
            text: "外观"; color: Theme.text3
            font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.letterSpacing: Theme.metaTracking
            font.italic: Theme.metaItalic; font.capitalization: Theme.metaCase
        }
        ColumnLayout {
            visible: !root.settingsMode
            Layout.fillWidth: true; spacing: 2
            NavRow { objectName: "stageEntry"; glyph: "theater"; text: "幕布舞台"; onClicked: root.enterCurtain() }
        }

        ColumnLayout {
            objectName: "settingsNav"
            visible: root.settingsMode
            Layout.fillWidth: true; spacing: 2
            NavRow {
                objectName: "settingsClose"; glyph: "arrow-left"; text: "返回"; restInk: Theme.text3
                Layout.bottomMargin: Theme.s2; onClicked: root.leaveSettings()
            }
            Repeater {
                model: root.sections
                NavRow {
                    required property var modelData
                    objectName: "settingsSection_" + modelData.id
                    glyph: modelData.icon; text: modelData.label
                    current: root.settingsSection === modelData.id
                    onClicked: root.openSettings(modelData.id)
                }
            }
        }
        RowLayout {
            visible: !root.settingsMode && accountState.loggedIn; Layout.fillWidth: true; Layout.topMargin: root.compactHeight ? 4 : Theme.s3
            Text { Layout.fillWidth: true; leftPadding: Theme.s3; text: "我的歌单"; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic }
            IconButton { glyph: "refresh-cw"; size: "sm"; enabled: !libraryState.playlistsLoading; Accessible.name: "刷新个人歌单"; onClicked: libraryState.refreshPlaylists() }
        }
        ListView {
            id: userLists; objectName: "sidebarPlaylists"; visible: !root.settingsMode && accountState.loggedIn
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: libraryState.playlists; boundsBehavior: Flickable.StopAtBounds; cacheBuffer: 100
            delegate: NavRow {
                required property var modelData; required property int index
                objectName: "sidebarPlaylist_" + index; width: userLists.width; height: implicitHeight
                text: modelData.name; glyph: "list-music"
                current: root.route === "playlist" && libraryState.playlistId === modelData.id
                onClicked: root.openPlaylist(modelData.id)
            }
            footer: ActionButton { visible: libraryState.playlistsMore; text: "加载更多"; size: "sm"; enabled: !libraryState.playlistsLoading; onClicked: libraryState.morePlaylists() }
        }
        Item { Layout.fillHeight: true; visible: root.settingsMode || !accountState.loggedIn }

        // .sidebar-foot: margin-top auto, 8px top padding.
        NavRow {
            objectName: "settingsButton"
            visible: !root.settingsMode
            Layout.topMargin: Theme.s1
            glyph: "settings"; text: "设置"; onClicked: root.openSettings("appearance")
        }
    }
}
