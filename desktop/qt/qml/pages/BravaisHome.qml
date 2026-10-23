import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

// Continuous cover wall with edge navigation and a pull-down search page.
// Folia-inspired packing keeps in-place focus; controllers own all data/audio.
Item {
    id: root
    objectName: "bravaisHome"
    clip: true
    property string selectedTab: "playlists"
    property bool searchOpen: false
    property bool sidebarOpen: false
    property string submittedTerm: ""
    property real trailingInset: 118
    property bool queueOpen: false
    signal startWindowMove()
    signal toggleMaximize()
    property bool lights: true
    property var selected: null
    signal openPlaylist(var collection, bool personal)
    signal openAlbum(var album)
    signal account()
    signal settings()
    signal queue()
    signal player()
    signal wall()
    signal paper()
    signal navigate(string page)
    // The agreed home is a continuous collage. Navigation opens at the edge
    // and never creates a gap or divides the artwork into two halves.
    readonly property real seamWidth: 0
    readonly property real zoom: Theme.bravaisZoom(width,height)
    readonly property var expansion: selected ? BravaisLayout.expand(selected, items.length) : []
    readonly property var browseItems: selectedTab === "playlists" ? collections(homeState.playlists, false)
        : selectedTab === "albums" ? albums(homeState.recommendedAlbums)
        : selectedTab === "mine" ? collections(libraryState.playlists, true)
        : selectedTab === "favorites" ? tracks(favoriteState.localTracks, "favorites")
        : tracks(music.feed, "feed")
    readonly property var items: browseItems
    readonly property var queueKeys: {
        var keys = Object.create(null)
        for (var i = 0; i < music.queue.length; ++i) keys[music.queue[i].hash] = true
        return keys
    }
    readonly property bool loading: selectedTab === "mine" ? libraryState.playlistsLoading : selectedTab === "favorites" ? false : homeState.loading
    readonly property string error: selectedTab === "mine" ? libraryState.playlistsError : selectedTab === "favorites" ? "" : homeState.error
    function tracks(rows, source) {
        return rows.map(function(row, i) { return { key: row.hash, title: row.title, artist: row.artist, image: row.image,
            kind: "track", track: row, address: i, source: source } })
    }
    function collections(rows, personal) {
        return rows.map(function(row) { return { key: "playlist:" + row.id, title: row.name,
            artist: row.count > 0 || personal && row.count === 0 ? row.count + " 首歌曲" : personal ? "个人歌单" : "公开歌单", image: row.image,
            // Public catalogue metadata can report zero for nonempty lists.
            // Keep the previous display convention: public zero is unknown;
            // a personal list's explicit zero remains a valid empty count.
            kind: "playlist", count: personal || Number(row.count)>0 ? row.count : -1, collection: row, personal: personal } })
    }
    function albums(rows) {
        return rows.map(function(row) { return { key: "album:" + row.id, title: row.name, artist: row.artist, image: row.image,
            kind: "album", collection: row } })
    }
    function chooseTab(tab) {
        searchOpen = false; sidebarOpen = false; selected = null; selectedTab = tab
        if (tab === "mine" && accountState.loggedIn && !libraryState.playlists.length) libraryState.refreshPlaylists()
        field.reset()
    }
    function queued(item) {
        return !!item.track && !!item.track.hash && !!queueKeys[item.track.hash]
    }
    function play(item) {
        if (item.kind === "album") { root.openAlbum(item.collection); return }
        if (item.kind === "playlist") { root.openPlaylist(item.collection, item.personal); return }
        var sourceRows = item.source === "favorites" ? favoriteState.localTracks : item.source === "search" ? music.searchResults : music.feed
        var sameQueue = !music.radio && music.queueIndex === item.address && music.queue.length === sourceRows.length
            && sourceRows.every(function(row,index) { return row.hash === music.queue[index].hash })
        if (sameQueue && item.track.hash === music.current.hash) { music.toggle(); return }
        if (item.source === "favorites") music.playTracks(favoriteState.localTracks, item.address)
        else music.playFrom(item.source, item.address)
    }
    function enqueue(item) {
        if (item.kind === "album") { root.openAlbum(item.collection); return }
        if (item.kind === "playlist") { root.openPlaylist(item.collection, item.personal); return }
        music.enqueueTrack(item.track)
    }
    function focusRect(rect) {
        var shift = rect.x < 0 ? -seamWidth / 2 : seamWidth / 2
        camera.stop()
        cameraX.to = rect.x + rect.w / 2 + shift / zoom
        cameraY.to = rect.y + rect.h / 2
        camera.start()
    }
    function activate(slot) {
        if (selected && selected.key === slot.key) return
        selected = slot; focusRect(BravaisLayout.expand(slot, items.length)[slot.slot])
    }
    function locate(address) {
        if (!items.length) return
        var block = Math.floor(address / BravaisLayout.slots), p = BravaisLayout.period(items.length)
        activate(BravaisLayout.at(block % p.columns, Math.floor(block / p.columns), address % BravaisLayout.slots, items.length))
    }
    function locateCurrent() {
        for (var i = 0; i < items.length; i++) if (items[i].track && items[i].track.hash === music.current.hash) { locate(i); return }
        chooseTab("recommend")
        for (var j = 0; j < items.length; j++) if (items[j].track && items[j].track.hash === music.current.hash) { locate(j); return }
    }
    function openSearch() { sidebarOpen=false; tools.close(); moreMenu.close(); searchOpen=true; Qt.callLater(searchPage.focusSearch) }
    function closeSearch() { searchOpen = false; forceActiveFocus() }
    function submitSearch(term, append) {
        var t=term.trim(); if (!t || !music.ready) return
        submittedTerm=t; music.search(t,!!append)
    }
    function refreshTab() { if (selectedTab === "mine") libraryState.refreshPlaylists(); else if (selectedTab !== "favorites") homeState.refresh() }
    function dismissLayer() {
        if (moreMenu.opened) { moreMenu.close(); return }
        if (tools.opened) { tools.close(); return }
        if (sidebarOpen) { sidebarOpen=false; return }
        if (searchOpen) { closeSearch(); return }
        if (selected) { selected=null; return }
    }
    onItemsChanged: if (selected && selected.address >= items.length) selected = null

    Rectangle { anchors.fill: parent; color: Theme.wallBg }
    ParallelAnimation {
        id: camera
        NumberAnimation { id: cameraX; target: field; property: "centerX"; duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic }
        NumberAnimation { id: cameraY; target: field; property: "centerY"; duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic }
    }
    // A Loader in a recycled delegate must create its face in the page's
    // context. The delegate context can disappear while an animation retires.
    Component {
        id: tileContent
        BravaisTile {
            readonly property Item placement: parent ? parent.parent : null
            objectName: !placement ? "" : placement.expanded ? "bravaisFocusedTile" : "bravaisTile_" + placement.original.key
            item: placement ? placement.modelItem : ({})
            expanded: !!placement && placement.expanded
            pixelScale: root.zoom; dimmed: !root.lights
            // Suppress only captions covered by the lower-left controls. Keep
            // the photographs continuous instead of adding an opaque footer.
            captionVisible: {
                if (!placement || placement.expanded) return true
                var left=placement.x*root.zoom-field.contentX
                var bottom=(placement.y+placement.height)*root.zoom-field.contentY
                return !(left<390 && left+placement.width*root.zoom>0 && bottom>root.height-100 && bottom<root.height+72)
            }
            current: !!item.track && !!item.track.hash && item.track.hash === music.current.hash
            queued: root.queued(item)
            onActivate: if (placement) root.activate(placement.original)
            onPlay: root.play(item)
            onEnqueue: root.enqueue(item)
            onOpenPlayer: root.player()
            onClose: root.selected = null
        }
    }
    Flickable {
        id: field; objectName: "bravaisField"; width: parent.width; height: parent.height; clip: true
        readonly property real originX: 64 * BravaisLayout.blockWidth
        readonly property real originY: 64 * BravaisLayout.blockHeight
        contentWidth: originX * 2 * root.zoom; contentHeight: originY * 2 * root.zoom
        boundsBehavior: Flickable.StopAtBounds
        onMovementStarted: camera.stop()
        property real centerX: 0
        property real centerY: BravaisLayout.blockHeight / 2
        contentX: (originX + centerX) * root.zoom - width / 2
        contentY: (originY + centerY) * root.zoom - height / 2
        function center() { return Qt.point((contentX + width/2) / root.zoom - originX, (contentY + height/2) / root.zoom - originY) }
        // The screen offsets are derived from a persistent world centre.
        // Native Flickable movement updates it; resizing never rewrites it.
        onContentXChanged: if (dragging || flicking) centerX = center().x
        onContentYChanged: if (dragging || flicking) centerY = center().y
        function reset() { camera.stop(); cancelFlick(); centerX = 0; centerY = BravaisLayout.blockHeight / 2 }
        readonly property real viewLeft: contentX / root.zoom - originX
        readonly property real viewTop: contentY / root.zoom - originY
        readonly property real viewRight: viewLeft + width / root.zoom
        readonly property real viewBottom: viewTop + height / root.zoom
        readonly property int column: Math.floor((viewLeft + viewRight) / 2 / BravaisLayout.blockWidth)
        readonly property int row: Math.floor((viewTop + viewBottom) / 2 / BravaisLayout.blockHeight)
        readonly property int spanX: Math.max(1, Math.ceil((width / root.zoom / 2 + 420) / BravaisLayout.blockWidth))
        readonly property int spanY: Math.max(1, Math.ceil((height / root.zoom / 2 + 420) / BravaisLayout.blockHeight))
        readonly property int poolColumns: spanX * 2 + 1
        readonly property int poolRows: spanY * 2 + 1
        Item {
            width: field.originX * 2; height: field.originY * 2
            scale: root.zoom; transformOrigin: Item.TopLeft
            Repeater {
                model: root.items.length ? field.poolColumns * field.poolRows * BravaisLayout.slots : 0
                Item {
                    id: tile; required property int index
                    readonly property int slot: index % BravaisLayout.slots
                    readonly property int pool: Math.floor(index / BravaisLayout.slots)
                    readonly property int column: field.column - field.spanX
                        + BravaisLayout.mod(pool % field.poolColumns - field.column + field.spanX, field.poolColumns)
                    readonly property int row: field.row - field.spanY
                        + BravaisLayout.mod(Math.floor(pool / field.poolColumns) - field.row + field.spanY, field.poolRows)
                    readonly property var original: BravaisLayout.at(column, row, slot, root.items.length)
                    readonly property bool expandedBlock: !!root.selected && root.selected.column === column && root.selected.row === row
                    readonly property var rect: (expandedBlock ? root.expansion[slot] : original) || original
                    readonly property var modelItem: root.items[original.address] || ({})
                    readonly property bool expanded: expandedBlock && root.selected.slot === slot
                    readonly property bool inView: rect.x < field.viewRight + 420 && rect.x + rect.w > field.viewLeft - 420
                        && rect.y < field.viewBottom + 420 && rect.y + rect.h > field.viewTop - 420
                    x: field.originX + rect.x + (original.x < 0 ? -root.seamWidth/2 : root.seamWidth/2) / root.zoom
                    y: field.originY + rect.y; width: rect.w; height: rect.h; z: expanded ? 20 : 0
                    Behavior on x { enabled: !field.dragging && !field.flicking; NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
                    Behavior on y { enabled: tile.expandedBlock; NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
                    Behavior on width { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
                    Behavior on height { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
                    Loader {
                        anchors.fill: parent; active: tile.inView
                        sourceComponent: tileContent
                    }
                }
            }
        }
    }
    // Edge shades belong to the controls, leaving the middle of the artwork clear.
    Rectangle {
        width: 104; height: parent.height
        gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: Theme.blackA(7) } GradientStop { position: 1; color: "transparent" } }
    }
    Rectangle {
        anchors.bottom: parent.bottom; width: parent.width; height: 126
        gradient: Gradient { GradientStop { position: 0; color: "transparent" } GradientStop { position: 1; color: Theme.blackA(9) } }
    }
    MouseArea {
        objectName: "homeWindowDrag"; x: 8; y: 0; width: Math.max(0,root.width/2-96); height: 26
        onPressed: root.startWindowMove(); onDoubleClicked: root.toggleMaximize()
    }
    Column {
        id: rail; objectName: "homeEdgeRail"; x: 12; anchors.verticalCenter: parent.verticalCenter; spacing: 12
        WallIcon { objectName: "homeSidebarToggle"; glyph: "panels-top-left"; Accessible.name: sidebarOpen ? "收起侧栏" : "展开侧栏"; onClicked: { root.sidebarOpen=!root.sidebarOpen; root.searchOpen=false } }
        WallIcon { objectName: "bravaisGlobalSearch"; glyph: "search"; Accessible.name: "搜索音乐"; onClicked: root.openSearch() }
        WallIcon { objectName: "bravaisQueue"; glyph: "list-music"; checked: root.queueOpen; Accessible.name: "播放队列"; onClicked: root.queue() }
        WallIcon { objectName: "bravaisMore"; glyph: "more-horizontal"; Accessible.name: "更多操作"; onClicked: moreMenu.open() }
    }
    Rectangle {
        id: edgePanel; objectName: "homeSidebar"
        x: root.sidebarOpen ? 66 : -width; y: Math.max(50,(root.height-height)/2)
        width: 164; height: navigation.height+28; radius: 10; color: Theme.wallSurface
        visible: x > -width
        Behavior on x { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
        Column {
            id: navigation; x: 10; y: 14; width: parent.width-20; spacing: 5
            Text { x: 10; text: "BottleMusic"; color: Theme.wallChromeInk; font.family: Theme.fontDisplay; font.pixelSize: Theme.fsMd; bottomPadding: 7 }
            Repeater {
                model: [{key:"recommend",label:"今日推荐"},{key:"playlists",label:"发现歌单"},{key:"albums",label:"推荐曲的专辑"},{key:"mine",label:"我的歌单"},{key:"favorites",label:"本地收藏"}]
                WallAction {
                    required property var modelData
                    objectName: "bravaisTab_"+modelData.key; width: navigation.width; text: modelData.label
                    restFill: root.selectedTab===modelData.key ? (Theme.wallChromePressed) : "transparent"
                    onClicked: root.chooseTab(modelData.key)
                }
            }
            Rectangle { x: 12; width: parent.width-24; height: Theme.hairline; color: Theme.wallBorder }
            WallAction { objectName: "bravaisAccount"; width: navigation.width; text: "账户"; glyph: "user-round"; restFill: "transparent"; onClicked: root.account() }
            WallAction { objectName: "bravaisSettings"; width: navigation.width; text: "设置"; glyph: "settings"; restFill: "transparent"; onClicked: root.settings() }
        }
    }
    Item {
        id: pull; objectName: "homeSearchPull"; z: 7
        x: (root.width-width)/2; width: 160; height: 36; visible: !root.searchOpen
        activeFocusOnTab: true
        property real distance: 0
        Accessible.role: Accessible.Button; Accessible.name: "下拉打开搜索"
        Keys.onReturnPressed: root.openSearch()
        Keys.onSpacePressed: root.openSearch()
        Rectangle { x: (parent.width-width)/2; y: 10+pull.distance*0.28; width: 30; height: 3; radius: 1.5; color: pullMouse.containsMouse || pull.activeFocus ? Theme.wallInk : Theme.wallMuted }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; y: 21+pull.distance*0.28
            text: "下拉搜索"; color: Theme.wallInk; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
            visible: pullMouse.containsMouse || pull.activeFocus || pullMouse.pressed
        }
        MouseArea {
            id: pullMouse; anchors.fill: parent; hoverEnabled: true; preventStealing: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property point start; property real travelX: 0
            onPressed: mouse => { start=mapToItem(root,mouse.x,mouse.y); pull.distance=0; travelX=0 }
            onPositionChanged: mouse => {
                if (!pressed) return
                var point=mapToItem(root,mouse.x,mouse.y)
                pull.distance=Math.max(0,Math.min(90,point.y-start.y)); travelX=point.x-start.x
            }
            onReleased: {
                if (pull.distance>=42 || pull.distance<5 && Math.abs(travelX)<5) root.openSearch()
                pull.distance=0
            }
            onCanceled: pull.distance=0
        }
    }
    HomeSearchPage {
        id: searchPage; z: 8; opened: root.searchOpen; submittedTerm: root.submittedTerm
        onClose: root.closeSearch(); onQuery: (term,append) => root.submitSearch(term,append)
    }
    Rectangle {
        id: nowPlaying; objectName: "homeNowPlaying"; x: 18; y: root.height-height-16; z: 9
        width: 280; height: 88; radius: 12; color: Theme.wallSurface
        border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: Theme.wallBorder
        MouseArea {
            anchors.fill: parent; acceptedButtons: Qt.AllButtons; preventStealing: true
            onPressed: mouse => mouse.accepted=true
            onWheel: wheel => wheel.accepted=true
        }
        AbstractButton {
            x: 12; y: 12; width: parent.width-90; height: 49
            Accessible.name: "正在播放："+(music.current.title || "还没有歌曲"); onClicked: if (music.current.hash) root.player()
            contentItem: Item {
                Cover { width: 48; height: 48; radius: 6; url: music.current.image || ""; pixels: 120 }
                Column {
                    x: 60; anchors.verticalCenter: parent.verticalCenter; width: parent.width-x; spacing: 4
                    Text { width: parent.width; text: music.current.title || "未播放歌曲"; color: Theme.wallChromeInk; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; elide: Text.ElideRight; textFormat: Text.PlainText }
                    Text { width: parent.width; text: music.current.artist || ""; color: Theme.wallChromeMuted; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; elide: Text.ElideRight; textFormat: Text.PlainText }
                }
            }
        }
        WallIcon { objectName: "homeCardPlay"; x: parent.width-76; y: 21; media: music.playing ? "pause" : "play"; mediaBox: 30; mediaGlyph: 15; enabled: !!music.current.hash; Accessible.name: music.playing ? "暂停" : "播放"; onClicked: music.toggle() }
        WallIcon { objectName: "bravaisTools"; x: parent.width-42; y: 21; glyph: "more-horizontal"; Accessible.name: "播放选项"; onClicked: tools.opened ? tools.close() : tools.open() }
        Rectangle {
            objectName: "homeCardProgress"; x: 13; y: parent.height-15; width: parent.width-26; height: 2; radius: 1; color: Theme.wallBorderStrong
            Rectangle { width: parent.width*Math.max(0,Math.min(1,music.duration>0 ? music.position/music.duration : 0)); height: parent.height; radius: 1; color: Theme.wallAccent }
        }
    }
    component WallIcon: IconButton {
        boxSize: 30; radius: width/2; glyphSize: 15
        tint: Theme.wallChromeMuted; hoverTint: Theme.wallChromeInk; checkedTint: Theme.customized ? Theme.wallAccent : Theme.wallChromeInk
        hoverFill: Theme.wallChromeHover; downFill: Theme.wallChromePressed; focusInk: Theme.wallFocus
    }
    component WallAction: ActionButton {
        quiet: true; implicitHeight: 28; bgRadius: height/2; typeSize: 12
        restFill: Theme.wallChromeHover; restInk: Theme.wallChromeInk; hoverInk: Theme.wallChromeInk
        hoverFill: Theme.wallChromePressed; downFill: Theme.wallChromePressed; edgeColor: "transparent"; focusInk: Theme.wallFocus
    }
    Column {
        objectName: "bravaisCentreState"; visible: !root.items.length
        x: Math.max(24,(root.width-width)/2); width: Math.min(400,root.width-48)
        y: (root.height-height)/2; spacing: 12
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: root.loading ? "正在加载…"
                : root.selectedTab === "mine" ? accountState.loggedIn ? "还没有个人歌单" : "登录，打开你的歌单"
                : root.selectedTab === "favorites" ? "还没有本地收藏" : "这里还没有内容"
            color: Theme.wallChromeInk; font.family: Theme.fontUi; font.pixelSize: Theme.fsXl; font.weight: Theme.fwSemiBold
        }
        WallAction {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !root.loading && root.selectedTab !== "favorites"
            typeSize: 13; restFill: Theme.wallChromeSurface
            hoverFill: Theme.wallHoverOn(restFill); downFill: Theme.wallDownOn(restFill)
            text: root.selectedTab === "mine" && !accountState.loggedIn ? "登录账户" : "刷新"
            enabled: root.selectedTab !== "favorites"
            onClicked: root.selectedTab === "mine" && !accountState.loggedIn ? root.account() : root.refreshTab()
        }
    }
    Popup {
        id: tools; objectName: "bravaisToolsPopup"
        x: nowPlaying.x; y: Math.max(10,nowPlaying.y-height-10)
        width: 320; padding: 16
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Plate { color: Theme.wallChromeSurface; radius: 16; elevation: 3 }
        contentItem: Column {
            spacing: 16
            AbstractButton {
                width: parent.width; height: 48; enabled: !!music.current.hash
                Accessible.name: "打开播放页"; onClicked: { tools.close(); root.player() }
                contentItem: Item {
                    Cover { width: 48; height: 48; radius: 8; url: music.current.image || ""; pixels: 120 }
                    Column {
                        x: 60; width: parent.width-x; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                        Text { width: parent.width; text: music.current.title || "此刻的声音"; textFormat: Text.PlainText; color: Theme.wallChromeInk; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; font.weight: Theme.fwSemiBold; elide: Text.ElideRight }
                        Text { width: parent.width; text: music.current.artist || "选一首歌开始"; textFormat: Text.PlainText; color: Theme.wallChromeMuted; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; elide: Text.ElideRight }
                    }
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter; spacing: 12
                WallIcon { anchors.verticalCenter: parent.verticalCenter; glyph: music.loopMode === "single" ? "repeat-1" : music.loopMode === "random" ? "shuffle" : "repeat"; checked: music.loopMode !== "list"; flatChecked: true; enabled: !music.radio; Accessible.name: "切换播放顺序"; onClicked: music.cycleLoopMode() }
                WallIcon { objectName: "bravaisTransportPrev"; anchors.verticalCenter: parent.verticalCenter; media: "prev"; mediaBox: 40; mediaGlyph: 18; enabled: !!music.current.hash && !music.radio; Accessible.name: "上一首"; onClicked: music.previous() }
                IconButton {
                    objectName: "bravaisTransportPlay"; anchors.verticalCenter: parent.verticalCenter
                    media: music.playing ? "pause" : "play"; mediaBox: 48; mediaGlyph: 24; radius: width/2; variant: "primary"
                    primaryFill: Theme.customized ? Theme.wallAccent : Theme.white
                    primaryHoverFill: Theme.customized ? Theme.actionShade(primaryFill, .08) : Theme.wallHoverOn(primaryFill)
                    primaryDownFill: Theme.customized ? Theme.actionShade(primaryFill, .16) : Theme.wallDownOn(primaryFill)
                    tint: Theme.customized ? Theme.inkOn(primaryFill) : Theme.wallChromeSurface; hoverTint: tint; focusInk: Theme.wallFocus
                    enabled: !!music.current.hash && !music.loading; Accessible.name: music.playing ? "暂停" : "播放"; onClicked: music.toggle()
                }
                WallIcon { objectName: "bravaisTransportNext"; anchors.verticalCenter: parent.verticalCenter; media: "next"; mediaBox: 40; mediaGlyph: 18; enabled: !!music.current.hash; Accessible.name: "下一首"; onClicked: music.next() }
                WallIcon { anchors.verticalCenter: parent.verticalCenter; glyph: "heart"; glyphFilled: music.favorite; checked: music.favorite; flatChecked: true; enabled: !!music.current.hash; Accessible.name: "收藏歌曲"; onClicked: music.toggleFavorite() }
            }
            Row {
                spacing: 10; width: parent.width
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "volume-2"; size: 18; color: Theme.wallChromeMuted }
                Slider {
                    id: volume; objectName: "bravaisVolume"; width: parent.width-78; height: 24; padding: 0
                    from: 0; to: 1; value: music.volume; onMoved: music.volume=value; Accessible.name: "音量"
                    background: Rectangle { y: (volume.height-height)/2; width: volume.width; height: 4; radius: 2; color: Theme.wallBorderStrong; Rectangle { width: volume.visualPosition*parent.width; height: parent.height; radius: 2; color: Theme.wallChromeInk } }
                    handle: Rectangle { x: volume.visualPosition*(volume.width-width); y: (volume.height-height)/2; width: 12; height: 12; radius: 6; color: Theme.wallChromeInk; border.pixelAligned: false; antialiasing: true; border.width: volume.visualFocus ? Theme.hairline : 0; border.color: Theme.wallFocus }
                }
                Text { anchors.verticalCenter: parent.verticalCenter; text: Math.round(music.volume*100)+"%"; color: Theme.wallChromeMuted; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.features: {"tnum":1} }
            }
            Row {
                width: parent.width; spacing: 8
                WallAction { width: (parent.width-8)/2; text: "定位歌曲"; glyph: "locate-fixed"; enabled: !!music.current.hash; onClicked: { tools.close(); root.locateCurrent() } }
                WallAction { width: (parent.width-8)/2; text: root.lights ? "熄灯" : "开灯"; glyph: root.lights ? "eye" : "eye-off"; onClicked: root.lights=!root.lights }
            }
        }
    }
    component WallMenuItem: MenuItem {
        id: menuItem
        implicitHeight: visible ? 36 : 0; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd
        leftPadding: 12; rightPadding: 12
        contentItem: Text { text: menuItem.text; textFormat: Text.PlainText; font: menuItem.font; color: Theme.wallChromeInk; verticalAlignment: Text.AlignVCenter; opacity: menuItem.enabled ? 1 : 0.56 }
        background: Rectangle { radius: 8; color: menuItem.down ? Theme.wallChromePressed : menuItem.highlighted ? Theme.wallChromeHover : "transparent"; border.pixelAligned: false; antialiasing: true; border.width: menuItem.visualFocus ? Theme.hairline : 0; border.color: Theme.wallFocus }
        indicator: null; arrow: null
    }
    Menu {
        id: moreMenu; objectName: "bravaisMoreMenu"
        x: 66; y: Math.max(12,rail.y+rail.height-height); width: 208; padding: 6
        background: Plate { color: Theme.wallChromeSurface; radius: 12; elevation: 3 }
        WallMenuItem { text: "刷新"; enabled: root.selectedTab !== "favorites" && !root.loading; onTriggered: root.refreshTab() }
        Item { width: 1; height: 8 }
        WallMenuItem { text: "最近播放"; onTriggered: root.navigate("history") }
        WallMenuItem { text: "统计"; onTriggered: root.navigate("stats") }
        Item { width: 1; height: 8 }
        WallMenuItem { text: "打开播放页"; visible: !!music.current.hash; onTriggered: root.player() }
        WallMenuItem { text: "队列拼贴"; onTriggered: root.wall() }
        WallMenuItem { text: "红心推荐"; onTriggered: music.startRadio() }
        Item { width: 1; height: 8 }
        WallMenuItem { text: "返回报刊"; onTriggered: root.paper() }
    }
}
