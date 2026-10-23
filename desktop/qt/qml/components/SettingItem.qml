import QtQuick
import BottleMusic

// Setting item layout: label (fs-md/500 --font-title) + hint (fs-sm
// --text-3) in 1.5 line boxes on the left, the control on the right, 16px
// vertical padding, 24px gap, and a 1px --border hairline underneath (one
// device pixel in layout too, so a two-line row is 77.3px at 125%).
// `stacked` puts the control under the text (.is-stacked, 16px gap).
// Positioned by hand: QtQuick.Layouts round every size up to whole logical
// pixels (1.25 device pixels at 125%), which drifts the rows apart.
Item {
    id: root
    property string label: ""
    property string hint: ""
    property string badge: ""
    property bool badgeAccent: false
    property bool stacked: false
    default property alias control: slot.data
    width: parent ? parent.width : 0
    readonly property real textHeight: labelRow.height + (hintText.visible ? Theme.s1 + hintText.height : 0)
    readonly property real bodyHeight: stacked ? textHeight + Theme.s4 + slot.height : Math.max(textHeight, slot.height)
    implicitHeight: bodyHeight + Theme.s4 * 2 + Theme.hairline

    Item {
        id: copy
        y: Theme.s4 + (root.stacked ? 0 : (root.bodyHeight - root.textHeight) / 2)
        width: root.stacked ? root.width : Math.max(0, root.width - slot.width - Theme.s6)
        height: root.textHeight
        // .settings-field-label: a flex row, items centered, 8px apart.
        Item {
            id: labelRow
            width: parent.width
            height: Math.max(labelText.height, badgeBox.visible ? badgeBox.height : 0)
            CssText {
                id: labelText
                y: (parent.height - height) / 2
                text: root.label; color: Theme.text
                font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd; font.weight: Theme.fwMedium
            }
            // .settings-badge: pill, fs-xs/1.6, 8px inline padding.
            Rectangle {
                id: badgeBox
                visible: root.badge !== ""
                x: labelText.width + Theme.s2
                y: (parent.height - height) / 2
                width: badgeText.implicitWidth + Theme.s2 * 2; height: badgeText.height
                radius: Theme.rFull; color: root.badgeAccent ? Theme.accentSoft : Theme.surfaceHover
                CssText {
                    id: badgeText
                    x: Theme.s2
                    lineBox: Theme.fsXs * 1.6
                    text: root.badge; color: root.badgeAccent ? Theme.accentText : Theme.text2
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                }
            }
        }
        CssText {
            id: hintText
            visible: root.hint !== ""
            y: labelRow.height + Theme.s1
            width: parent.width
            text: root.hint; color: Theme.text3; wrapMode: Text.Wrap
            font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
        }
    }
    Item {
        id: slot
        x: root.stacked ? 0 : root.width - width
        y: Theme.s4 + (root.stacked ? root.textHeight + Theme.s4 : (root.bodyHeight - height) / 2)
        width: root.stacked ? root.width : childrenRect.width
        height: childrenRect.height
    }
    Hairline { anchors.bottom: parent.bottom; width: parent.width }
}
