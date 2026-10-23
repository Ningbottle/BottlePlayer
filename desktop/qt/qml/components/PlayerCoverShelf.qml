import QtQuick
import QtQuick.Controls
import BottleMusic

FocusScope {
    id: root
    enabled: visible
    property real viewportHeight: height
    property int focusIndex: Math.max(0, music.queueIndex)
    property double lastWheel: 0
    readonly property real coverSize: Math.max(110, Math.min(170, viewportHeight * 0.16))
    readonly property var items: {
        if (!visible) return []
        const start = Math.max(0, focusIndex - 7), end = Math.min(music.queue.length, focusIndex + 8)
        return music.queue.slice(start, end).map((track, i) => ({track:track, index:start + i, offset:start + i - focusIndex}))
    }
    readonly property var focused: music.queue[focusIndex]
    signal close()
    height: Math.min(parent.height, coverSize * 1.6 + Theme.s10 * 2.5)
    function move(step) { focusIndex = Math.max(0, Math.min(music.queue.length - 1, focusIndex + step)) }
    function activate(index) { if (index === focusIndex) music.playQueue(index); else focusIndex = index }
    onVisibleChanged: if (visible) focusIndex = Math.max(0, music.queueIndex)
    Connections {
        target: music
        function onQueueChanged() { root.focusIndex = Math.max(0, music.queueIndex) }
        function onQueueIndexChanged() { root.focusIndex = Math.max(0, music.queueIndex) }
    }
    Accessible.role: Accessible.Dialog; Accessible.name: "播放队列封面架"
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) { move(event.key === Qt.Key_Right ? 1 : -1); event.accepted = true }
        else if (event.key === Qt.Key_Home || event.key === Qt.Key_End) { focusIndex = event.key === Qt.Key_Home ? 0 : Math.max(0, music.queue.length - 1); event.accepted = true }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { if (focused) music.playQueue(focusIndex); event.accepted = true }
        else if (event.key === Qt.Key_Escape) { close(); event.accepted = true }
    }
    WheelHandler {
        enabled: root.visible
        onWheel: event => {
            const now = Date.now(), delta = Math.abs(event.angleDelta.x) > Math.abs(event.angleDelta.y) ? event.angleDelta.x : event.angleDelta.y
            if (delta && now - root.lastWheel >= 90) { root.lastWheel = now; root.move(delta < 0 ? 1 : -1) }
            event.accepted = true
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient { GradientStop { position: 0; color: Theme.clear(Theme.bg) } GradientStop { position: 0.28; color: Theme.bg } GradientStop { position: 1; color: Theme.bg } }
    }
    Text {
        x: Theme.s8; y: Theme.s8; text: "播放队列，" + (root.focusIndex + 1) + " / " + music.queue.length
        color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; font.italic: Theme.metaItalic
    }
    IconButton { objectName: "playerShelfClose"; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: Theme.s8; size: "sm"; glyph: "x"; Accessible.name: "关闭封面架"; onClicked: root.close() }
    Item {
        id: stage; x: Theme.s8; y: Theme.s8 + Theme.controlSm
        width: parent.width - Theme.s8 * 2; height: parent.height - y - 54; clip: true
        Repeater {
            model: root.items
            AbstractButton {
                required property var modelData
                objectName: "playerShelfItem_" + modelData.index
                readonly property int distance: Math.abs(modelData.offset)
                width: root.coverSize; height: width
                x: stage.width / 2 - width / 2 + width * (modelData.offset * 0.8 + Math.sign(modelData.offset) * 0.45)
                y: stage.height / 2 - height / 2; z: 100 - distance
                scale: Math.max(0.7, 1 - distance * 0.11); opacity: Math.max(0, 1 - distance * 0.12)
                transform: Rotation { origin.x: root.coverSize / 2; origin.y: root.coverSize / 2; axis.y: 1; axis.x: 0; axis.z: 0; angle: Math.max(-55, Math.min(55, modelData.offset * -34)) }
                Behavior on x { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
                Accessible.name: "播放 " + modelData.track.title
                onClicked: root.activate(modelData.index); onDoubleClicked: music.playQueue(modelData.index)
                contentItem: Cover { url: modelData.track.image || ""; title: ""; radius: Theme.rCard; glyphScale: 0.36 }
                background: Rectangle { color: Theme.surface; radius: Theme.rCard; border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: parent.modelData.offset === 0 ? Theme.accent : Theme.border }
                Rectangle { visible: parent.modelData.index === music.queueIndex; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: Theme.s2; width: Theme.s2; height: width; radius: width / 2; color: Theme.accent }
            }
        }
    }
    Column {
        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.s3; anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - Theme.s8 * 2; spacing: Theme.s1
        Text { width: parent.width; text: root.focused ? root.focused.title : "队列为空"; color: Theme.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; font.family: Theme.fontTitle; font.pixelSize: Theme.fsLg }
        Text { width: parent.width; text: root.focused ? root.focused.artist : ""; color: Theme.text2; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
    }
}
