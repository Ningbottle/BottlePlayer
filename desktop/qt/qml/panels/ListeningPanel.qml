import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

// Listening panel: a transparent click-away scrim plus a 22rem (308px;
// 1rem is 14px) stage card inset .75rem from the edges (1rem radius,
// --shadow-pop, 1.5rem padding and gap). LISTENING ROOM header, current
// track between hairlines, bordered transport, progress/volume fields, the
// next four songs and the page links pinned to the bottom.
Item {
    id: root
    objectName: "listeningPanel"
    property bool wall: false
    signal close()
    signal paper()
    signal settings()
    signal account()
    signal navigate(string page)
    signal search(string query)
    signal player()
    signal queue()
    signal toggleWall()
    readonly property var upcoming: music.queue.slice(music.queueIndex + 1, music.queueIndex + 5)

    // --stage-home-text ladder (Theme.text* already switches in curtain).
    readonly property color ink: Theme.text
    readonly property color ink2: Theme.text2
    readonly property color ink3: Theme.text3

    // .panel-nav button: .75rem/.3rem padding, .65rem icon gap, .4rem radius.
    component PanelLink: AbstractButton {
        id: link
        property string glyph: ""
        property string trailingGlyph: "chevron-right"
        readonly property color ink: link.hovered ? root.ink : root.ink2
        implicitHeight: Theme.rem(0.75) * 2 + Theme.iconSize
        hoverEnabled: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        background: Rectangle {
            radius: Theme.rem(0.4)
            color: link.hovered ? Theme.surfaceHover : Theme.clear(Theme.surfaceHover)
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
        }
        contentItem: Item {
            Icon {
                id: lead; x: Theme.rem(0.3); anchors.verticalCenter: parent.verticalCenter
                visible: link.glyph !== ""; name: link.glyph; size: Theme.rem(1); color: link.ink
            }
            Text {
                id: label
                x: link.glyph !== "" ? lead.x + lead.width + Theme.rem(0.65) : Theme.rem(0.3)
                anchors.verticalCenter: parent.verticalCenter
                text: link.text; color: link.ink
                font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75))
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
            }
            Icon {
                anchors.right: parent.right; anchors.rightMargin: Theme.rem(0.3); anchors.verticalCenter: parent.verticalCenter
                name: link.trailingGlyph; size: Theme.rem(1); color: link.ink
            }
        }
    }
    // .panel-transport button: .7rem padding, 1px card border, .5rem radius.
    component TransportButton: AbstractButton {
        id: tb
        property string glyph: ""
        property string media: ""
        readonly property var mediaSpec: Theme.mediaStyle[media] || Theme.mediaStyle.play
        readonly property color ink: tb.hovered ? root.ink : root.ink2
        implicitHeight: 40
        implicitWidth: text ? row.implicitWidth + Theme.rem(0.7) * 2 + 2 : implicitHeight
        hoverEnabled: true
        opacity: enabled ? 1 : Theme.disabledOpacity
        Accessible.role: Accessible.Button
        background: Rectangle {
            radius: Theme.rem(0.5); border.width: Theme.hairline; border.color: Theme.stageCardBorder
            color: tb.hovered && tb.enabled ? Theme.surfaceHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
        }
        contentItem: Item {
            Row {
                id: row; anchors.centerIn: parent; spacing: Theme.rem(0.4)
                Icon { objectName: tb.objectName ? tb.objectName+"_glyph" : ""; name: tb.media ? tb.mediaSpec.glyph : tb.glyph; filled: tb.media !== ""; stroke: tb.media ? tb.mediaSpec.stroke : Theme.iconStroke; stroked: tb.media ? tb.mediaSpec.stroked : true; size: Theme.rem(1); color: tb.ink; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    visible: tb.text !== ""; text: tb.text; anchors.verticalCenter: parent.verticalCenter
                    color: tb.ink
                    font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75))
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                }
            }
        }
    }
    // Native <input type=range> with accent-color: 4px track, 16px thumb.
    component Range: Slider {
        id: range
        implicitHeight: 32; padding: 0
        background: Item {
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width; height: 4; radius: 2; color: Theme.borderStrong
                Rectangle { width: range.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
            }
        }
        handle: Rectangle {
            x: range.visualPosition * (range.availableWidth - width); y: (range.height - height) / 2
            width: 16; height: 16; radius: 8; color: range.pressed ? Theme.accentHover : Theme.accent
            opacity: range.enabled ? 1 : 0.5
        }
    }
    // .panel-field label: .7rem --stage-home-text-2.
    component FieldLabel: Text {
        color: root.ink2; font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.7))
    }

    MouseArea { anchors.fill: parent; onClicked: root.close() }
    QualityMenu { id: listeningQualityMenu; objectName: "listeningQualityMenu" }

    Plate {
        id: card
        objectName: "listeningCard"
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.margins: Theme.rem(0.75)
        width: Math.min(Theme.rem(22), root.width - Theme.rem(1.5))
        color: Theme.stageCard; radius: Theme.rem(1); elevation: 3
        borderColor: Theme.stageCardBorder
        MouseArea { anchors.fill: parent }
        HoverHandler { id: cardHover }
        Flickable {
            id: scroller
            objectName: "listeningScroller"
            anchors.fill: parent; anchors.margins: Theme.rem(1.5)
            contentWidth: width
            contentHeight: Math.max(height, body.implicitHeight); clip: true
            boundsBehavior: Flickable.StopAtBounds
            SmoothWheel { flickable: scroller }
            // overflow-y: auto on the padded card puts the bar at its inner edge.
            ScrollBar.vertical: ThinScrollBar {
                parent: card
                x: card.width - width - 3; y: card.radius
                height: card.height - card.radius * 2
                areaHovered: cardHover.hovered
            }
            ColumnLayout {
                id: body
                objectName: "listeningBody"
                width: scroller.width; height: scroller.contentHeight
                spacing: Theme.rem(1.5)

                RowLayout {
                    objectName: "listeningHeader"
                    Layout.fillWidth: true; spacing: Theme.rem(1)
                    Column {
                        Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                        Text {
                            text: "LISTENING ROOM"; color: root.ink3
                            font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.55))
                            font.letterSpacing: Theme.rem(0.55) * 0.12
                        }
                        Text {
                            topPadding: Theme.rem(0.3); text: "此刻的声音"; color: root.ink
                            font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(1.3)); font.weight: Theme.fwSemiBold
                            lineHeight: 1.4
                        }
                    }
                    IconButton {
                        objectName: "listeningClose"; Layout.alignment: Qt.AlignTop
                        glyph: "x"; glyphSize: Theme.rem(1); implicitWidth: 40; implicitHeight: 40; radius: Theme.rSm
                        glyphLift: 0
                        tint: root.ink2; hoverTint: root.ink
                        Accessible.name: "收起聆听面板"; onClicked: root.close()
                    }
                }

                TextField {
                    id: query; objectName: "listeningSearchInput"
                    Layout.fillWidth: true; implicitHeight: 40
                    leftPadding: 36; rightPadding: 12; topPadding: 0; bottomPadding: 0
                    verticalAlignment: TextInput.AlignVCenter; placeholderText: "搜索歌曲、歌手、歌单"
                    placeholderTextColor: root.ink3; color: root.ink
                    selectionColor: Theme.accentSoft; selectedTextColor: root.ink
                    font.family: Theme.fontUi; font.pixelSize: 12
                    Accessible.name: "搜索音乐"
                    onAccepted: if (text.trim()) root.search(text.trim())
                    background: Rectangle {
                        radius: 20; color: Theme.surface; border.width: Theme.hairline
                        border.color: query.activeFocus ? Theme.accent : Theme.stageCardBorder
                    }
                    Icon { x: 12; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 14; color: root.ink3 }
                }

                // .panel-current: hairlines above and below, 1.25rem block padding.
                Item {
                    Layout.fillWidth: true; implicitHeight: current.implicitHeight + Theme.rem(1.25) * 2
                    Hairline { width: parent.width; color: Theme.stageCardBorder }
                    Hairline { y: parent.height - Theme.hairline; width: parent.width; color: Theme.stageCardBorder }
                    Column {
                        id: current; y: Theme.rem(1.25); width: parent.width; spacing: Theme.rem(0.4)
                        Text {
                            width: parent.width; text: music.current.title || "音乐还未开始"; color: root.ink
                            font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(1.2)); font.weight: Theme.fwSemiBold
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                        Text {
                            width: parent.width; text: music.current.artist || "开启红心推荐，发现下一首"; color: root.ink2
                            font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75)); elide: Text.ElideRight
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true; spacing: Theme.rem(0.65)
                    TransportButton {
                        visible: !music.radio; media: "prev"; enabled: !!music.current.hash && !music.loading
                        Accessible.name: "上一首"; onClicked: music.previous()
                    }
                    TransportButton {
                        objectName: "listeningPlay"; Layout.fillWidth: true
                        media: music.playing ? "pause" : "play"; text: music.playing ? "暂停" : "继续播放"
                        enabled: !!music.current.hash; Accessible.name: text; onClicked: music.toggle()
                    }
                    TransportButton {
                        media: "next"; text: music.radio ? "换一首" : "下一首"
                        enabled: !!music.current.hash && !music.loading; Accessible.name: text; onClicked: music.next()
                    }
                    IconButton {
                        objectName: "listeningFavorite"; glyph: "heart"; glyphFilled: music.favorite
                        implicitWidth: 40; implicitHeight: 40
                        tint: music.favorite ? Theme.radioAccent : root.ink2
                        enabled: !!music.current.hash; Accessible.name: music.favorite ? "取消收藏" : "收藏歌曲"
                        onClicked: music.toggleFavorite()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true; spacing: Theme.rem(0.5)
                    RowLayout {
                        Layout.fillWidth: true
                        FieldLabel { text: "播放进度"; Layout.fillWidth: true }
                        FieldLabel {
                            text: Theme.duration(music.position / 1000) + " / " + Theme.duration(music.duration / 1000)
                            font.features: { "tnum": 1 }
                        }
                    }
                    Range {
                        Layout.fillWidth: true
                        from: 0; to: Math.max(1, music.duration); value: music.position
                        enabled: !!music.current.hash && music.duration > 0
                        Accessible.name: "播放进度"; onMoved: music.seek(value)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    FieldLabel { text: "音质"; Layout.fillWidth: true }
                    ActionButton {
                        objectName: "listeningQuality"; size: "sm"
                        text: music.qualityText(music.actualQuality)
                        enabled: !!music.current.hash && !String(music.current.hash).startsWith("file:")
                        onClicked: listeningQualityMenu.popup()
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Theme.rem(0.5)
                    RowLayout {
                        Layout.fillWidth: true; spacing: Theme.rem(0.45)
                        Icon { name: "volume-2"; size: Theme.rem(1); color: root.ink2 }
                        FieldLabel { text: "音量"; Layout.fillWidth: true }
                        FieldLabel { text: Math.round(music.volume * 100) + "%" }
                    }
                    Range {
                        Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.01
                        value: music.volume; Accessible.name: "音量"; onMoved: music.volume = value
                    }
                }

                Column {
                    Layout.fillWidth: true
                    Text {
                        bottomPadding: Theme.rem(0.75)
                        text: music.radio ? "下一段发现" : "接下来"; color: root.ink2
                        font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75)); font.weight: Theme.fwMedium
                    }
                    Repeater {
                        model: root.upcoming
                        Item {
                            required property var modelData; required property int index
                            width: parent.width; height: Theme.rem(0.75) * 2 + rowText.implicitHeight + Theme.hairline
                            Text {
                                id: number; anchors.verticalCenter: rowText.verticalCenter
                                text: String(index + 1).padStart(2, "0"); color: root.ink3
                                font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.65))
                            }
                            Column {
                                id: rowText; y: Theme.rem(0.75); x: number.width + Theme.rem(0.75); width: parent.width - x; spacing: Theme.rem(0.25)
                                Text {
                                    width: parent.width; text: modelData.title || ""; color: root.ink; elide: Text.ElideRight
                                    font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.75)); font.weight: Theme.fwMedium
                                }
                                Text {
                                    width: parent.width; text: modelData.artist || ""; color: root.ink3; elide: Text.ElideRight
                                    font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.65))
                                }
                            }
                            Hairline { y: parent.height - Theme.hairline; width: parent.width; color: Theme.stageCardBorder }
                        }
                    }
                    Text {
                        visible: root.upcoming.length === 0
                        text: music.radio ? "换一首时继续为你推荐" : "队列里还没有更多歌曲"; color: root.ink3
                        font.family: Theme.fontUi; font.pointSize: Theme.pt(Theme.rem(0.65))
                    }
                    PanelLink {
                        objectName: "listeningQueue"; visible: !music.radio; width: parent.width
                        glyph: "list-music"; text: "查看完整队列"; onClicked: root.queue()
                    }
                }

                Item { Layout.fillHeight: true }

                ColumnLayout {
                    Layout.fillWidth: true; spacing: Theme.rem(0.25)
                    Text {
                        Layout.fillWidth: true; text: "我的音乐"; color: root.ink3
                        font.family: Theme.fontUi; font.pixelSize: 10; bottomPadding: 4
                    }
                    PanelLink { objectName: "listeningLibrary"; Layout.fillWidth: true; glyph: "list-music"; text: "个人歌单"; onClicked: root.navigate("library") }
                    PanelLink { objectName: "listeningFavorites"; Layout.fillWidth: true; glyph: "heart"; text: "本地收藏"; onClicked: root.navigate("favorites") }
                    PanelLink { objectName: "listeningHistory"; Layout.fillWidth: true; glyph: "history"; text: "最近播放"; onClicked: root.navigate("history") }
                    PanelLink { objectName: "listeningStats"; Layout.fillWidth: true; glyph: "chart-no-axes-combined"; text: "听歌统计"; onClicked: root.navigate("stats") }
                    Hairline { Layout.fillWidth: true; Layout.topMargin: 8; Layout.bottomMargin: 8; color: Theme.stageCardBorder }
                    Text {
                        Layout.fillWidth: true; text: "界面与账户"; color: root.ink3
                        font.family: Theme.fontUi; font.pixelSize: 10; bottomPadding: 4
                    }
                    PanelLink {
                        objectName: "listeningWall"; Layout.fillWidth: true
                        glyph: "panels-top-left"; text: root.wall ? "回到幕布舞台" : "海报墙"
                        onClicked: root.toggleWall()
                    }
                    PanelLink {
                        objectName: "listeningAccount"; Layout.fillWidth: true
                        glyph: "user-round"; text: accountState.loggedIn ? "我的账户" : "登录账户"; onClicked: root.account()
                    }
                    PanelLink {
                        objectName: "listeningPlayer"; Layout.fillWidth: true; glyph: "audio-lines"; text: "打开播放页"; trailingGlyph: "arrow-up-right"
                        onClicked: root.player()
                    }
                    PanelLink {
                        objectName: "listeningSettings"; Layout.fillWidth: true
                        glyph: "settings"; text: "设置"; onClicked: root.settings()
                    }
                    PanelLink {
                        objectName: "listeningPaper"; Layout.fillWidth: true
                        text: "返回报刊"; onClicked: root.paper()
                    }
                }
            }
        }
    }
}
