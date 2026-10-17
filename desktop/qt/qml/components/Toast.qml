import QtQuick
import BottleMusic

// .player-bar-toast: fs-xs/500 --font-ui on --bg-elevated, 1px --border,
// --r-md, --shadow-2, at most min(50vw, 32rem) wide, fading in and out.
// It keeps its last text through the fade-out so a cleared message does not
// collapse the plate mid-fade.
Plate {
    id: root
    property string text: ""
    property bool wall: false
    property real maxWidth: Theme.rem(32)
    property string held: text
    onTextChanged: if (text.length) held = text
    width: Math.min(maxWidth, label.implicitWidth + Theme.s3 * 2)
    height: label.implicitHeight + Theme.s2 * 2
    color: wall ? Theme.wallChromeSurface : Theme.bgElevated; radius: Theme.rMd; elevation: 2
    borderColor: wall ? "transparent" : Theme.border
    opacity: text.length ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.durBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
    Text {
        id: label
        objectName: root.objectName ? root.objectName + "_label" : ""
        anchors.centerIn: parent
        width: Math.min(implicitWidth, root.maxWidth - Theme.s3 * 2)
        text: root.held; textFormat: Text.PlainText; color: root.wall ? Theme.wallChromeInk : Theme.text
        font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.weight: Theme.fwMedium
        lineHeightMode: Text.FixedHeight; lineHeight: Math.round(Theme.fsXs * Theme.lhNormal)
        wrapMode: Text.Wrap
    }
}
