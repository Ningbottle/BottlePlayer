import QtQuick
import QtQuick.Controls
import BottleMusic

// Segmented control: 4px padding/gap inside a 1px --border, --r-sm, --surface
// track; options are 28px tall, 12px inline padding inside a 1px transparent
// border, --r-xs, fs-sm --text-2 with an fs-xs secondary label; the checked
// option gets --surface-active. Chromium floors each 1px border to one device
// pixel in layout as well as paint (0.8px at 125%: 36 + 1.6 = 37.6px tall).
// The checked fill is one plate that slides to the picked option (rest state
// is identical to the CSS; the slide only runs after a click).
Rectangle {
    id: root
    // [{ value, label, secondary, objectName }]
    property var options: []
    property var value
    property bool sliding: false
    signal selected(var choice)
    readonly property int currentIndex: {
        for (var i = 0; i < options.length; i++) if (options[i].value === value) return i
        return -1
    }
    readonly property Item currentItem: repeater.count > 0 && currentIndex >= 0 ? repeater.itemAt(currentIndex) : null
    readonly property real inset: Theme.s1 + Theme.hairline
    implicitWidth: Theme.snapUp(row.implicitWidth + inset * 2)
    implicitHeight: Theme.controlSm + inset * 2
    radius: Theme.rMd
    color: Theme.surface
    border.color: Theme.border
    border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline
    // Whole device pixels, so the hairline frame is one crisp row/column.
    PixelSnap.enabled: true

    Rectangle {
        id: plate
        // Both edges rounded to device pixels, like Blink's paint rect.
        readonly property real startX: row.x + (root.currentItem ? root.currentItem.x : 0)
        readonly property real endX: startX + (root.currentItem ? root.currentItem.width : 0)
        visible: root.currentItem !== null
        x: Theme.snap(startX)
        y: row.y
        width: Theme.snap(endX) - x
        height: row.height
        radius: Theme.innerRadius(root.radius, root.inset)
        color: Theme.surfaceActive
        Behavior on x { enabled: root.sliding; NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durTab; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSnap } }
        Behavior on width { enabled: root.sliding; NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durTab; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSnap } }
    }

    Row {
        id: row
        x: root.inset; y: root.inset
        spacing: Theme.s1
        Repeater {
            id: repeater
            model: root.options
            AbstractButton {
                id: option
                required property var modelData
                readonly property bool current: root.value === modelData.value
                readonly property color ink: current ? Theme.text : Theme.text2
                objectName: modelData.objectName || ""
                height: Theme.controlSm
                leftPadding: Theme.s3 + Theme.hairline; rightPadding: Theme.s3 + Theme.hairline
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                checkable: false
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checked: current
                onClicked: { root.sliding = true; root.selected(modelData.value) }
                // outline: 2px --focus, offset 2px.
                background: Item {
                    Rectangle {
                        anchors.fill: parent; anchors.margins: -4
                        radius: Theme.rXs + 4
                        color: "transparent"
                        border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: Theme.focus
                        visible: option.visualFocus
                    }
                }
                contentItem: Row {
                    spacing: Theme.s1
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: option.modelData.label; color: option.ink
                        font.family: Theme.fontUi; font.pixelSize: Theme.fsSm

                    }
                    Text {
                        visible: !!option.modelData.secondary
                        anchors.verticalCenter: parent.verticalCenter
                        text: option.modelData.secondary || ""; color: option.ink
                        opacity: option.current ? 0.8 : 1
                        font.family: Theme.fontUi; font.pixelSize: Theme.fsXs

                    }
                }
            }
        }
    }
}
