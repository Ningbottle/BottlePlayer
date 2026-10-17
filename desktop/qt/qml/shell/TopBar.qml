import QtQuick
import QtQuick.Controls
import BottleMusic

// Top bar geometry: 56px row, clamp(16px, 2.4vw, 40px) inline
// padding, back/forward ghost icon buttons, and a pill search field capped at
// 26rem (364px; 1rem is 14px) with a 16px stroke-2 search glyph inset 12px.
Item {
    id: root
    property real viewportWidth: width
    property bool canGoBack: false
    property bool canGoForward: false
    property alias query: field.text
    signal back()
    signal forward()
    signal search(string query)
    height: Theme.topbarH

    readonly property real inset: Math.min(Theme.s10, Math.max(Theme.s4, viewportWidth * 0.024))

    Row {
        id: arrows
        x: root.inset; anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s1
        IconButton { objectName: "navBack"; glyph: "chevron-left"; enabled: root.canGoBack; Accessible.name: "后退"; onClicked: root.back() }
        IconButton { objectName: "navForward"; glyph: "chevron-right"; enabled: root.canGoForward; Accessible.name: "前进"; onClicked: root.forward() }
    }
    TextField {
        id: field
        objectName: "searchInput"
        x: arrows.x + arrows.width + Theme.s2
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, Math.min(Theme.rem(26), root.width - x - root.inset))
        implicitHeight: Theme.controlMd
        leftPadding: Theme.s8; rightPadding: Theme.s3; topPadding: 0; bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        placeholderText: "搜索歌曲、艺人、专辑、歌单"
        placeholderTextColor: Theme.text3
        color: Theme.text; selectionColor: Theme.accentSoft; selectedTextColor: Theme.text
        font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
        Accessible.name: "搜索歌曲、艺人、专辑、歌单"
        background: Rectangle {
            radius: Theme.rFull; color: Theme.surface
            border.width: Theme.hairline; border.color: field.activeFocus ? Theme.accent : Theme.clear(Theme.accent)
            Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
            PixelSnap.fit: true
        }
        Icon {
            x: Theme.s3; anchors.verticalCenter: parent.verticalCenter
            name: "search"; stroke: 2; color: Theme.text3
        }
        onAccepted: root.search(text)
    }
}
