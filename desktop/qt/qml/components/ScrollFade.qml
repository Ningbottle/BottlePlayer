import QtQuick
import BottleMusic

// 28px scroll-edge fade: drop this as a sibling OVER a Flickable/ListView
// (same bounds) and point `flickable` at it. Top fades in once the content
// can scroll above, bottom fades while content can scroll below. It is an
// overlay of flat-tinted gradients, so text keeps native glyph rendering —
// a masked layer on the Flickable would rasterize it into a texture.
Item {
    id: root
    property Flickable flickable
    property color tint: Theme.bg
    property real fadeSize: Theme.scrollFade
    z: 5
    readonly property bool overflows: flickable && flickable.contentHeight + flickable.topMargin + flickable.bottomMargin > flickable.height + 1
    Rectangle {
        objectName: "scrollFadeTop"
        anchors.top: parent.top; width: parent.width; height: Math.min(root.fadeSize, root.height / 2)
        visible: root.overflows
        // ListView's origin can move after inserts/removals. Its edge flags
        // also account for content margins and overshoot.
        opacity: root.flickable && !root.flickable.atYBeginning ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
        gradient: Gradient {
            GradientStop { position: 0; color: root.tint }
            GradientStop { position: 1; color: Theme.clear(root.tint) }
        }
    }
    Rectangle {
        objectName: "scrollFadeBottom"
        anchors.bottom: parent.bottom; width: parent.width; height: Math.min(root.fadeSize, root.height / 2)
        visible: root.overflows
        opacity: root.flickable && !root.flickable.atYEnd ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.clear(root.tint) }
            GradientStop { position: 1; color: root.tint }
        }
    }
}
