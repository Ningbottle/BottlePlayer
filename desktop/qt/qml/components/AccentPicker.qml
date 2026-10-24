import QtQuick
import QtQuick.Controls
import BottleMusic

Flow {
    id: root
    spacing: Theme.s2
    property string namePrefix: "accentChoice_"
    Repeater {
        model: Theme.accentChoices
        AbstractButton {
            id: choice
            required property string modelData
            required property int index
            objectName: root.namePrefix + (modelData || "default")
            readonly property bool selected: modelData ? Theme.customized && Theme.accentFamily === ["tomato","orange","amber","green","teal","blue","violet"][index-1] : !Theme.customized
            width: 36; height: 36; hoverEnabled: true; focusPolicy: Qt.TabFocus
            Accessible.role: Accessible.RadioButton
            Accessible.checked: selected
            Accessible.name: Theme.accentNames[index]
            ToolTip.visible: hovered; ToolTip.text: Accessible.name
            onClicked: appearanceState.accent = modelData
            background: Rectangle {
                radius: Theme.rSm; color: choice.hovered ? Theme.surfaceHover : "transparent"
                border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline
                border.color: choice.visualFocus ? Theme.focus : choice.selected ? Theme.borderStrong : "transparent"
            }
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent; width: 24; height: 24; radius: Theme.innerRadius(Theme.rSm,6)
                    color: choice.modelData || Theme.radix("tomato",9,false)
                    Icon { anchors.centerIn: parent; name: "check"; size: 16; color: Theme.inkOn(parent.color); visible: choice.selected }
                }
            }
        }
    }
}
