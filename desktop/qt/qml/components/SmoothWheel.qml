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
    // Page-level settle animations must stop before either kind of delta
    // updates contentY, including the touchpad path that never starts glide.
    signal scrollInput()
    target: null
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    enabled: flickable.contentHeight > flickable.height + 0.5

    function clampY(v) {
        return Math.max(flickable.originY, Math.min(v, flickable.originY + flickable.contentHeight - flickable.height))
    }
    // Rest positions land on whole device pixels (Blink paints scrolled text
    // the same way) — a fractional contentY leaves every glyph half a device
    // pixel off at 125% scaling, which is what made columns read soft.
    onWheel: event => {
        if (event.pixelDelta.y !== 0 || event.angleDelta.y !== 0) scrollInput()
        if (event.pixelDelta.y !== 0) {
            glide.stop()
            flickable.contentY = Theme.snap(clampY(flickable.contentY - event.pixelDelta.y))
            goal = flickable.contentY
        } else if (event.angleDelta.y !== 0) {
            if (!glide.running) goal = flickable.contentY
            goal = Theme.snap(clampY(goal - event.angleDelta.y / 120 * step))
            glide.to = goal
            glide.restart()
        }
        event.accepted = true
    }

    property NumberAnimation glide: NumberAnimation {
        target: wheel.flickable; property: "contentY"
        duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic
    }
    property Connections watcher: Connections {
        target: wheel.flickable
        function onDraggingChanged() { if (wheel.flickable.dragging) wheel.glide.stop() }
        function onFlickingChanged() { if (wheel.flickable.flicking) wheel.glide.stop() }
        function onMovementEnded() {
            const f = wheel.flickable
            if (f.contentY >= f.originY - 0.5 && f.contentY <= f.originY + f.contentHeight - f.height + 0.5)
                f.contentY = Theme.snap(f.contentY)
        }
    }
}
