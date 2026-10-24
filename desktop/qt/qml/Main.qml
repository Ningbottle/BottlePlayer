import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import BottleMusic

// Application window grid: a 36px titlebar floating over a full-height sidebar
// pane, the topbar + page in the main column, and the player bar spanning the
// window bottom. Browsing and playback share one navigation shell and queue.
ApplicationWindow {
    id: window
    objectName: "mainWindow"
    visible: true; width: 1280; height: 850; minimumWidth: 900; minimumHeight: 620
    title: "BottleMusic"
    color: Theme.bg
    palette.window: Theme.bgElevated
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.buttonText: Theme.text
    palette.button: Theme.surface
    palette.base: Theme.bg
    palette.alternateBase: Theme.surface
    palette.light: Theme.surfaceHover
    palette.midlight: Theme.surfaceActive
    palette.mid: Theme.border
    palette.dark: Theme.borderStrong
    palette.shadow: Theme.shadowPopColor
    palette.toolTipBase: Theme.bgElevated
    palette.toolTipText: Theme.text
    palette.placeholderText: Theme.text3
    palette.accent: Theme.accent
    font.family: Theme.fontUi
    font.pixelSize: Theme.fsMd
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentContrast
    flags: Qt.Window | Qt.FramelessWindowHint
    property string route: "home"
    onRouteChanged: { homeState.setActive(route === "home"); if (route !== "player") cancelCoverFlight() }
    property string settingsSection: "appearance"
    property bool listeningOpen: false
    // Keep the queue card alive for its exit animation after it closes.
    property bool queueLingering: false
    readonly property real queueCardWidth: Math.round(Theme.clamp(320, main.width * 0.28, 390))
    onListeningOpenChanged: {
        if (listeningOpen) { queueLinger.stop(); queueLingering = false; return }
        if (queueLoader.item && queueLoader.item.objectName === "queuePanel") { queueLingering = true; queueLinger.restart() }
    }
    property bool immersive: false
    onImmersiveChanged: if (immersive) cancelCoverFlight()
    onWidthChanged: cancelCoverFlight()
    onHeightChanged: cancelCoverFlight()
    property var backStack: []
    property var forwardStack: []
    readonly property bool playbackBarVisible: !immersive
    readonly property var settingsSections: [
        { id: "account", label: "账户与会员", icon: "user-round" },
        { id: "appearance", label: "外观", icon: "palette" },
        { id: "playback", label: "播放", icon: "sliders-vertical" },
        { id: "storage", label: "存储与缓存", icon: "hard-drive" },
        { id: "experimental", label: "实验室", icon: "flask-conical" },
        { id: "diagnostics", label: "诊断", icon: "bug" },
        { id: "about", label: "关于与更新", icon: "info" }
    ]
    readonly property var settingsMeta: settingsSections.find(section => section.id === settingsSection) || settingsSections[1]

    Binding { target: Theme; property: "dark"; value: appearanceState.dark }
    Binding { target: Theme; property: "customAccent"; value: appearanceState.accent }
    Binding { target: Theme; property: "compactList"; value: appearanceState.compact }
    Binding { target: Theme; property: "lyricFontSetting"; value: appearanceState.lyricFont }
    Binding { target: Theme; property: "lyricFocus"; value: appearanceState.lyricFocus }
    // Device-pixel ratio for the hairline rules (survives monitor moves).
    Binding { target: Theme; property: "dpr"; value: window.Screen.devicePixelRatio }


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

    Shortcut { sequence: "Space"; enabled: !window.searchInputFocused() && !(window.activeFocusItem instanceof AbstractButton); onActivated: music.toggle() }
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
    Shortcut { sequence: "Ctrl+K"; enabled: !window.immersive; onActivated: topbar.focusSearch() }

    Sidebar {
        id: sidebar
        visible: !window.immersive
        opacity: window.launched ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durBase; easing.type: Easing.OutCubic } }
        width: Theme.sidebarW; height: parent.height - Theme.playerbarH
        route: window.route
        settingsMode: window.route === "settings"
        settingsSection: window.settingsSection
        sections: window.settingsSections
        onNavigate: page => window.go(page)
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
            property bool engaged: false
            onPressed: mouse => { parent.forceActiveFocus(); startX = mapToItem(window.contentItem, mouse.x, mouse.y).x; startWidth = appearanceState.sidebarWidth; engaged = false }
            onPositionChanged: mouse => {
                if (!pressed) return
                const dx = mapToItem(window.contentItem, mouse.x, mouse.y).x - startX
                if (!engaged && Math.abs(dx) >= Theme.dragDeadzone) engaged = true
                if (engaged) appearanceState.sidebarWidth = Math.round(startWidth + dx)
            }
            // Drop the focus ring after mouse use: it is an accent-coloured
            // strip, so leaving activeFocus on would keep it emphasised until
            // the next click elsewhere. Keyboard users re-arm it via Tab.
            onReleased: { engaged = false; parent.focus = false }
            onDoubleClicked: appearanceState.sidebarWidth = 232
        }
    }

    Item {
        id: main
        visible: !window.immersive
        opacity: window.launched ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durBase; easing.type: Easing.OutCubic } }
        x: Theme.sidebarW; y: Theme.titlebarH
        width: parent.width - x
        height: parent.height - y - (window.playbackBarVisible ? Theme.playerbarH : 0)
        TopBar {
            id: topbar
            width: parent.width; viewportWidth: window.width
            onOpenSettings: section => window.go("settings", section)
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
                : paperPage
            // Page entry animation: fade + rise (Mint .24s power3.out
            // from 8px; Aurora .28s power2.out from 12px), out-in mode.
            opacity: 0
            transform: Translate { id: pageSlide }
            onLoaded: {
                pageIn.restart()
                if (!window.coverFlying) return
                const flightOwner = window.flightGeneration
                // Deferred so the player page's card layout has settled and
                // coverSlotRect() reports the final slot, not a half-laid frame.
                Qt.callLater(() => {
                    if (flightOwner !== window.flightGeneration) return
                    if (!window.coverFlying || !pages.item || pages.item.objectName !== "playerPage") { window.cancelCoverFlight(); return }
                    const slot = pages.item.coverSlotRect()
                    if (slot.width <= 0 || !pages.item.hasTrack) { window.cancelCoverFlight(); return }
                    window.flightPage = pages.item
                    const t = pages.item.mapToItem(window.contentItem, slot.x, slot.y)
                    // pageIn's temporary translation is not the settled slot.
                    flightCover.target = Qt.rect(t.x - pageSlide.x, t.y - pageSlide.y, slot.width, slot.height)
                    flightCover.x = window.coverFrom.x; flightCover.y = window.coverFrom.y
                    flightCover.width = window.coverFrom.width; flightCover.height = window.coverFrom.height
                    // Preselect the destination's image bucket, so animation
                    // geometry never reloads its source at a bucket boundary.
                    flightCover.pixels = Math.max(512, Math.ceil(Math.max(slot.width, slot.height) * Theme.dpr))
                    flightCover.radius = Theme.rMd
                    flightCover.pending = true; flightTimeout.restart()
                    window.startCoverFlight()
                })
            }
        }
        ParallelAnimation {
            id: pageIn
            objectName: "pageEnterAnimation"
            NumberAnimation { target: pages; property: "opacity"; from: 0; to: 1; duration: music.reducedMotion ? 0 : Theme.pageEnterMs; easing.type: Theme.pageEnterEasing }
            NumberAnimation { target: pageSlide; property: "y"; from: Theme.pageEnterY; to: 0; duration: music.reducedMotion ? 0 : Theme.pageEnterMs; easing.type: Theme.pageEnterEasing }
        }
    }
    Component {
        id: paperPage
        PaperPage { results: window.route === "search"; onHome: window.go("home"); onOpenPlayer: window.navigate("player"); onOpenPlaylist: playlist => window.openPublicPlaylist(playlist); onOpenAlbum: album => window.openAlbum(album) }
    }
    Component {
        id: playerPage
        PlayerPage {
            viewportWidth: window.width; viewportHeight: window.height; queueOpen: window.listeningOpen
            queueReserve: window.listeningOpen && queueLoader.item && queueLoader.item.objectName === "queuePanel" ? queueLoader.item.cardWidth + 12 : 0
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
            onOpenEqualizer: window.go("equalizer")
        }
    }

    PlayerBar {
        id: playerBar
        visible: window.playbackBarVisible
        onAddToPlaylist: track => window.addToPlaylist(track)
        opacity: window.launched ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durBase; easing.type: Easing.OutCubic } }
        y: parent.height - height; width: parent.width
        queueOpen: window.listeningOpen
        lyricView: window.route === "player"
        onOpenPlayer: window.flyCoverToPlayer()
        onOpenImmersive: window.enterImmersive(true)
        // usePlayerControls.toggleLyricView: the lyric page or back home.
        onToggleLyrics: window.route === "player" ? window.go("home") : window.navigate("player")
        onToggleQueue: window.listeningOpen = !window.listeningOpen
    }

    TitleBar {
        id: titleBar
        z: 10
        visible: !window.immersive
        opacity: window.launched ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durBase; easing.type: Easing.OutCubic } }
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
        readonly property real noticeInset: window.listeningOpen && queueLoader.item && queueLoader.item.objectName === "queuePanel" ? queueLoader.item.cardWidth + 12 : 0
        readonly property real toastWidth: Math.min(window.width/2,Theme.rem(32),Math.max(0,window.width-32-noticeInset))
        anchors.right: parent.right; anchors.rightMargin: 16+noticeInset
        y: window.height-(window.playbackBarVisible?Theme.playerbarH:0)-8-height
        width: Math.max(noticeToast.visible?noticeToast.width:0,messageToast.visible?messageToast.width:0)
        height: (noticeToast.visible?noticeToast.height:0)+(messageToast.visible?messageToast.height:0)+(noticeToast.visible&&messageToast.visible?4:0)
        Toast {
            id: noticeToast; objectName: "noticeToast"; anchors.right: parent.right
            y: 0
            text: window.notice; maxWidth: parent.toastWidth
        }
        Toast {
            id: messageToast; objectName: "messageToast"; anchors.right: parent.right
            y: noticeToast.visible?noticeToast.height+4:0
            text: music.message; maxWidth: parent.toastWidth
        }
    }
    Loader {
        id: queueLoader
        active: window.listeningOpen || window.queueLingering
        x: window.immersive ? 0 : main.x; y: window.immersive ? 48 : main.y
        width: window.immersive ? window.width : main.width
        height: window.immersive ? window.height - 124 : main.height; z: 20
        sourceComponent: queuePanel
    }
    Timer { id: queueLinger; interval: music.reducedMotion ? 1 : Theme.durQueueOut + 20; onTriggered: window.queueLingering = false }
    // Shared-element cover flight (open-claude-ui): the player bar artwork
    // grows into the player page's cover slot while the destination stays
    // hidden, then hands over in one frame.
    property rect coverFrom: Qt.rect(0, 0, 0, 0)
    property bool coverFlying: false
    property int flightGeneration: 0
    property Item flightPage: null
    property string flightTrack: ""
    property string flightImage: ""
    function cancelCoverFlight() {
        if (!coverFlying) return
        ++flightGeneration
        coverFlying = false
        flightCover.pending = false; flightTimeout.stop(); coverFlight.stop()
        flightCover.visible = false
        if (flightPage) flightPage.hideCover = false
        flightPage = null
    }
    function validateCoverFlight() {
        if (coverFlying && (route !== "player" || immersive || music.reducedMotion || music.vinyl
                           || String(music.current.hash || "") !== flightTrack || String(music.current.image || "") !== flightImage))
            cancelCoverFlight()
    }
    function startCoverFlight() {
        validateCoverFlight()
        if (!coverFlying || !flightPage || !flightCover.ready || !flightPage.coverReady) return
        flightCover.pending = false; flightTimeout.stop()
        flightPage.hideCover = true; flightCover.visible = true; coverFlight.restart()
    }
    Connections {
        target: music
        function onPlaybackChanged() { window.validateCoverFlight() }
        function onQueueChanged() { window.validateCoverFlight() }
        function onAppearanceChanged() { window.validateCoverFlight() }
    }
    Connections { target: appearanceState; function onSidebarWidthChanged() { window.cancelCoverFlight() } }
    Connections { target: playerLayoutState; function onCardsChanged() { window.cancelCoverFlight() } }
    Connections { target: Theme; function onDprChanged() { window.cancelCoverFlight() } }
    Connections {
        target: window.flightPage
        function onCoverReadyChanged() {
            if (coverFlight.running && !window.flightPage.coverReady) window.cancelCoverFlight()
            else if (flightCover.pending) window.startCoverFlight()
        }
    }
    function flyCoverToPlayer() {
        if (window.route === "player") return
        window.cancelCoverFlight()
        if (music.reducedMotion || music.vinyl || !music.current.image || !playerBar.visible) { navigate("player"); return }
        const p = playerBar.coverItem.mapToItem(window.contentItem, 0, 0)
        window.coverFrom = Qt.rect(p.x, p.y, playerBar.coverItem.width, playerBar.coverItem.height)
        window.flightTrack = String(music.current.hash || ""); window.flightImage = String(music.current.image || "")
        ++window.flightGeneration
        window.coverFlying = true
        window.navigate("player")
    }
    Cover {
        id: flightCover; objectName: "coverFlight"; z: 25
        visible: false
        url: window.flightImage; pixels: 512; radius: Theme.rMd
        property rect target: Qt.rect(0, 0, 0, 0)
        property bool pending: false
        onReadyChanged: if (ready && pending) window.startCoverFlight()
    }
    ParallelAnimation {
        id: coverFlight
        NumberAnimation { target: flightCover; property: "x"; to: flightCover.target.x; duration: Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeExpand }
        NumberAnimation { target: flightCover; property: "y"; to: flightCover.target.y; duration: Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeExpand }
        NumberAnimation { target: flightCover; property: "width"; to: flightCover.target.width; duration: Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeExpand }
        NumberAnimation { target: flightCover; property: "height"; to: flightCover.target.height; duration: Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeExpand }
        NumberAnimation { target: flightCover; property: "radius"; to: Theme.rCard; duration: Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeExpand }
        onStopped: window.cancelCoverFlight()
    }
    Timer {
        id: flightTimeout; interval: 400
        onTriggered: window.cancelCoverFlight()
    }
    Component {
        id: queuePanel
        QueuePanel {
            wall: window.immersive
            cardShown: window.listeningOpen; preferredWidth: window.queueCardWidth
            onClose: window.listeningOpen = false; onAddToPlaylist: track => window.addToPlaylist(track) }
    }
}
