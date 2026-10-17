import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import BottleMusic

// Application window grid: a 36px titlebar floating over a full-height sidebar
// pane, the topbar + page in the main column, and the player bar spanning the
// window bottom. The Folia curtain hides this chrome; its listening panel
// provides access to the same library, account and search routes.
ApplicationWindow {
    id: window
    objectName: "mainWindow"
    visible: true; width: 1280; height: 850; minimumWidth: 900; minimumHeight: 620
    title: "BottleMusic · Qt Preview"
    color: window.curtainScene ? Theme.wallBg : Theme.bg
    palette.window: Theme.bgElevated
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.buttonText: Theme.text
    palette.button: Theme.surface
    palette.base: Theme.bg
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentContrast
    flags: Qt.Window | Qt.FramelessWindowHint
    property string presentation: appearanceState.presentation
    property string route: "home"
    onRouteChanged: homeState.setActive(route === "home")
    property string settingsSection: "appearance"
    property bool listeningOpen: false
    property bool queueExpanded: false
    property string bravaisTab: "playlists"
    onListeningOpenChanged: if (!listeningOpen) queueExpanded = false
    readonly property bool bravaisHomeScene: curtainScene && route === "home" && !immersive
    property bool immersive: false
    property int playbackWindowState: Window.Windowed
    property int immersiveWindowState: Window.Windowed
    property rect playbackWindowRect: Qt.rect(0, 0, 1280, 850)
    property rect immersiveWindowRect: Qt.rect(0, 0, 1280, 850)
    property rect pendingWindowRect: Qt.rect(0, 0, 1280, 850)
    property rect windowedRect: Qt.rect(0, 0, 1280, 850)
    property int pendingWindowState: Window.Windowed
    onWidthChanged: rememberGeometry.restart()
    onHeightChanged: rememberGeometry.restart()
    onXChanged: rememberGeometry.restart()
    onYChanged: rememberGeometry.restart()
    onVisibilityChanged: rememberGeometry.restart()
    Timer {
        id: rememberGeometry; interval: 120
        onTriggered: if (!window.immersive && window.visibility === Window.Windowed)
            window.windowedRect = Qt.rect(window.x, window.y, window.width, window.height)
    }
    property var backStack: []
    property var forwardStack: []
    readonly property bool paper: presentation === "paper"
    readonly property bool curtainScene: !paper && (route === "home" || route === "wall")
    readonly property bool browseChrome: !curtainScene
    readonly property bool playbackBarVisible: !immersive && browseChrome
    readonly property var settingsSections: [
        { id: "account", label: "账户与会员", lede: "扫码登录 · 账号资料 · 个人歌单", icon: "user-round" },
        { id: "appearance", label: "外观", lede: "报刊主界面 · 幕布舞台 · 阅读习惯", icon: "palette" },
        { id: "playback", label: "播放", lede: "音源连接与设备状态", icon: "sliders-vertical" },
        { id: "storage", label: "存储与缓存", lede: "本地缓存的去处", icon: "hard-drive" },
        { id: "experimental", label: "实验室", lede: "默认关闭，按需开启", icon: "flask-conical" },
        { id: "diagnostics", label: "诊断", lede: "内核状态与资源使用", icon: "bug" },
        { id: "about", label: "关于与更新", lede: "版本信息与自动更新", icon: "info" }
    ]
    readonly property var settingsMeta: settingsSections.find(section => section.id === settingsSection) || settingsSections[1]

    // Page theme mapping: the curtain home and poster wall run Aurora, every
    // other route runs Mint; Theme tokens follow from this one assignment.
    Binding {
        target: Theme; property: "skin"
        value: window.route === "wall" || (window.presentation === "curtain" && window.route === "home") ? "aurora" : "mint"
    }
    Binding { target: Theme; property: "curtain"; value: window.presentation === "curtain" }
    Binding { target: Theme; property: "dark"; value: appearanceState.dark }
    Binding { target: Theme; property: "customAccent"; value: appearanceState.accent }
    Binding { target: Theme; property: "compactList"; value: appearanceState.compact }
    Binding { target: Theme; property: "lyricFontSetting"; value: appearanceState.lyricFont }
    Binding { target: Theme; property: "lyricFocus"; value: appearanceState.lyricFocus }
    // Device-pixel ratio for the hairline rules (survives monitor moves).
    Binding { target: Theme; property: "dpr"; value: window.Screen.devicePixelRatio }

    // Skin changes tint the window over one beat, like the CSS transition.
    Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.pageEnterMs; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }

    function changeMode(mode) {
        if (immersive) leaveImmersive()
        appearanceState.presentation = mode; presentation = mode; route = "home"; listeningOpen = false
        backStack = []; forwardStack = []
    }
    function here() { return { route: route, section: settingsSection, playlist: route === "playlist" ? libraryState.playlistId : "", album: libraryState.album, publicPlaylist: libraryState.publicPlaylist, collection: {id:libraryState.playlistId,name:libraryState.playlistName,image:libraryState.playlistImage} } }
    function show(place) {
        if (immersive) leaveImmersive()
        if (route === "playlist" && place.route !== "playlist") libraryState.cancelTracks()
        if (place.route === "playlist" && place.playlist && (place.playlist !== libraryState.playlistId || !!place.album !== libraryState.album)) {
            if (place.album) libraryState.openAlbum(place.collection)
            else if (place.publicPlaylist) libraryState.openPublicPlaylist(place.collection)
            else libraryState.openPlaylist(place.playlist)
        }
        route = place.route; settingsSection = place.section; listeningOpen = false
    }
    function openPlaylist(id) {
        if (route === "playlist" && libraryState.playlistId === id) return
        const previous = here()
        libraryState.openPlaylist(id)
        if (libraryState.playlistId !== id) return
        backStack = backStack.concat([previous]); forwardStack = []; route = "playlist"; listeningOpen = false
    }
    function openPublicPlaylist(playlist) {
        const previous = here()
        libraryState.openPublicPlaylist(playlist)
        if (libraryState.playlistId !== String(playlist.id)) return
        backStack = backStack.concat([previous]); forwardStack = []; route = "playlist"; listeningOpen = false
    }
    function openAlbum(album) {
        const previous = here()
        libraryState.openAlbum(album)
        if (!libraryState.album || libraryState.playlistId !== String(album.id)) return
        backStack = backStack.concat([previous]); forwardStack = []; route = "playlist"; listeningOpen = false
    }
    function enterImmersive(fullscreen) {
        if (!immersive) {
            playbackWindowState = visibility === Window.Maximized ? Window.Maximized : Window.Windowed
            immersiveWindowState = playbackWindowState
            playbackWindowRect = Qt.rect(x, y, width, height)
            if (playbackWindowState === Window.Windowed) windowedRect = playbackWindowRect
            immersiveWindowRect = playbackWindowRect
            listeningOpen = false
            immersive = true
        }
    }
    function leaveImmersive() {
        listeningOpen = false
        immersive = false
        if (pages.item) pages.item.forceActiveFocus()
    }
    function togglePlaybackFullscreen() {
        if (immersive) leaveImmersive()
        else enterImmersive(false)
    }
    function restorePlaybackWindow(state, rect) {
        // Seed the native normal geometry before restoring maximization.
        // Otherwise Windows can retain the full-screen rect as its normal rect.
        pendingWindowState = state
        pendingWindowRect = state === Window.Maximized ? windowedRect : rect
        visibility = Window.Windowed
        x = pendingWindowRect.x; y = pendingWindowRect.y
        width = pendingWindowRect.width; height = pendingWindowRect.height
        restoreGeometry.restart()
    }
    Timer {
        id: restoreGeometry; interval: 80
        onTriggered: {
            if (window.visibility === Window.Windowed) {
                window.x = window.pendingWindowRect.x; window.y = window.pendingWindowRect.y
                window.width = window.pendingWindowRect.width; window.height = window.pendingWindowRect.height
                if (window.pendingWindowState === Window.Maximized) window.visibility = Window.Maximized
            }
        }
    }
    function go(page, section) {
        var place = { route: page, section: section || settingsSection }
        if (place.route === route && place.section === settingsSection) return
        backStack = backStack.concat([here()]); forwardStack = []
        show(place)
    }
    function navigate(page) { go(page) }
    function goBack() {
        if (!backStack.length) return
        var place = backStack[backStack.length - 1]
        forwardStack = forwardStack.concat([here()]); backStack = backStack.slice(0, -1)
        show(place)
    }
    function goForward() {
        if (!forwardStack.length) return
        var place = forwardStack[forwardStack.length - 1]
        backStack = backStack.concat([here()]); forwardStack = forwardStack.slice(0, -1)
        show(place)
    }
    function openSettings(section) {
        listeningOpen = false
        go("settings", section || settingsSection)
    }
    function leaveSettings() {
        for (var i = backStack.length - 1; i >= 0; i--)
            if (backStack[i].route !== "settings") { go(backStack[i].route); return }
        go("home")
    }
    function runSearch(text) {
        if (!text.trim()) return
        go("search"); music.search(text)
    }
    function addToPlaylist(track) { playlistPicker.track = track; playlistPicker.open() }
    AddToPlaylistDialog { id: playlistPicker; parent: Overlay.overlay }
    function searchInputFocused() { return activeFocusItem && (activeFocusItem instanceof TextInput) }

    // Application launch stagger: the chrome fades in on the first frame
    // (titlebar / sidebar / content / playerbar, 30ms apart in CSS).
    property bool launched: false
    Timer { interval: 1; running: true; onTriggered: window.launched = true }

    Shortcut { sequence: "Space"; enabled: !window.searchInputFocused() && !(window.activeFocusItem && window.activeFocusItem.objectName === "homeSearchPull"); onActivated: music.toggle() }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (window.listeningOpen) window.listeningOpen = false
            else if (window.immersive) window.leaveImmersive()
            else if (window.route === "home" && pages.item && pages.item.dismissLayer) pages.item.dismissLayer()
            else if (window.route === "player" && pages.item && pages.item.shelfOpen) { pages.item.shelfOpen = false; pages.item.wakeTools() }
            else if (window.route === "player" && pages.item && pages.item.editing) pages.item.finishEditing()
            else if (window.route === "settings") window.leaveSettings()
            else if (window.route !== "home") window.go("home")
        }
    }
    Shortcut { sequence: "F11"; onActivated: window.togglePlaybackFullscreen() }
    Shortcut { sequence: "Alt+Left"; enabled: !window.immersive; onActivated: window.goBack() }
    Shortcut { sequence: "Alt+Right"; enabled: !window.immersive; onActivated: window.goForward() }
    Shortcut { sequence: "Ctrl+K"; enabled: window.bravaisHomeScene; onActivated: if (pages.item) pages.item.openSearch() }

    Sidebar {
        id: sidebar
        visible: window.browseChrome && !window.immersive
        opacity: window.launched ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durBase } }
        width: Theme.sidebarW; height: parent.height - Theme.playerbarH
        route: window.route
        settingsMode: window.route === "settings"
        settingsSection: window.settingsSection
        sections: window.settingsSections
        onNavigate: page => window.go(page)
        onEnterCurtain: window.changeMode("curtain")
        onOpenSettings: section => window.openSettings(section)
        onLeaveSettings: window.leaveSettings()
        onOpenPlaylist: id => window.openPlaylist(id)
    }

    Rectangle {
        id: sidebarResize
        objectName: "sidebarResizeHandle"
        visible: sidebar.visible; z: 12
        x: sidebar.width - width / 2; y: Theme.titlebarH
        width: 8; height: sidebar.height - y
        color: resizeMouse.containsMouse || resizeMouse.pressed || activeFocus ? Theme.accentSoft : "transparent"
        activeFocusOnTab: true
        Accessible.role: Accessible.Separator
        Accessible.name: "调整侧边栏宽度"
        function resizeBy(delta) { appearanceState.sidebarWidth += delta }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Left) resizeBy(event.modifiers & Qt.ShiftModifier ? -40 : -10)
            else if (event.key === Qt.Key_Right) resizeBy(event.modifiers & Qt.ShiftModifier ? 40 : 10)
            else if (event.key === Qt.Key_Home) appearanceState.sidebarWidth = 200
            else if (event.key === Qt.Key_End) appearanceState.sidebarWidth = 360
            else return
            event.accepted = true
        }
        MouseArea {
            id: resizeMouse; anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.SplitHCursor
            property real startX: 0
            property int startWidth: 232
            onPressed: mouse => { parent.forceActiveFocus(); startX = mapToItem(window.contentItem, mouse.x, mouse.y).x; startWidth = appearanceState.sidebarWidth }
            onPositionChanged: mouse => { if (pressed) appearanceState.sidebarWidth = Math.round(startWidth + mapToItem(window.contentItem, mouse.x, mouse.y).x - startX) }
            onDoubleClicked: appearanceState.sidebarWidth = 232
        }
    }

    Item {
        id: main
        visible: !window.immersive
        opacity: window.launched ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durBase } }
        x: window.browseChrome ? Theme.sidebarW : 0; y: window.bravaisHomeScene ? 0 : Theme.titlebarH
        width: parent.width - x
        height: parent.height - y - (window.playbackBarVisible ? Theme.playerbarH : 0)
        TopBar {
            id: topbar
            visible: window.browseChrome
            width: parent.width; viewportWidth: window.width
            canGoBack: window.backStack.length > 0
            canGoForward: window.forwardStack.length > 0
            onBack: window.goBack()
            onForward: window.goForward()
            onSearch: query => window.runSearch(query)
        }
        Loader {
            id: pages
            objectName: "mainPageLoader"
            active: !window.immersive
            y: topbar.visible ? topbar.height : 0
            width: parent.width; height: parent.height - y
            sourceComponent: window.route === "settings" ? settingsPage
                : window.route === "player" ? playerPage
                : window.route === "library" || window.route === "playlist" ? libraryPage
                : window.route === "history" ? historyPage
                : window.route === "stats" ? statsPage
                : window.route === "favorites" ? localFavoritesPage
                : window.route === "equalizer" ? equalizerPage
                : window.route === "wall" ? wallPage
                : window.route === "search" ? paperPage
                : window.paper ? paperPage : curtainPage
            // Page entry animation: fade + rise (Mint .24s power3.out
            // from 8px; Aurora .28s power2.out from 12px), out-in mode.
            opacity: 0
            transform: Translate { id: pageSlide }
            onLoaded: pageIn.restart()
        }
        ParallelAnimation {
            id: pageIn
            objectName: "pageEnterAnimation"
            NumberAnimation { target: pages; property: "opacity"; from: 0; to: 1; duration: music.reducedMotion ? 0 : Theme.pageEnterMs; easing.type: Theme.pageEnterEasing }
            NumberAnimation { target: pageSlide; property: "y"; from: window.bravaisHomeScene ? 0 : Theme.pageEnterY; to: 0; duration: music.reducedMotion ? 0 : Theme.pageEnterMs; easing.type: Theme.pageEnterEasing }
        }
    }
    Component {
        id: paperPage
        PaperPage { results: window.route === "search"; onHome: window.go("home"); onOpenPlayer: window.navigate("player"); onOpenPlaylist: playlist => window.openPublicPlaylist(playlist); onOpenAlbum: album => window.openAlbum(album) }
    }
    Component {
        id: curtainPage
        BravaisHome {
            selectedTab: window.bravaisTab; trailingInset: titleBar.controlsWidth; queueOpen: window.listeningOpen
            onStartWindowMove: window.startSystemMove()
            onToggleMaximize: titleBar.toggleMaximize()
            onSelectedTabChanged: window.bravaisTab = selectedTab
            onWall: window.navigate("wall")
            onPlayer: window.navigate("player")
            onAccount: window.openSettings("account")
            onSettings: window.openSettings("appearance")
            onQueue: { if (window.listeningOpen) window.listeningOpen=false; else { window.queueExpanded=true; window.listeningOpen=true } }
            onPaper: window.changeMode("paper")
            onNavigate: page => window.go(page)
            onOpenPlaylist: (collection, personal) => personal ? window.openPlaylist(String(collection.id)) : window.openPublicPlaylist(collection)
            onOpenAlbum: album => window.openAlbum(album)
        }
    }
    Component {
        id: wallPage
        PosterWallPage {
            onBack: window.navigate("home"); onPlayer: window.navigate("player")
            onListen: window.listeningOpen = !window.listeningOpen
        }
    }
    Component {
        id: playerPage
        PlayerPage {
            viewportWidth: window.width; viewportHeight: window.height; queueOpen: window.listeningOpen
            onBack: window.goBack(); onOpenHome: window.go("home"); onOpenSearch: window.go("search")
            onOpenImmersive: window.enterImmersive(true); onToggleQueue: window.listeningOpen = !window.listeningOpen
        }
    }
    Component {
        id: libraryPage
        LibraryPage { catalog: window.route === "library"; onOpenPlaylist: id => window.openPlaylist(id); onOpenAccount: window.openSettings("account"); onAddToPlaylist: track => window.addToPlaylist(track) }
    }
    Component { id: historyPage; HistoryPage { onAddToPlaylist: track => window.addToPlaylist(track) } }
    Component { id: localFavoritesPage; LocalFavoritesPage { onAddToPlaylist: track => window.addToPlaylist(track); onPlay: (tracks, index) => { music.playTracks(tracks, index); window.go("player") } } }
    Component { id: statsPage; StatsPage { } }
    Component { id: equalizerPage; EqualizerPage { } }
    Connections {
        target: libraryState
        function onPlayRequested(tracks, index) { music.playTracks(tracks, index); window.go("player") }
    }
    Connections { target: historyState; function onPlayRequested(tracks, index) { music.playTracks(tracks, index); window.go("player") } }
    Component {
        id: settingsPage
        SettingsPage {
            meta: window.settingsMeta; viewportWidth: window.width
            onEnterPaper: window.changeMode("paper")
            onEnterCurtain: window.changeMode("curtain")
            onOpenEqualizer: window.go("equalizer")
        }
    }

    PlayerBar {
        visible: window.playbackBarVisible
        onAddToPlaylist: track => window.addToPlaylist(track)
        opacity: window.launched ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durBase } }
        y: parent.height - height; width: parent.width
        queueOpen: window.listeningOpen
        lyricView: window.route === "player"
        onOpenPlayer: window.navigate("player")
        onOpenImmersive: window.enterImmersive(true)
        // usePlayerControls.toggleLyricView: the lyric page or back home.
        onToggleLyrics: window.route === "player" ? window.go("home") : window.navigate("player")
        onToggleQueue: window.listeningOpen = !window.listeningOpen
    }

    TitleBar {
        id: titleBar
        scene: window.bravaisHomeScene ? "overlay" : window.curtainScene ? "wall" : "chrome"
        z: 10
        visible: !window.immersive
        opacity: window.launched ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durBase } }
        width: parent.width
        win: window
    }

    Loader {
        id: immersivePage
        objectName: "immersiveLoader"
        anchors.fill: parent; z: 15
        active: window.immersive
        sourceComponent: ImmersivePlayerPage {
            win: window; queueOpen: window.listeningOpen
            onClose: window.leaveImmersive()
            onFullscreen: window.togglePlaybackFullscreen()
            onToggleQueue: window.listeningOpen = !window.listeningOpen
            onOpenSettings: { window.leaveImmersive(); window.openSettings("appearance") }
        }
        onLoaded: item.forceActiveFocus()
    }

    // .player-bar-notices: toasts stacked 4px apart, right-aligned 8px above
    // the player bar — the 2s loop / favourite notice (showToast) over the
    // sticky playback status.
    property string notice: ""
    Timer { id: noticeTimer; interval: 2000; onTriggered: window.notice = "" }
    Connections {
        target: music
        function onNotice(text) { window.notice = text; noticeTimer.restart() }
    }
    Item {
        objectName: "playerNotices"; z: 30
        readonly property bool noticesAtTop: window.bravaisHomeScene
        readonly property real noticeInset: noticesAtTop && window.listeningOpen && queueLoader.item && queueLoader.item.objectName === "queuePanel" ? queueLoader.item.cardWidth + 12 : 0
        readonly property real toastWidth: Math.min(window.width/2,Theme.rem(32),Math.max(0,window.width-32-noticeInset))
        anchors.right: parent.right; anchors.rightMargin: 16+noticeInset
        y: noticesAtTop ? 64 : window.height-(window.playbackBarVisible?Theme.playerbarH:0)-8-height
        width: Math.max(noticeToast.visible?noticeToast.width:0,messageToast.visible?messageToast.width:0)
        height: (noticeToast.visible?noticeToast.height:0)+(messageToast.visible?messageToast.height:0)+(noticeToast.visible&&messageToast.visible?4:0)
        Toast {
            id: noticeToast; objectName: "noticeToast"; anchors.right: parent.right
            y: parent.noticesAtTop ? (messageToast.visible?messageToast.height+4:0) : 0
            wall: window.curtainScene; text: window.notice; maxWidth: parent.toastWidth
        }
        Toast {
            id: messageToast; objectName: "messageToast"; anchors.right: parent.right
            y: parent.noticesAtTop ? 0 : (noticeToast.visible?noticeToast.height+4:0)
            wall: window.curtainScene; text: music.message; maxWidth: parent.toastWidth
        }
    }
    Loader {
        id: queueLoader
        active: window.listeningOpen
        x: window.immersive ? 0 : main.x; y: window.immersive ? 48 : window.bravaisHomeScene ? Theme.wallBarH : main.y
        width: window.immersive ? window.width : main.width
        height: window.immersive ? window.height - 124 : main.height-(window.bravaisHomeScene?Theme.wallBarH:0); z: 20
        sourceComponent: window.queueExpanded || (window.browseChrome && !window.immersive) ? queuePanel : listeningPanel
    }
    Component {
        id: queuePanel
        QueuePanel { wall: window.curtainScene && !window.immersive; onClose: window.listeningOpen = false; onAddToPlaylist: track => window.addToPlaylist(track) }
    }
    Component {
        id: listeningPanel
        ListeningPanel {
            wall: window.route === "wall"
            onClose: window.listeningOpen = false
            onPaper: window.changeMode("paper")
            onSettings: window.openSettings("appearance")
            onAccount: window.openSettings("account")
            onNavigate: page => window.go(page)
            onSearch: query => window.runSearch(query)
            onPlayer: { window.listeningOpen = false; window.navigate("player") }
            onQueue: window.queueExpanded = true
            onToggleWall: window.navigate(window.route === "wall" ? "home" : "wall")
        }
    }
}
