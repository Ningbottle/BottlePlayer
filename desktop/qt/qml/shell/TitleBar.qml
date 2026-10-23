import QtQuick
import QtQuick.Controls
import BottleMusic

// Listening modes are selected in SettingsPage. This row manages the window.
Item {
    id: root
    objectName: "windowTitlebar"
    property Window win
    property string scene: "chrome"
    readonly property bool wallInk: scene !== "chrome"
    readonly property real controlsWidth: controls.width + Theme.s2
    readonly property bool floating: scene === "overlay"
    height: scene === "overlay" ? Theme.wallBarH : Theme.titlebarH
    Rectangle { anchors.fill: parent; visible: root.scene === "wall"; color: Theme.wallBg }
    function toggleMaximize() {
        win.visibility = win.visibility === Window.Maximized ? Window.Windowed : Window.Maximized
    }
    MouseArea {
        anchors.fill: parent; enabled: root.scene !== "overlay"
        onPressed: root.win.startSystemMove()
        onDoubleClicked: root.toggleMaximize()
    }
    Text {
        anchors.left: parent.left; anchors.leftMargin: Theme.s4; anchors.verticalCenter: parent.verticalCenter
        visible: root.scene !== "overlay" && (root.scene === "wall" || !root.win || root.win.route === "settings")
        text: "BottleMusic"; color: root.wallInk ? Theme.wallChromeMuted : Theme.text3
        font.family: Theme.fontDisplay; font.pixelSize: Theme.wordmarkSize
        font.weight: Theme.wordmarkWeight; font.letterSpacing: Theme.wordmarkTracking
    }
    component WindowButton: IconButton {
        implicitWidth: root.scene === "overlay" ? 28 : Theme.controlSm; implicitHeight: implicitWidth
        glyphSize: root.floating ? 14 : Theme.iconSize
        radius: root.wallInk ? width/2 : Theme.rSm
        tint: root.wallInk ? Theme.wallChromeMuted : Theme.text2; hoverTint: root.wallInk ? Theme.wallChromeInk : Theme.text
        hoverFill: root.wallInk ? Theme.wallChromeHover : Theme.surfaceHover
        downFill: root.wallInk ? Theme.wallChromePressed : Theme.surfaceActive
        focusInk: root.wallInk ? Theme.wallFocus : Theme.focus
    }
    Rectangle {
        visible: root.floating; width: controls.width+12; height: 32; radius: 16
        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        color: reveal.hovered ? Theme.wallSurface : Theme.wallBg

    }
    Row {
        id: controls
        anchors.right: parent.right; anchors.rightMargin: root.floating ? 14 : Theme.s2; anchors.verticalCenter: parent.verticalCenter
        spacing: root.floating ? 6 : Theme.s1
        opacity: root.floating && !reveal.hovered && !(root.win.activeFocusItem && root.win.activeFocusItem.parent === controls) ? 0.66 : 1
        Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic } }
        HoverHandler { id: reveal; margin: 16 }
        WindowButton { objectName: "minimizeButton"; size: "sm"; glyph: "minus"; Accessible.name: "最小化"; onClicked: root.win.showMinimized() }
        WindowButton { objectName: "maximizeButton"; size: "sm"; glyph: root.win.visibility === Window.Maximized ? "move-diagonal-2" : "maximize-2"; Accessible.name: root.win.visibility === Window.Maximized ? "还原窗口" : "最大化"; onClicked: root.toggleMaximize() }
        WindowButton {
            objectName: "closeButton"; size: "sm"; glyph: "x"
            hoverFill: root.wallInk ? Theme.wallDanger : Theme.danger; downFill: hoverFill; hoverTint: Theme.inkOn(hoverFill)
            Accessible.name: "关闭"; onClicked: root.win.close()
        }
    }
}
