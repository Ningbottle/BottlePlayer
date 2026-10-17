import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import BottleMusic

// Settings page layout: the sidebar lists the sections, the page
// shows one section as flat hairline rows under a kicker / display title /
// lede head. max-width 62rem, padding 16 / clamp(20px, 3vw, 40px) / 40,
// inside the shell's .scroll padding 20 / clamp(16px, 2vw, 32px) / 24.
Flickable {
    id: root
    objectName: "settingsPage"
    property var meta: ({ id: "appearance", label: "外观", lede: "" })
    property real viewportWidth: width
    signal enterPaper()
    signal enterCurtain()
    signal openEqualizer()
    FileDialog { id: exportFile; title: "导出偏好与播放数据"; fileMode: FileDialog.SaveFile; nameFilters: ["BottleMusic 数据 (*.json)"]; defaultSuffix: "json"; onAccepted: migrationState.exportData(selectedFile) }
    FileDialog { id: importFile; title: "选择播放器设置导出文件"; nameFilters: ["BottleMusic 数据 (*.json)"]; onAccepted: importConfirmation.open() }
    ConfirmDialog { id: importConfirmation; title: "导入播放与偏好数据"; message: "导入会暂停播放并更新偏好与队列。现有数据会先备份，账号凭据不会导入。"; onAccepted: migrationState.importData(importFile.selectedFile) }
    FileDialog { id: exportStatsFile; title: "导出聆听统计"; fileMode: FileDialog.SaveFile; nameFilters: ["聆听统计 (*.json)"]; defaultSuffix: "json"; onAccepted: migrationState.exportStatistics(selectedFile) }
    FileDialog { id: importStatsFile; title: "选择旧版数据库或聆听统计档案"; nameFilters: ["BottleMusic 统计 (*.db *.sqlite *.json)"]; onAccepted: statsConfirmation.open() }
    ConfirmDialog { id: statsConfirmation; title: "合并聆听统计"; message: "请先关闭旧播放器。只读取聆听统计，导入前备份现有统计。重复记录会去重，账号凭据不会复制。"; onAccepted: migrationState.importStatistics(importStatsFile.selectedFile) }
    clip: true
    contentWidth: width
    contentHeight: page.y + page.height + Theme.s6
    boundsBehavior: Flickable.StopAtBounds
    HoverHandler { id: scrollHover }
    ScrollBar.vertical: ThinScrollBar { areaHovered: scrollHover.hovered }
    SmoothWheel { flickable: root }

    readonly property real scrollInset: Theme.clamp(Theme.s4, viewportWidth * 0.02, Theme.s8)
    readonly property real inset: Theme.clamp(Theme.s5, viewportWidth * 0.03, Theme.s10)
    readonly property var lyricSizes: [
        { value: 26, label: "小", secondary: "S", objectName: "lyricSizeS" },
        { value: 32, label: "标准", secondary: "M", objectName: "lyricSizeM" },
        { value: 38, label: "大", secondary: "L", objectName: "lyricSizeL" },
        { value: 46, label: "特大", secondary: "XL", objectName: "lyricSizeXL" }
    ]
    function nearestLyricSize(px) {
        var best = lyricSizes[0].value
        for (var i = 0; i < lyricSizes.length; i++)
            if (Math.abs(lyricSizes[i].value - px) < Math.abs(best - px)) best = lyricSizes[i].value
        return best
    }
    ColorDialog { id: accentPicker; title: "强调色"; selectedColor: Theme.accent; onAccepted: appearanceState.accent = String(selectedColor) }

    Item {
        id: page
        // .settings-page: max-width 62rem (868px; 1rem is 14px), centered in
        // the .scroll content box.
        width: Math.min(Theme.rem(62), root.width - root.scrollInset * 2)
        x: (root.width - width) / 2; y: Theme.s5
        height: Theme.s4 + column.height + Theme.s10
        // .settings-content: the head, then the section, 24px apart. Laid
        // out with positioners and anchors (QtQuick.Layouts would round each
        // row to whole logical pixels); the snap puts the origin on a device
        // pixel the way Blink rounds the boxes' 450.5px left edge.
        Column {
            id: column
            x: root.inset; y: Theme.s4; width: page.width - root.inset * 2
            spacing: Theme.s6
            PixelSnap.enabled: true

            // .settings-head: grid gap 8 (the kicker adds an 8px margin),
            // 24px bottom padding, then the --rule-ink rule.
            Item {
                id: head
                width: parent.width
                height: rule.y + (rule.visible ? rule.height : 0)
                CssText {
                    id: kicker
                    text: "BOTTLEMUSIC / PREFERENCES"; color: Theme.text3
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; font.weight: Theme.fwMedium
                    font.letterSpacing: Theme.fsXs * 0.12
                }
                // --display-lg is 37.12px at 1280; the 1.15 line box keeps
                // the unrounded size so the rows below land where Blink's do.
                Text {
                    id: title
                    objectName: "settingsTitle"
                    y: kicker.height + Theme.s2 * 2
                    text: root.meta.label; color: Theme.text
                    readonly property real exact: Theme.displayLg(root.viewportWidth)
                    readonly property int size: Math.round(exact)
                    font.family: Theme.fontDisplay; font.pixelSize: size
                    font.weight: Theme.fwDisplay; font.letterSpacing: Theme.displayTracking * size
                    height: exact * 1.15; verticalAlignment: Text.AlignVCenter
                }
                // .settings-lede: --font-meta in --meta-style (italic serif in Mint).
                CssText {
                    id: lede
                    y: title.y + title.height + Theme.s2
                    width: parent.width
                    text: root.meta.lede; color: Theme.text3; wrapMode: Text.Wrap
                    font.family: Theme.fontMeta; font.pixelSize: Theme.fsSm; font.italic: Theme.metaItalic
                }
                Hairline {
                    id: rule
                    visible: Theme.rules
                    y: lede.y + lede.height + Theme.s6
                    width: parent.width
                    color: Theme.ruleInk
                }
            }

            Loader {
                width: parent.width
                sourceComponent: root.meta.id === "account" ? accountSection
                    : root.meta.id === "playback" ? playbackSection
                    : root.meta.id === "storage" ? storageSection
                    : root.meta.id === "about" ? aboutSection
                    : root.meta.id === "experimental" ? labSection
                    : root.meta.id === "diagnostics" ? diagnosticsSection
                    : appearanceSection
            }
        }
    }

    Component {
        id: accountSection
        AccountPanel { }
    }

    // .settings-interface-options: two columns, 20px gap; each choice is a
    // --surface grid (8px gap, 20px padding) between 1px --border-strong
    // rules: mono number, 1.4rem/1.4 title, fs-sm caption, and a 1rem arrow
    // pinned 20px inside the top-right corner.
    component InterfaceOption: AbstractButton {
        id: choice
        property string number: ""
        property string title: ""
        property string caption: ""
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        leftPadding: Theme.s5; rightPadding: Theme.s5
        topPadding: Theme.s5 + Theme.hairline; bottomPadding: Theme.s5 + Theme.hairline
        Accessible.name: title
        background: Rectangle {
            color: choice.hovered ? Theme.surfaceHover : Theme.surface
            Behavior on color { ColorAnimation { duration: 200 } }
            Hairline { width: parent.width; color: Theme.borderStrong }
            Hairline { anchors.bottom: parent.bottom; width: parent.width; color: Theme.borderStrong }
            Icon {
                anchors.right: parent.right; anchors.rightMargin: Theme.s5
                y: Theme.hairline + Theme.s5
                size: Theme.rem(1); name: "arrow-up-right"; color: Theme.text3
            }
            // outline: 2px --focus, offset 4px.
            Rectangle {
                anchors.fill: parent; anchors.margins: -6
                color: "transparent"; border.width: 2; border.color: Theme.focus
                visible: choice.visualFocus
            }
        }
        contentItem: Column {
            spacing: Theme.s2
            // font: fs-xs mono resets line-height to normal: Blink rounds
            // the ascent and descent to device pixels (14.4px at 125%).
            Text {
                height: (Math.round(mono.ascent * Theme.dpr) + Math.round(mono.descent * Theme.dpr)) / Theme.dpr + mono.leading
                verticalAlignment: Text.AlignVCenter
                text: choice.number; color: Theme.text3; font: mono.font
                FontMetrics { id: mono; font.family: Theme.fontMono; font.pixelSize: Theme.fsXs }
            }
            Text {
                height: Theme.rem(1.4) * 1.4; verticalAlignment: Text.AlignVCenter
                text: choice.title; color: Theme.text
                font.family: Theme.fontTitle; font.pointSize: Theme.pt(Theme.rem(1.4)); font.weight: Theme.fwMedium
            }
            CssText {
                text: choice.caption; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
            }
        }
    }

    Component {
        id: appearanceSection
        Column {
            SettingItem {
                visible: appearanceState.storageError.length > 0
                label: appearanceState.storageError
                ActionButton { text: "重试保存"; onClicked: appearanceState.retryStorage() }
            }
            Text { text: "界面"; color: Theme.text; font.family: Theme.fontUi; font.pixelSize: 14; font.weight: Theme.fwSemiBold; topPadding: 20; bottomPadding: 8 }
            SettingItem {
                label: "聆听方式"; stacked: true
                hint: "报刊负责浏览与管理，幕布留给音乐。两种界面共用播放与队列，切换时播放不中断。"
                // repeat(2, minmax(0, 1fr)) with a 20px gap.
                Row {
                    id: interfaces
                    width: parent.width; spacing: Theme.s5
                    readonly property real column: (width - spacing) / 2
                    InterfaceOption {
                        width: interfaces.column
                        objectName: "settingsOpenPaper"; number: "01"; title: "报刊"; caption: "推荐、搜索、歌单与设置"
                        onClicked: root.enterPaper()
                    }
                    InterfaceOption {
                        width: interfaces.column
                        objectName: "settingsOpenCurtain"; number: "02"; title: "幕布舞台"; caption: "沉浸舞台 · 向前发现"
                        onClicked: root.enterCurtain()
                    }
                }
            }
            SettingItem {
                label: "光感"
                Segmented { objectName: "appearanceMode"; value: appearanceState.mode; options: [{value:"light",label:"浅色",objectName:"appearanceLight"},{value:"dark",label:"深色",objectName:"appearanceDark"},{value:"system",label:"跟随系统",objectName:"appearanceSystem"}]; onSelected: value => appearanceState.mode = value }
            }
            SettingItem {
                label: "强调色"; hint: "按钮、进度与当前播放的颜色"
                Row { spacing: 8; ActionButton { text: "选择颜色"; onClicked: accentPicker.open() } ActionButton { text: "恢复默认"; quiet: true; onClicked: appearanceState.accent = "" } }
            }
            SettingItem {
                label: "紧凑列表"; hint: "每屏显示更多歌曲"
                Toggle { objectName: "compactListToggle"; checked: appearanceState.compact; onToggled: appearanceState.compact = checked }
            }
            Text { text: "歌词"; color: Theme.text; font.family: Theme.fontUi; font.pixelSize: 14; font.weight: Theme.fwSemiBold; topPadding: 20; bottomPadding: 8 }
            SettingItem {
                label: "歌词字体"; hint: "普通与全屏歌词的字体偏好"
                Segmented { objectName: "lyricFontControl"; value: appearanceState.lyricFont; options: [{value:"skin",label:"跟随皮肤"},{value:"kai",label:"文楷"},{value:"song",label:"宋体"},{value:"hei",label:"黑体"}]; onSelected: value => appearanceState.lyricFont = value }
            }
            SettingItem {
                label: "歌词阅读"; hint: "舞台模式在上下边缘淡出，阅读模式保持完整文字。"
                Segmented { objectName: "lyricFocusControl"; value: appearanceState.lyricFocus; options: [{value:"readable",label:"阅读"},{value:"stage",label:"舞台"}]; onSelected: value => appearanceState.lyricFocus = value }
            }
            SettingItem {
                label: "歌词字号"; hint: "普通播放页、全屏与海报墙同步缩放，小窗口自动适配。"
                Segmented {
                    objectName: "lyricSizeControl"
                    options: root.lyricSizes
                    value: root.nearestLyricSize(music.lyricSize)
                    onSelected: choice => music.lyricSize = choice
                }
            }
            Text { text: "播放画面"; color: Theme.text; font.family: Theme.fontUi; font.pixelSize: 14; font.weight: Theme.fwSemiBold; topPadding: 20; bottomPadding: 8 }
            SettingItem {
                label: "封面"; hint: "唱片仅在播放时旋转，暂停后停在原处。"
                Segmented {
                    objectName: "coverModeControl"
                    options: [
                        { value: false, label: "方形", secondary: "Square", objectName: "squareMode" },
                        { value: true, label: "唱片", secondary: "Vinyl", objectName: "vinylMode" }
                    ]
                    value: music.vinyl
                    onSelected: choice => music.vinyl = choice
                }
            }
            SettingItem {
                label: "减少动画"; hint: "停止唱片旋转，歌词直接跳到当前行。"
                Toggle {
                    objectName: "reducedMotionSwitch"
                    checked: music.reducedMotion
                    Accessible.name: "减少动画"
                    onToggled: music.reducedMotion = checked
                }
            }
        }
    }

    Component {
        id: playbackSection
        Column {
            SettingItem {
                label: "音乐核心"; badge: music.ready ? "已连接" : "未连接"; badgeAccent: music.ready
                hint: "EchoCAPI 原生核心负责推荐、搜索与取流。"
            }
            DeviceSettings { width: parent.width }
            SettingItem {
                label: "输出设备"
                hint: "当前输出：" + equalizerState.activeDeviceName
                ComboBox { objectName: "audioOutputDevice"; model: equalizerState.devices; textRole: "name"; valueRole: "id"; currentIndex: model.findIndex(row => row.id === equalizerState.deviceId); onActivated: equalizerState.deviceId = currentValue }
            }
            SettingItem { label: "均衡器"; hint: "十段频率与音效预设"; ActionButton { text: "打开均衡器"; onClicked: root.openEqualizer() } }
        }
    }

    Component {
        id: storageSection
        Column {
            SettingItem {
                label: "独立配置目录"
                hint: "设置与播放记录保存在当前数据目录中，可打开目录查看或备份。"
                ActionButton { text: "打开目录"; size: "sm"; onClicked: settingsState.openProfileFolder() }
            }
            SettingItem {
                label: "迁移播放与偏好数据"; stacked: true; hint: "导入会备份现有设置。导出不含账号凭据、设备指纹或 API Key。"
                Flow { width: parent.width; spacing: 8; ActionButton { objectName: "exportUserData"; text: "导出偏好"; glyph: "download"; enabled: !migrationState.busy; onClicked: exportFile.open() } ActionButton { objectName: "importUserData"; text: "导入偏好"; glyph: "upload"; enabled: !migrationState.busy; onClicked: importFile.open() } ActionButton { visible: migrationState.restartNeeded; text: "重新启动"; onClicked: migrationState.restartApp() } }
            }
            SettingItem {
                label: "聆听统计档案"; stacked: true; hint: "可合并兼容的聆听数据库或统计档案，导入前备份当前记录，不覆盖源文件。"
                Flow { width: parent.width; spacing: 8; ActionButton { objectName: "exportStats"; text: "导出统计"; glyph: "download"; enabled: !migrationState.busy; onClicked: exportStatsFile.open() } ActionButton { objectName: "importStats"; text: "合并统计"; glyph: "upload"; enabled: !migrationState.busy; onClicked: importStatsFile.open() } ActionButton { visible: migrationState.busy; text: "停止迁移"; onClicked: migrationState.cancelStatistics() } }
            }
            Text { width: parent.width; text: migrationState.message; wrapMode: Text.Wrap; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.text2 }
        }
    }

    Component {
        id: aboutSection
        Column {
            SettingItem { label: "BottleMusic"; hint: "酷狗概念版非官方桌面播放器 · Qt Quick 预览版" }
            SettingItem {
                label: "Qt 更新"; stacked: true; hint: "仅使用本项目的 Qt x64 更新包，下载后检查 SHA-256。"
                Row { spacing: 8; ActionButton { objectName: "checkUpdates"; text: updateState.busy ? "处理中…" : "检查更新"; enabled: !updateState.busy; onClicked: updateState.check() } ActionButton { visible: updateState.version.length > 0; text: "下载 v" + updateState.version; enabled: !updateState.busy; onClicked: updateState.download() } ActionButton { visible: updateState.downloaded; text: "打开更新包"; onClicked: updateState.openDownloaded() } ActionButton { visible: updateState.version.length > 0 && !updateState.busy; text: "跳过此版本"; quiet: true; onClicked: updateState.skip() } }
            }
            Text { width: parent.width; text: updateState.message + (updateState.busy && updateState.progress > 0 ? " " + updateState.progress + "%" : ""); wrapMode: Text.Wrap; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.text2 }
            Text { width: parent.width; text: updateState.notes; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.text3 }
            ActionButton { text: "项目与发布页"; quiet: true; onClicked: settingsState.openExternal("https://github.com/Ningbottle/BottlePlayer/releases") }
        }
    }
    Component {
        id: labSection
        Column {
            SettingItem { label: "实验室模式"; hint: "允许使用实验性音频处理，默认关闭。"; Toggle { checked: appearanceState.labMode; onToggled: appearanceState.labMode = checked } }
        }
    }
    Component {
        id: diagnosticsSection
        Column {
            width: parent.width; spacing: 16
            Component.onCompleted: settingsState.loadDiagnostics()
            Row { spacing: 8; ActionButton { text: "刷新诊断"; onClicked: settingsState.loadDiagnostics() } ActionButton { text: "复制"; onClicked: settingsState.copyDiagnostics() } }
            Text { width: parent.width; text: settingsState.diagnosticText; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; font.family: Theme.fontMono; font.pixelSize: Theme.fsXs; color: Theme.text2 }
        }
    }
}
