import QtQuick
import QtQuick.Controls
import BottleMusic

Column {
    id: root
    spacing: 12
    property string loaded: ""
    function sync() {
        const snapshot = JSON.stringify(settingsState.device)
        if (snapshot === loaded) return
        loaded = snapshot
        dfid.text = settingsState.device.dfid || ""
        mid.text = settingsState.device.mid || ""
        uuid.text = settingsState.device.uuid || ""
    }
    Component.onCompleted: { settingsState.loadDevice(); sync() }
    Connections { target: settingsState; function onChanged() { root.sync() } }
    SettingItem {
        label: "设备参数"; badge: settingsState.device.registered ? "已注册" : "待注册"
        hint: "保存设备标识后重新获取音源，账号凭据由原生核心管理。"
        ActionButton { objectName: "deviceRefresh"; text: "刷新"; size: "sm"; enabled: !settingsState.busy; onClicked: settingsState.loadDevice() }
    }
    Text { text: "DFID"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
    TextField { id: dfid; objectName: "deviceDfid"; width: parent.width; placeholderText: "24 位设备标识"; selectByMouse: true; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.text; background: Rectangle { color: Theme.surface; border.color: Theme.border; border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; radius: Theme.rSm } }
    Text { text: "MID"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
    TextField { id: mid; objectName: "deviceMid"; width: parent.width; placeholderText: "32–40 位十六进制标识"; selectByMouse: true; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.text; background: Rectangle { color: Theme.surface; border.color: Theme.border; border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; radius: Theme.rSm } }
    Text { text: "UUID"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
    TextField { id: uuid; objectName: "deviceUuid"; width: parent.width; placeholderText: "十六进制标识或 GUID"; selectByMouse: true; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.text; background: Rectangle { color: Theme.surface; border.color: Theme.border; border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; radius: Theme.rSm } }
    Row {
        spacing: 8
        ActionButton { objectName: "deviceSave"; text: "保存参数"; filled: true; enabled: !settingsState.busy; onClicked: settingsState.saveDevice(dfid.text, mid.text, uuid.text) }
        ActionButton { objectName: "deviceTest"; text: "测试连接"; enabled: !settingsState.busy; onClicked: settingsState.testDevice() }
        ActionButton { objectName: "deviceReset"; text: "重置"; quiet: true; enabled: !settingsState.busy; onClicked: resetConfirmation.open() }
    }
    Text { width: parent.width; text: settingsState.message; wrapMode: Text.Wrap; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
    ConfirmDialog {
        id: resetConfirmation; title: "重置设备参数"
        message: "恢复自动生成的设备标识，并重新检查音源连接？"
        onAccepted: settingsState.resetDevice()
    }
}
