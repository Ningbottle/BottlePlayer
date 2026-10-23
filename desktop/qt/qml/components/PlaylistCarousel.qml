import QtQuick
import QtQuick.Controls
import BottleMusic

// Rotating playlist ring: a PathView fakes depth with scale, yaw, z order
// and fade along the path. Drag or the side arrows turn it; tapping the
// front cover opens the playlist, tapping a side cover brings it forward.
Item {
    id: root
    property var collections: []
    property string title: "推荐歌单"
    signal openPlaylist(var playlist)
    readonly property real cardSize: Theme.snap(Math.max(140, Math.min(width * 0.3, 340)))
    readonly property var front: collections[ring.currentIndex] || ({})
    implicitHeight: caption.y + caption.height + Theme.s4

    CssText {
        id: heading
        text: root.title; color: Theme.text
        font.family: Theme.fontSerif; font.pixelSize: Theme.fs2xl; font.weight: Theme.fwMedium
    }
    Row {
        anchors.right: parent.right; anchors.verticalCenter: heading.verticalCenter
        spacing: Theme.s1
        IconButton {
            objectName: "carouselPrev"; glyph: "chevron-left"; Accessible.name: "上一个歌单"
            Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Space
            onClicked: ring.decrementCurrentIndex()
        }
        IconButton {
            objectName: "carouselNext"; glyph: "chevron-right"; Accessible.name: "下一个歌单"
            Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Space
            onClicked: ring.incrementCurrentIndex()
        }
    }

    PathView {
        id: ring
        objectName: "playlistCarousel"
        anchors { left: parent.left; right: parent.right; top: heading.bottom; topMargin: Theme.s4 }
        height: root.cardSize + Theme.s4 * 2
        model: root.collections.slice(0, 15)
        pathItemCount: Math.min(count, 5)
        preferredHighlightBegin: 0.5; preferredHighlightEnd: 0.5
        highlightRangeMode: PathView.StrictlyEnforceRange
        snapMode: PathView.SnapToItem
        highlightMoveDuration: music.reducedMotion ? 0 : 460
        maximumFlickVelocity: 1600; flickDeceleration: 900
        dragMargin: height
        path: Path {
            startX: 0; startY: ring.height / 2
            PathAttribute { name: "itemScale"; value: 0.56 }
            PathAttribute { name: "itemYaw"; value: 52 }
            PathAttribute { name: "itemZ"; value: 0 }
            PathAttribute { name: "itemFade"; value: 0.25 }
            PathQuad { x: ring.width / 2; y: ring.height / 2 + Theme.s4; controlX: ring.width * 0.25; controlY: ring.height / 2 + Theme.s4 }
            PathAttribute { name: "itemScale"; value: 1 }
            PathAttribute { name: "itemYaw"; value: 0 }
            PathAttribute { name: "itemZ"; value: 10 }
            PathAttribute { name: "itemFade"; value: 1 }
            PathQuad { x: ring.width; y: ring.height / 2; controlX: ring.width * 0.75; controlY: ring.height / 2 + Theme.s4 }
            PathAttribute { name: "itemScale"; value: 0.56 }
            PathAttribute { name: "itemYaw"; value: -52 }
            PathAttribute { name: "itemZ"; value: 0 }
            PathAttribute { name: "itemFade"; value: 0.25 }
        }
        delegate: AbstractButton {
            id: card
            required property var modelData
            required property int index
            objectName: "carouselCard" + index
            width: root.cardSize; height: width
            padding: 0
            focusPolicy: PathView.isCurrentItem ? Qt.TabFocus : Qt.NoFocus
            text: modelData.name || "歌单"
            Accessible.name: (PathView.isCurrentItem ? "打开歌单：" : "选中歌单：") + text
            function activate() {
                if (PathView.isCurrentItem) root.openPlaylist(modelData)
                else ring.currentIndex = index
            }
            onClicked: activate()
            // Space belongs to the focused card, ahead of the global play shortcut.
            Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Space
            Keys.onReturnPressed: activate()
            Keys.onEnterPressed: activate()
            scale: PathView.itemScale; z: PathView.itemZ; opacity: PathView.itemFade
            transform: Rotation {
                origin.x: card.width / 2; origin.y: card.height / 2
                axis { x: 0; y: 1; z: 0 }
                angle: card.PathView.itemYaw
            }
            background: Rectangle {
                anchors.fill: parent; anchors.margins: -1; radius: Theme.rSm + 1
                color: "transparent"
                border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline
                border.color: card.visualFocus ? Theme.focus : Theme.borderStrong
            }
            contentItem: Cover {
                radius: Theme.rSm; pixels: 400
                url: card.modelData.image || ""; title: card.modelData.name || ""
            }
        }
    }

    Column {
        id: caption
        y: Math.round(ring.y + ring.height / 2 + Theme.s4 + root.cardSize / 2 + Theme.s5); anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width, root.cardSize * 1.6); spacing: Theme.s1
        // Keep the section height steady when rotating between playlists
        // with and without an author line.
        height: Math.max(implicitHeight, Theme.fsMd * Theme.lhNormal + Theme.s1 + Theme.fsXs * Theme.lhNormal)
        CssText {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
            text: root.front.name || ""; color: Theme.text
            font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; font.weight: Theme.fwMedium
        }
        CssText {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
            text: root.front.author || root.front.artist || ""; visible: text.length > 0; color: Theme.text3
            font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
        }
    }
}
