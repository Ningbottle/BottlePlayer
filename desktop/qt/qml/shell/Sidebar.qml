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
    property bool playlistsExpanded: false
    onRouteChanged: if (route === "library" || route === "playlist") playlistsExpanded = true
    // The submenu caps its rows so the foot links stay reachable; the
    // overflow row lands on the catalog page instead.
    readonly property int playlistRowLimit: compactHeight ? 4 : 6
    readonly property var playlistRows: libraryState.playlists.slice(0, playlistRowLimit)
    readonly property bool playlistsTruncated: libraryState.playlists.length > playlistRows.length || libraryState.playlistsMore
    signal navigate(string page)
    signal openSettings(string section)
    signal leaveSettings()
    signal openPlaylist(string id)
    function revealRow(item) {
        var ancestor = item.parent
        while (ancestor && ancestor !== navigationRows) ancestor = ancestor.parent
        if (!ancestor) return
        var top = item.mapToItem(navigationRows, 0, 0).y + navigationRows.y
        if (top < sidebarScroll.contentY) sidebarScroll.contentY = Math.max(0, top - Theme.s1)
        else if (top + item.height > sidebarScroll.contentY + sidebarScroll.height)
            sidebarScroll.contentY = Math.max(0, Math.min(sidebarScroll.contentHeight - sidebarScroll.height, top + item.height + Theme.s1 - sidebarScroll.height))
    }

    // .shell-sidebar border-right: var(--rule-w) solid var(--border).
    Hairline {
        visible: Theme.rules; vertical: true
        anchors.right: parent.right; height: parent.height
    }

    // .nav a: 34px row, 0 12px padding, 12px gap, --r-sm, fs-sm --text-2;
    // hover --surface-hover/--text; active --accent-soft/--accent/500.
    // Background and ink ease over --dur-fast; the active fill cross-fades
    // between rows. `depth` indents a level for the playlist submenu (smaller
    // row, smaller glyph); `expandable` adds a trailing chevron that rotates
    // open over --dur-base.
    component NavRow: AbstractButton {
        id: row
        property string glyph: ""
        property real glyphSize: 18
        // `bullet` swaps the icon box for the press column's middle dot —
        // playlist submenu rows read as a bulleted list, not an icon grid.
        property bool bullet: false
        property bool current: false
        property bool expandable: false
        property bool expanded: false
        property int depth: 0
        property color restInk: Theme.text2
        readonly property color ink: current ? Theme.accentText : hovered || visualFocus ? Theme.text : restInk
        Layout.fillWidth: true
        // Fixed so the eliding label never feeds back into the layout width.
        implicitWidth: Theme.sidebarW - Theme.s3 * 2
        implicitHeight: row.depth > 0 ? 30 : root.compactHeight ? 34 : 40
        Layout.minimumHeight: implicitHeight
        onActiveFocusChanged: if (activeFocus) root.revealRow(row)
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        leftPadding: Theme.s3 + row.depth * Theme.s4; rightPadding: Theme.s3
        background: Item {
            Rectangle {
                anchors.fill: parent; radius: Theme.rSm
                color: row.hovered && !row.current ? Theme.surfaceHover : Theme.clear(Theme.surfaceHover)

            }
            Rectangle {
                anchors.fill: parent; radius: Theme.rSm; color: Theme.accentSoft
                opacity: row.current ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
            }
            Rectangle {
                anchors.fill: parent; radius: Theme.rSm; color: "transparent"
                border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: Theme.focus; visible: row.visualFocus
            }
        }
        contentItem: Row {
            spacing: Theme.s3
            Icon { anchors.verticalCenter: parent.verticalCenter; visible: !row.bullet; name: row.glyph; color: row.ink; size: row.glyphSize }
            Item {
                anchors.verticalCenter: parent.verticalCenter; visible: row.bullet
                width: row.glyphSize; height: width
                Rectangle { anchors.centerIn: parent; width: Math.max(2, Math.round(3 * Theme.dpr) / Theme.dpr); height: width; radius: width / 2; color: row.ink }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: row.availableWidth - row.glyphSize - Theme.s3 - (row.expandable ? 14 + Theme.s3 : 0)
                text: row.text; color: row.ink; elide: Text.ElideRight
                font.family: Theme.fontUi; font.pixelSize: row.depth > 0 ? Theme.fsXs : Theme.fsMd
                // Noto Serif SC ships regular only — submenu rows never take
                // the medium weight that would embolden synthetically.
                font.weight: row.current && row.depth === 0 ? Theme.fwMedium : Theme.fwRegular

            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: row.expandable; name: "chevron-right"; size: 14; color: row.restInk
                rotation: row.expanded ? 90 : 0
                Behavior on rotation { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
            }
        }
        Accessible.name: text
    }

    // Section eyebrows: the press's small tracked caps — "我的歌单，PLAYLISTS".
    component Eyebrow: Text {
        color: Theme.text3; leftPadding: Theme.s3; rightPadding: Theme.s3
        font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs
        font.letterSpacing: Theme.metaTracking; font.capitalization: Theme.metaCase
        elide: Text.ElideRight
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.titlebarH + Theme.s2
        anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3; anchors.bottomMargin: Theme.s3
        spacing: Theme.s1

        // The nameplate: Caslon wordmark over the folio double rule, the way
        // a newspaper opens its front page below the dateline.
        Column {
            visible: !root.settingsMode
            Layout.fillWidth: true; Layout.bottomMargin: Theme.s2; spacing: Theme.s2
            CssText {
                leftPadding: Theme.s3; text: "BottleMusic"; color: Theme.text
                font.family: Theme.fontDisplay; font.pixelSize: Theme.fsXl
                font.weight: Theme.fwRegular; font.letterSpacing: Theme.displayTracking * Theme.fsXl
            }
            DoubleRule { width: parent.width }
        }

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

            }
            contentItem: Row {
                spacing: Theme.s3
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.controlMd; height: width; radius: Theme.rSm; color: Theme.surface
                    Icon {
                        anchors.centerIn: parent; name: "user-round"; size: 18; color: Theme.text2
                        visible: !accountState.loggedIn && !accountState.avatar.length
                    }
                    Text {
                        anchors.centerIn: parent; text: accountState.loggedIn ? accountState.username.slice(0, 1) : "听"; color: Theme.accent
                        visible: accountState.loggedIn && !accountState.avatar.length
                        font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; font.weight: Theme.fwDisplay
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
                        text: accountState.loggedIn ? "账户与会员" : "扫码登录"; color: Theme.text3; elide: Text.ElideRight
                        font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic
                    }
                }
            }
            Accessible.name: "账户与会员"
        }

        Flickable {
            id: sidebarScroll; objectName: "sidebarNavigation"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            clip: true; contentWidth: width; contentHeight: navigationRows.implicitHeight + Theme.s2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThinScrollBar {}
            SmoothWheel { flickable: sidebarScroll }
            ColumnLayout {
                id: navigationRows
                y: Theme.s1; width: parent.width; height: implicitHeight; spacing: Theme.s1
                ColumnLayout {
                    visible: !root.settingsMode
                    Layout.fillWidth: true; spacing: 2
                    NavRow { objectName: "homeEntry"; glyph: "newspaper"; text: "首页"; current: root.route === "home"; onClicked: root.navigate("home") }
                    NavRow { objectName: "historyEntry"; glyph: "history"; text: "最近播放"; current: root.route === "history"; onClicked: root.navigate("history") }
                    NavRow { objectName: "statsEntry"; glyph: "chart-no-axes-combined"; text: "统计"; current: root.route === "stats"; onClicked: root.navigate("stats") }
                }
                Eyebrow {
                    visible: !root.settingsMode && !root.compactHeight
                    Layout.fillWidth: true; Layout.topMargin: Theme.s5; Layout.bottomMargin: Theme.s1
                    text: "音乐库"
                }
                ColumnLayout {
                    visible: !root.settingsMode; Layout.fillWidth: true; spacing: 2
                    Layout.topMargin: root.compactHeight ? 8 : 0
                    NavRow {
                        objectName: "libraryEntry"; glyph: "list-music"; text: "我的歌单"
                        current: root.route === "library"
                        expandable: true; expanded: root.playlistsExpanded
                        // Expands the playlist submenu in place; a playlist row opens its detail page.
                        onClicked: root.playlistsExpanded = !root.playlistsExpanded
                    }
                    // Playlist submenu: depth-1 rows are bulleted press items — a
                    // middle dot and a serif name, like the reference's list.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: root.playlistsExpanded && !root.settingsMode ? playlistSubmenu.implicitHeight : 0
                        clip: true

                        Column {
                            id: playlistSubmenu
                            width: parent.width; spacing: 2
                            Repeater {
                                model: root.playlistRows
                                NavRow {
                                    required property var modelData
                                    objectName: "sidebarPlaylist_" + modelData.id
                                    depth: 1; bullet: true; restInk: Theme.text3
                                    text: modelData.name
                                    current: root.route === "playlist" && libraryState.playlistId === modelData.id
                                    onClicked: root.openPlaylist(modelData.id)
                                }
                            }
                            NavRow {
                                visible: accountState.loggedIn && libraryState.playlistsLoading && root.playlistRows.length === 0
                                depth: 1; bullet: true; restInk: Theme.text3
                                text: "正在读取歌单…"; enabled: false
                            }
                            NavRow {
                                objectName: "sidebarPlaylistsAll"
                                // The catalog also exposes errors and refresh for an
                                // empty or failed first page, so it stays reachable.
                                visible: accountState.loggedIn
                                depth: 1; bullet: true; restInk: Theme.text3
                                text: root.playlistsTruncated ? "查看全部歌单" : "歌单目录"
                                onClicked: root.navigate("library")
                            }
                            NavRow {
                                visible: !accountState.loggedIn
                                depth: 1; bullet: true; restInk: Theme.text3
                                text: "登录后查看歌单"; onClicked: root.openSettings("account")
                            }
                        }
                    }
                    NavRow { objectName: "localFavoritesEntry"; glyph: "heart"; text: "本地收藏"; current: root.route === "favorites"; onClicked: root.navigate("favorites") }
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
            }
        }

        // .sidebar-foot: margin-top auto, 8px top padding.
        NavRow {
            objectName: "settingsButton"
            visible: !root.settingsMode
            Layout.topMargin: Theme.s1
            glyph: "settings"; text: "设置"; onClicked: root.openSettings("appearance")
        }
    }
}
