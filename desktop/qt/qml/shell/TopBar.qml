import QtQuick
import QtQuick.Controls
import BottleMusic

// Search and navigation share the shell inset; optional settings open here.
Item {
    id: root
    property real viewportWidth: width
    property bool canGoBack: false
    property bool canGoForward: false
    property alias query: field.text
    function focusSearch() { field.forceActiveFocus(); field.selectAll() }
    signal back()
    signal forward()
    signal search(string query)
    signal openSettings(string section)
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
        width: Math.max(0, Math.min(Theme.rem(26), trailing.x - x - Theme.s4))
        implicitHeight: Theme.controlMd
        leftPadding: Theme.s8; rightPadding: Theme.s3; topPadding: 0; bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        placeholderText: "搜索歌曲、艺人、专辑、歌单"
        placeholderTextColor: Theme.text3
        color: Theme.text; selectionColor: Theme.accentSoft; selectedTextColor: Theme.text
        font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
        Accessible.name: "搜索歌曲、艺人、专辑、歌单"
        background: Rectangle {
            radius: Theme.rSm; color: Theme.surface
            border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: field.activeFocus ? Theme.accent : Theme.clear(Theme.accent)

            PixelSnap.fit: true
        }
        Icon {
            x: Theme.s3; anchors.verticalCenter: parent.verticalCenter
            name: "search"; color: Theme.text3
        }
        onAccepted: root.search(text)
    }

    // Membership badge and the quick-settings popover (the old top bar's
    // right edge); the full settings page stays one click further.
    Row {
        id: trailing
        anchors.right: parent.right; anchors.rightMargin: root.inset; anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s2
        AbstractButton {
            id: vipBadge; objectName: "vipBadge"
            visible: accountState.loggedIn
            anchors.verticalCenter: parent.verticalCenter
            readonly property bool member: vipState.tier !== "" && vipState.tier !== "none"
            implicitHeight: Theme.controlSm; leftPadding: Theme.s1; rightPadding: Theme.s3
            implicitWidth: leftPadding + rightPadding + vipRow.implicitWidth
            hoverEnabled: true
            Accessible.name: vipLabel.text
            onClicked: root.openSettings("account")
            background: Rectangle {
                radius: height / 2
                color: vipBadge.member ? (vipBadge.hovered ? Theme.text2 : Theme.text) : (vipBadge.hovered ? Theme.surfaceHover : "transparent")
                border.pixelAligned: false; antialiasing: true; border.width: vipBadge.member ? 0 : Theme.hairline; border.color: Theme.borderStrong

                PixelSnap.fit: true
            }
            contentItem: Row {
                id: vipRow; spacing: Theme.s2
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: vipBadge.implicitHeight - Theme.s1 * 2; height: width; radius: width / 2
                    color: vipBadge.member ? Theme.accent : Theme.surfaceActive
                    Text {
                        anchors.centerIn: parent; text: "V"; color: vipBadge.member ? Theme.inkOn(parent.color) : Theme.text3
                        font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.weight: Theme.fwBold
                    }
                }
                Text {
                    id: vipLabel; anchors.verticalCenter: parent.verticalCenter
                    text: vipBadge.member ? (vipState.label || "VIP 畅听中") : "开通会员"
                    color: vipBadge.member ? Theme.bg : Theme.text2
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.weight: Theme.fwMedium
                }
            }
        }
        IconButton {
            id: quickButton; objectName: "quickSettingsButton"
            anchors.verticalCenter: parent.verticalCenter
            glyph: "sliders-horizontal"; checked: quick.opened; Accessible.name: "快捷设置"
            onClicked: quick.opened ? quick.close() : quick.open()
        }
    }
    Popup {
        id: quick; objectName: "quickSettings"
        parent: quickButton
        x: quickButton.width - width; y: quickButton.height + Theme.s2
        width: 300; padding: Theme.s4
        transformOrigin: Popup.TopRight
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: music.reducedMotion ? 0 : Theme.durBase; easing.type: Easing.OutCubic }
        }
        background: Plate { radius: Theme.rLg; color: Theme.bgElevated; borderColor: Theme.borderStrong; elevation: 3 }
        component Label: Text { color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.weight: Theme.fwMedium }
        contentItem: Column {
            spacing: Theme.s3
            Label { text: "显示模式" }
            Segmented {
                objectName: "quickMode"; width: parent.width; value: appearanceState.mode
                options: [{value:"light",label:"浅色"},{value:"dark",label:"深色"},{value:"system",label:"跟随系统"}]
                onSelected: value => appearanceState.mode = value
            }
            Label { text: "主题颜色" }
            AccentPicker { width: parent.width; namePrefix: "quickAccent_" }
            Item {
                width: parent.width; height: compactToggle.height
                Text { anchors.verticalCenter: parent.verticalCenter; text: "紧凑列表"; color: Theme.text; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
                Toggle { id: compactToggle; objectName: "quickCompact"; anchors.right: parent.right; checked: appearanceState.compact; onToggled: appearanceState.compact = checked }
            }
            Label { text: "歌词字体" }
            Segmented {
                objectName: "quickLyricFont"; width: parent.width; value: appearanceState.lyricFont
                options: [{value:"skin",label:"默认"},{value:"kai",label:"文楷"},{value:"song",label:"宋体"},{value:"hei",label:"黑体"}]
                onSelected: value => appearanceState.lyricFont = value
            }
            Hairline { width: parent.width }
            ActionButton { objectName: "quickMore"; quiet: true; text: "更多设置"; glyph: "chevron-right"; onClicked: { quick.close(); root.openSettings("appearance") } }
        }
    }
}
