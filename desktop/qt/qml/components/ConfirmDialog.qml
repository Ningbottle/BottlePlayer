import QtQuick
import QtQuick.Controls
import BottleMusic

Dialog {
    id: root
    property string message: ""
    modal: true
    anchors.centerIn: parent
    width: Math.min(400, parent ? parent.width - 32 : 400)
    standardButtons: Dialog.Ok | Dialog.Cancel
    contentItem: Text {
        text: root.message; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: Theme.text; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
        renderType: Text.NativeRendering
    }
    background: Rectangle { color: Theme.bgElevated; radius: Theme.rMd; border.width: Theme.hairline; border.color: Theme.borderStrong }
}
