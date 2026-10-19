import QtQuick
import QtQuick.Controls
import BottleMusic

Flickable {
    id: root
    objectName: "equalizerPage"
    clip: true; contentWidth: width; contentHeight: content.y + content.height + 32; boundsBehavior: Flickable.StopAtBounds
    SnapSettle { flickable: root }
    ScrollBar.vertical: ThinScrollBar { }
    readonly property var frequencies: ["31 Hz","62 Hz","125 Hz","250 Hz","500 Hz","1 kHz","2 kHz","4 kHz","8 kHz","16 kHz"]
    Column {
        id: content; x: 24; y: 24; width: parent.width - x * 2; spacing: 24
        CssText { text: "SOUND ROOM / EQUALIZER"; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; color: Theme.text3; font.letterSpacing: 1 }
        CssText { text: "均衡器"; font.family: Theme.fontDisplay; font.pixelSize: Theme.fs2xl; color: Theme.text }
        Text { width: parent.width; text: equalizerState.reason; wrapMode: Text.Wrap; color: equalizerState.active ? Theme.accent : Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
        SettingItem { label: "启用音频处理"; hint: "须先开启实验室模式；处理失败时自动回退原始音频。"; Toggle { objectName: "equalizerEnabled"; enabled: equalizerState.allowed; checked: equalizerState.enabled; onToggled: equalizerState.enabled = checked } }
        Row {
            width: parent.width; spacing: 4; height: 220
            Repeater {
                model: 10
                Column {
                    required property int index
                    width: (content.width - 36) / 10; spacing: 8
                    Text { width: parent.width; text: root.frequencies[index]; horizontalAlignment: Text.AlignHCenter; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: 10 }
                    Slider {
                        id: gain; objectName: "eqBand_" + index
                        anchors.horizontalCenter: parent.horizontalCenter; height: 160; orientation: Qt.Vertical
                        from: -6; to: 6; stepSize: .5; value: Number(equalizerState.bands[index]); enabled: equalizerState.allowed
                        onMoved: equalizerState.setBand(index, value)
                    }
                    Text { width: parent.width; text: Number(equalizerState.bands[index]).toFixed(1) + " dB"; horizontalAlignment: Text.AlignHCenter; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: 10 }
                }
            }
        }
        Hairline { width: parent.width; color: Theme.ruleInk }
        CssText { text: "音效预设"; font.family: Theme.fontTitle; font.pixelSize: Theme.fsXl; color: Theme.text }
        Flow {
            width: parent.width; spacing: 8
            Repeater {
                model: [{value:"Flat",label:"中性"},{value:"Bass Boost",label:"低频增强"},{value:"Vocal",label:"人声"},{value:"Rock",label:"摇滚"},{value:"Harman Kardon",label:"哈曼卡顿"},{value:"125Hz Test",label:"125 Hz 测试"}]
                ActionButton { required property var modelData; objectName: "eqPreset_" + modelData.value; text: modelData.label; active: equalizerState.preset === modelData.value; enabled: equalizerState.allowed; onClicked: equalizerState.applyPreset(modelData.value) }
            }
        }
    }
}
