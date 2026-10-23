import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

// Source reference: BravaisTile / BravaisFocusCardBody at Folia 7701347.
// Camera-scaled type uses distance-field text; the face is never a text layer.
Item {
    id: root
    property var item: ({})
    property bool expanded: false
    property bool current: false
    property bool queued: false
    property bool dimmed: false
    property bool captionVisible: true
    property real pixelScale: 0.76
    signal activate()
    signal play()
    signal enqueue()
    signal openPlayer()
    signal close()
    property var shown: item
    property real imageOpacity: 1
    property bool mounted: false
    Component.onCompleted: { shown = item; mounted = true }
    onItemChanged: shown = item
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: (item.title || "") + "，" + (item.artist || "")
    Keys.onReturnPressed: root.activate()
    Keys.onEnterPressed: root.activate()
    Item {
        id: face; anchors.fill: parent; clip: true
        Rectangle { anchors.fill: parent; color: Theme.wallCurrentBg }
        Cover {
            objectName: "bravaisArtwork"
            anchors.fill: parent; radius: 0
            pixels: root.expanded || Math.max(root.width,root.height)*root.pixelScale*Theme.dpr > 512 ? 1000 : 512
            url: root.shown.image || ""; title: root.shown.title || ""
            opacity: (root.dimmed ? 0.4 : 1)*root.imageOpacity
            scale: 1

        }
        Rectangle {
            objectName: "bravaisScrim"; anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: root.expanded ? 0.54 : Math.max(0,Math.min(0.34,textBlock.y/root.height-0.24)); color: root.expanded ? Theme.clear(Theme.black) : Theme.blackA(1) }
                GradientStop { position: root.expanded ? 0.70 : Math.max(0.02,Math.min(0.56,textBlock.y/root.height-0.02)); color: Theme.blackA(5) }
                GradientStop { position: root.expanded ? 0.86 : 1; color: root.expanded ? Theme.blackA(8) : Theme.blackA(11) }
                GradientStop { position: 1; color: Theme.blackA(11) }
            }
        }
        Rectangle {
            objectName: "bravaisScrimSide"; anchors.fill: parent; visible: root.expanded
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.blackA(11) }
                GradientStop { position: 0.52; color: Theme.blackA(7) }
                GradientStop { position: 1; color: Theme.blackA(2) }
            }
        }
        MouseArea { id: pointer; anchors.fill: parent; hoverEnabled: true; onClicked: root.activate() }
        Rectangle { anchors.fill: parent; color: Theme.whiteA(1); visible: pointer.containsMouse && !root.expanded }
        Rectangle {
            objectName: root.current ? "bravaisNowPlayingBadge" : "bravaisKindBadge"
            visible: root.current || root.shown.kind === "playlist" || root.shown.kind === "album"
            x: Theme.tileInsetPx(root.pixelScale)/root.pixelScale; y: x
            width: badgeRow.width+16/root.pixelScale; height: 22/root.pixelScale; radius: 11/root.pixelScale
            color: root.current ? Theme.wallAccent : Theme.wallBadgeBg
            Row {
                id: badgeRow; anchors.centerIn: parent; spacing: 4/root.pixelScale
                Icon { visible: root.current; anchors.verticalCenter: parent.verticalCenter; name: "audio-lines"; size: 12/root.pixelScale; rasterScale: root.pixelScale; color: Theme.inkOn(Theme.wallAccent) }
                Text {
                    objectName: root.current ? "bravaisNowPlayingBadge_label" : ""
                    anchors.verticalCenter: parent.verticalCenter; textFormat: Text.PlainText
                    text: root.current ? "播放中" : root.shown.kind === "playlist" ? (Number(root.shown.count)>=0 ? "歌单，"+root.shown.count+" 首" : "歌单") : "专辑"
                    font.family: Theme.fontUi; font.pixelSize: Theme.worldPx(12,root.pixelScale); font.weight: Theme.fwMedium
                    color: root.current ? Theme.inkOn(Theme.wallAccent) : Theme.white; renderType: Text.QtRendering
                }
            }
        }
        Column {
            id: textBlock; objectName: "bravaisTileText"
            visible: root.captionVisible || root.expanded
            x: (root.expanded?24:Theme.tileInsetPx(root.pixelScale))/root.pixelScale
            width: root.expanded ? (parent.width-48/root.pixelScale)*0.76 : parent.width-x*2
            y: root.expanded ? controls.y-height-20/root.pixelScale : parent.height-height-x
            spacing: (root.expanded?8:6)/root.pixelScale
            Text {
                width: parent.width; text: root.shown.title || "未命名"; textFormat: Text.PlainText
                font.family: Theme.fontUi
                readonly property int titleSize: root.expanded ? Theme.worldPx(Theme.focusTitlePx(root.Window.window ? root.Window.window.width : root.width,root.pixelScale),root.pixelScale)
                    : Theme.worldPx(Theme.tileTitlePx(root.Window.window ? root.Window.window.width : root.width,root.pixelScale),root.pixelScale)
                font.pixelSize: titleSize
                font.weight: root.expanded ? Theme.fwSemiBold : Theme.fwMedium; font.letterSpacing: -titleSize*(root.expanded?0.02:0.01)
                color: Theme.white; renderType: Text.QtRendering; wrapMode: Text.Wrap
                maximumLineCount: root.expanded ? 2 : root.height >= 3*BravaisLayout.pitch-BravaisLayout.gap ? 3 : 2
                elide: Text.ElideRight; lineHeight: root.expanded?1.05:1.12
            }
            Text {
                width: parent.width; textFormat: Text.PlainText
                text: root.expanded ? root.shown.kind === "playlist" ? Number(root.shown.count)>=0 ? root.shown.count+" 首歌曲" : ""
                    : root.shown.kind === "album" ? root.shown.artist || ""
                    : [root.shown.artist,root.shown.track ? root.shown.track.album : "",root.shown.track && root.shown.track.duration>0 ? Theme.duration(root.shown.track.duration) : ""].filter(Boolean).join(" / ")
                    : root.shown.kind === "playlist" ? "" : root.shown.artist || ""
                visible: !!text; font.family: Theme.fontUi
                font.pixelSize: root.expanded ? Theme.worldPx(Theme.focusInfoPx(root.pixelScale),root.pixelScale)
                    : Theme.worldPx(Theme.tileSubtitlePx(root.Window.window ? root.Window.window.width : root.width,root.pixelScale),root.pixelScale)
                color: Theme.wallInkSecondary; renderType: Text.QtRendering; elide: Text.ElideRight
            }
        }
        Item {
            id: controls; visible: root.expanded
            x: 24/root.pixelScale; width: parent.width-48/root.pixelScale
            y: parent.height-height-24/root.pixelScale; height: 44/root.pixelScale
            IconButton {
                id: playControl; objectName: root.expanded ? "bravaisFocusPlay" : ""
                anchors.verticalCenter: parent.verticalCenter
                media: root.current && music.playing ? "pause" : "play"; mediaBox: 44; mediaGlyph: 22; worldScale: root.pixelScale
                variant: "primary"; radius: width/2; primaryFill: Theme.wallAccent
                primaryHoverFill: Theme.wallHoverOn(primaryFill)
                primaryDownFill: Theme.wallDownOn(primaryFill)
                tint: Theme.inkOn(down ? primaryDownFill : lit ? primaryHoverFill : primaryFill); hoverTint: tint; focusInk: Theme.wallFocus
                enabled: !music.loading; Accessible.name: root.current && music.playing ? "暂停" : "播放"; onClicked: root.play()
            }
            ActionButton {
                objectName: root.expanded ? "bravaisFocusEnqueue" : ""
                x: playControl.width+12/root.pixelScale; anchors.verticalCenter: parent.verticalCenter
                text: root.item.kind === "playlist" ? "打开歌单" : root.queued ? "已在队列" : "加入队列"
                glyph: root.queued ? "check" : "plus"; quiet: true; typeSize: Theme.worldPx(14,root.pixelScale)
                implicitHeight: 36/root.pixelScale; leftPadding: 12/root.pixelScale; rightPadding: leftPadding
                restFill: Theme.whiteA(3); hoverFill: Theme.whiteA(4); downFill: Theme.whiteA(4)
                restInk: Theme.white; hoverInk: Theme.white; edgeColor: "transparent"; bgRadius: height/2
                glyphSize: 16/root.pixelScale; worldScale: root.pixelScale; focusInk: Theme.wallFocus; typeRendering: Text.QtRendering
                enabled: root.item.kind === "playlist" || (!music.radio && !root.queued); onClicked: root.enqueue()
            }
            IconButton {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 36/root.pixelScale; implicitHeight: implicitWidth; glyphSize: 18/root.pixelScale
                glyph: "maximize-2"; tint: Theme.wallInkSecondary; hoverTint: Theme.white; hoverFill: Theme.whiteA(3); downFill: Theme.whiteA(4)
                worldScale: root.pixelScale; focusInk: Theme.wallFocus; radius: width/2; visible: root.current
                Accessible.name: "打开播放页"; onClicked: root.openPlayer()
            }
        }
        IconButton {
            visible: root.expanded; x: parent.width-width-12/root.pixelScale; y: 12/root.pixelScale
            implicitWidth: 36/root.pixelScale; implicitHeight: implicitWidth; glyphSize: 18/root.pixelScale
            glyph: "x"; tint: Theme.wallInkSecondary; hoverTint: Theme.white; hoverFill: Theme.whiteA(3); downFill: Theme.whiteA(4)
            worldScale: root.pixelScale; focusInk: Theme.wallFocus; radius: width/2
            Accessible.name: "收起卡片"; onClicked: root.close()
        }
        Rectangle {
            anchors.fill: parent; color: "transparent"; visible: root.activeFocus
            border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline/root.pixelScale; border.color: Theme.wallFocus
        }
    }
}
