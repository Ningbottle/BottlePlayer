import QtQuick
import BottleMusic

// Chromium-style wheel scrolling for a Flickable: each mouse-wheel notch
// eases the content 100px instead of Flickable's velocity fling; touchpad
// pixel deltas apply directly. Dragging or flicking cancels the glide.
// Pattern from noctalia-shell's NScrollView (MIT).
WheelHandler {
    id: wheel
    required property Flickable flickable
    property real step: 100
    property real goal: 0
    target: null
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    enabled: flickable.contentHeight > flickable.height + 0.5

    function clampY(v) {
        return Math.max(flickable.originY, Math.min(v, flickable.originY + flickable.contentHeight - flickable.height))
    }
    onWheel: event => {
        if (event.pixelDelta.y !== 0) {
            glide.stop()
            flickable.contentY = clampY(flickable.contentY - event.pixelDelta.y)
            goal = flickable.contentY
        } else if (event.angleDelta.y !== 0) {
            if (!glide.running) goal = flickable.contentY
            goal = clampY(goal - event.angleDelta.y / 120 * step)
            glide.to = goal
            glide.restart()
        }
        event.accepted = true
    }

    property NumberAnimation glide: NumberAnimation {
        target: wheel.flickable; property: "contentY"
        duration: 220; easing.type: Easing.OutCubic
    }
    property Connections watcher: Connections {
        target: wheel.flickable
        function onDraggingChanged() { if (wheel.flickable.dragging) wheel.glide.stop() }
        function onFlickingChanged() { if (wheel.flickable.flicking) wheel.glide.stop() }
    }
}
