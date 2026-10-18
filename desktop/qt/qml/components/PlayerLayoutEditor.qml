import QtQuick
import QtQuick.Controls
import BottleMusic

Item {
    id: root
    property var layout
    property var cardNames
    property real gap: 12
    property real cellWidth
    property real cellHeight
    property string dragId: ""
    property bool resizing: false
    property point pressPoint
    property var origin
    property var preview
    property var targetRect
    property real deltaX: 0
    property real deltaY: 0
    signal rejected()
    signal done()
    function cancelDrag() { dragId = ""; preview = null; targetRect = null; deltaX = 0; deltaY = 0 }
    function begin(id, item, point, resize) {
        cancelDrag(); dragId = id; resizing = resize; origin = layout.cards[id]
        pressPoint = item.mapToItem(root, point.x, point.y)
    }
    function update(item, point) {
        if (!dragId) return
        let p = item.mapToItem(root, point.x, point.y)
        const rawX = p.x - pressPoint.x, rawY = p.y - pressPoint.y
        if (Math.hypot(rawX, rawY) < Theme.dragDeadzone && !preview) { deltaX = 0; deltaY = 0; targetRect = null; return }
        deltaX = rawX; deltaY = rawY
        const dx = Math.round(deltaX / (cellWidth + gap)), dy = Math.round(deltaY / (cellHeight + gap))
        if (resizing) {
            preview = layout.previewResize(dragId, origin.w + dx, origin.h + dy); targetRect = preview.cards[dragId]
        } else {
            targetRect = { x: Math.max(0, Math.min(12 - origin.w, origin.x + dx)), y: Math.max(0, Math.min(6 - origin.h, origin.y + dy)), w: origin.w, h: origin.h }
            preview = layout.previewMove(dragId, targetRect.x, targetRect.y)
        }
    }
    function finish() {
        if (preview && preview.ok) layout.commit(preview.cards)
        else if (preview) rejected()
        cancelDrag()
    }
    function key(id, event) {
        if (event.key === Qt.Key_Escape) { event.accepted = true; done(); return }
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) { event.accepted = true; layout.hide(id); return }
        const dx = event.key === Qt.Key_Right ? 1 : event.key === Qt.Key_Left ? -1 : 0
        const dy = event.key === Qt.Key_Down ? 1 : event.key === Qt.Key_Up ? -1 : 0
        if (!dx && !dy) return
        event.accepted = true
        const rect = layout.cards[id]
        if (!rect) return
        const ok = event.modifiers & Qt.ShiftModifier ? layout.resize(id, rect.w + dx, rect.h + dy) : layout.move(id, rect.x + dx, rect.y + dy)
        if (!ok) rejected()
    }
    onVisibleChanged: if (!visible) cancelDrag()
    Repeater {
        model: root.visible ? 72 : 0
        Rectangle {
            required property int index
            x: index % 12 * (root.cellWidth + root.gap) + root.cellWidth / 2 - width / 2
            y: Math.floor(index / 12) * (root.cellHeight + root.gap) + root.cellHeight / 2 - height / 2
            width: 3; height: 3; radius: 1.5; color: Qt.rgba(Theme.ruleInk.r, Theme.ruleInk.g, Theme.ruleInk.b, 0.4)
        }
    }
    Rectangle {
        objectName: "playerLayoutDropTarget"; visible: !!root.targetRect
        readonly property color ink: root.preview && root.preview.ok ? Theme.accent : Theme.danger
        x: root.targetRect ? root.targetRect.x * (root.cellWidth + root.gap) : 0
        y: root.targetRect ? root.targetRect.y * (root.cellHeight + root.gap) : 0
        width: root.targetRect ? root.targetRect.w * root.cellWidth + (root.targetRect.w - 1) * root.gap : 0
        height: root.targetRect ? root.targetRect.h * root.cellHeight + (root.targetRect.h - 1) * root.gap : 0
        color: Qt.rgba(ink.r, ink.g, ink.b, 0.1); border.color: ink; border.width: Theme.hairline
    }
    Repeater {
        model: root.visible && root.layout ? Object.keys(root.layout.cards) : []
        Item {
            id: frame
            required property string modelData
            objectName: "playerLayoutFrame_" + modelData
            readonly property var rect: root.layout.cards[modelData] || {x:0, y:0, w:0, h:0}
            x: rect.x * (root.cellWidth + root.gap); y: rect.y * (root.cellHeight + root.gap)
            width: Math.max(0, rect.w * root.cellWidth + (rect.w - 1) * root.gap)
            height: Math.max(0, rect.h * root.cellHeight + (rect.h - 1) * root.gap)
            z: root.dragId === modelData ? 2 : 1
            transform: Translate { x: root.dragId === frame.modelData && !root.resizing ? root.deltaX : 0; y: root.dragId === frame.modelData && !root.resizing ? root.deltaY : 0 }
            Accessible.role: Accessible.Grouping; Accessible.name: root.cardNames[modelData] + "卡片"
            Keys.onPressed: event => root.key(frame.modelData, event)
            Rectangle { anchors.fill: parent; radius: Theme.rCard; color: "transparent"; border.width: frame.activeFocus ? 2 : Theme.hairline; border.color: frame.activeFocus ? Theme.focus : Theme.ruleInk }
            MouseArea {
                anchors.fill: parent; cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onPressed: mouse => { frame.forceActiveFocus(); root.begin(frame.modelData, this, mouse, false) }
                onPositionChanged: mouse => { if (pressed) root.update(this, mouse) }
                onReleased: root.finish(); onCanceled: root.cancelDrag()
            }
            Rectangle {
                x: Theme.s2; y: Theme.s2; height: 26; width: Math.min(label.implicitWidth + Theme.s2 * 2 + 16, Math.max(0, parent.width - 48))
                color: Theme.bg; radius: Theme.rSm
                Icon { x: Theme.s2; anchors.verticalCenter: parent.verticalCenter; name: "grip-horizontal"; size: 12; color: Theme.text3 }
                Text { id: label; x: Theme.s2 + 16; width: Math.max(0, parent.width - x - Theme.s2); anchors.verticalCenter: parent.verticalCenter; text: root.cardNames[frame.modelData]; color: Theme.text; elide: Text.ElideRight; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.weight: Theme.fwMedium }
            }
            IconButton { objectName: "playerHide_" + frame.modelData; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: Theme.s1; size: "sm"; glyph: "x"; Accessible.name: "隐藏" + root.cardNames[frame.modelData]; onClicked: root.layout.hide(frame.modelData) }
            Item {
                objectName: "playerResize_" + frame.modelData
                anchors.right: parent.right; anchors.bottom: parent.bottom; width: Theme.controlSm; height: width
                Icon { anchors.centerIn: parent; name: "move-diagonal-2"; color: Theme.ruleInk }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.SizeFDiagCursor
                    onPressed: mouse => { frame.forceActiveFocus(); root.begin(frame.modelData, this, mouse, true) }
                    onPositionChanged: mouse => { if (pressed) root.update(this, mouse) }
                    onReleased: root.finish(); onCanceled: root.cancelDrag()
                }
            }
        }
    }
}
