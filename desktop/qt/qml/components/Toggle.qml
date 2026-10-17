import QtQuick
import QtQuick.Controls
import BottleMusic

// Toggle geometry: a button with a 1px transparent border around a 36×20
// border-box track (1px --border-strong, 4px padding, --surface-hover) and a
// 12px --bg-elevated thumb that slides 16px; checked fills track and border
// with --accent. Both ease over --dur-fast. Chromium floors the 1px borders
// to one device pixel, so at 125% the button is 37.6×21.6 and the thumb sits
// 5.6px in.
AbstractButton {
    id: root
    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    implicitWidth: 36 + Theme.hairline * 2
    implicitHeight: 20 + Theme.hairline * 2
    padding: 0
    opacity: enabled ? 1 : Theme.disabledOpacity
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked
    PixelSnap.enabled: true
    background: Item {
        Rectangle {
            id: track
            x: Theme.hairline; y: Theme.hairline; width: 36; height: 20
            radius: Theme.rFull
            color: root.checked ? Theme.accent : Theme.surfaceHover
            border.width: Theme.hairline
            border.color: root.checked ? Theme.accent : Theme.borderStrong
            Behavior on color { ColorAnimation { duration: Theme.durFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
            Behavior on border.color { ColorAnimation { duration: Theme.durFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
        }
        // outline: 2px --focus, offset 2px around the --r-sm button box.
        Rectangle {
            anchors.fill: parent; anchors.margins: -4; radius: Theme.rSm + 4
            color: "transparent"; border.width: 2; border.color: Theme.focus
            visible: root.visualFocus
        }
    }
    contentItem: Item {
        Rectangle {
            x: Theme.hairline * 2 + Theme.s1 + (root.checked ? 16 : 0)
            y: Theme.hairline + (20 - height) / 2
            width: 12; height: 12; radius: Theme.rFull; color: Theme.bgElevated
            Behavior on x { NumberAnimation { duration: Theme.durFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
        }
    }
}
