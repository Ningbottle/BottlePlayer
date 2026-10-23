import QtQuick
import QtQuick.Controls
import BottleMusic

Flickable {
    id: root
    objectName: "statsPage"
    clip: true; contentWidth: width; contentHeight: content.y + content.height + 32
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ThinScrollBar { }
    SmoothWheel { flickable: root }
    Component.onCompleted: statsState.setActive(true)
    Component.onDestruction: { statsState.setActive(false); analysisState.cancel() }
    property string topKind: "song"
    readonly property var topRows: topKind === "song" ? statsState.songs : topKind === "artist" ? statsState.artists : statsState.albums
    function duration(seconds) { const minutes = Math.floor(Number(seconds || 0) / 60); return minutes >= 60 ? Math.floor(minutes / 60) + " 小时 " + minutes % 60 + " 分钟" : minutes + " 分钟" }
    function playTop(index) {
        const rows = statsState.songs.filter(song => !!song.song_hash).map(song => ({hash: song.song_hash, title: song.name, artist: song.singer, album: song.album, albumId: song.album_id, image: song.cover_url, duration: 0}))
        const target = rows.findIndex(song => song.hash === statsState.songs[index].song_hash)
        if (target >= 0) music.playTracks(rows, target)
    }
    Column {
        id: content; x: root.width <= 700 ? 24 : 32; y: 24; width: parent.width - x * 2; spacing: 24
        Column {
            width: parent.width; spacing: 12
            CssText { text: "YOUR LISTENING / STATISTICS"; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; color: Theme.text3; font.letterSpacing: 1 }
            Row {
                width: parent.width
                CssText { width: parent.width - options.width; text: "聆听统计"; font.family: Theme.fontDisplay; font.pixelSize: Theme.fs2xl; color: Theme.text }
                Segmented { id: options; objectName: "statsRange"; value: statsState.range; options: [{value:"1d",label:"每日"},{value:"7d",label:"每周"},{value:"30d",label:"每月"}]; onSelected: value => statsState.range = value }
            }
            CssText { width: parent.width; text: "留住音乐陪伴你的时间。超过一分钟的真实聆听会计入统计。"; font.family: Theme.fontMeta; font.italic: Theme.metaItalic; font.pixelSize: Theme.fsSm; color: Theme.text3; wrapMode: Text.Wrap }
            Hairline { width: parent.width; color: Theme.ruleInk }
            Row {
                spacing: 12
                ActionButton { objectName: "statsRefresh"; text: statsState.loading ? "读取中…" : "刷新"; glyph: "refresh-cw"; enabled: !statsState.loading; onClicked: statsState.refresh() }
                CssText { anchors.verticalCenter: parent.verticalCenter; text: statsState.error; color: Theme.warning; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
            }
        }
        Row {
            width: parent.width; spacing: 20
            Repeater {
                model: [
                    {label:"播放次数",value:statsState.summary.total_plays || 0},
                    {label:"聆听时长",value:root.duration(statsState.summary.total_listened_seconds)},
                    {label:"听过歌曲",value:statsState.summary.unique_songs || 0}
                ]
                Column {
                    required property var modelData
                    width: (content.width - 40) / 3; spacing: 8
                    CssText { width: parent.width; text: statsState.loading ? "…" : statsState.error ? "—" : String(modelData.value); elide: Text.ElideRight; font.family: Theme.fontDisplay; font.pixelSize: Theme.fs2xl; color: Theme.text }
                    CssText { text: modelData.label; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs; color: Theme.text3 }
                }
            }
        }
        Column {
            width: parent.width; spacing: 16
            CssText { text: "聆听趋势"; font.family: Theme.fontTitle; font.pixelSize: Theme.fsXl; color: Theme.text }
            Text { text: "听过艺人 " + Number(statsState.summary.unique_artists || 0) + " 位，完成率 " + Math.round(Number(statsState.summary.completion_rate || 0) * 100) + "%"; visible: !statsState.loading && !statsState.error; font.family: Theme.fontMeta; font.pixelSize: Theme.fsSm; color: Theme.text3 }
            Row {
                width: parent.width; height: 140; spacing: 4
                readonly property real maximum: Math.max(1, ...statsState.timeline.map(row => Number(row.count || 0)))
                Repeater {
                    model: statsState.timeline
                    Item {
                        required property var modelData
                        required property int index
                        width: Math.max(4, (content.width - (statsState.timeline.length - 1) * 4) / Math.max(1, statsState.timeline.length)); height: parent.height
                        Rectangle { x: 2; width: Math.max(2, parent.width - 4); anchors.bottom: label.top; anchors.bottomMargin: 8; height: Math.max(2, Number(modelData.count || 0) / parent.parent.maximum * 100); color: Theme.accent; radius: Theme.rXs }
                        Text { id: label; anchors.bottom: parent.bottom; width: parent.width; text: index % Math.max(1, Math.ceil(statsState.timeline.length / 7)) === 0 ? String(modelData.date).slice(5) : ""; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs; color: Theme.text3; horizontalAlignment: Text.AlignHCenter }
                    }
                }
            }
            CssText { visible: !statsState.loading && statsState.timeline.length === 0 && !statsState.error; text: "这段时间还没有统计记录。"; font.family: Theme.fontMeta; font.pixelSize: Theme.fsSm; color: Theme.text3 }
        }
        Column {
            width: parent.width; spacing: 12
            Segmented { objectName: "statsTopKind"; value: root.topKind; options: [{value:"song",label:"歌曲"},{value:"artist",label:"艺人"},{value:"album",label:"专辑"}]; onSelected: value => root.topKind = value }
            Repeater {
                model: root.topRows
                AbstractButton {
                    required property var modelData
                    required property int index
                    width: parent.width; height: 56; hoverEnabled: true; enabled: root.topKind === "song" && !!modelData.song_hash
                    onClicked: root.playTop(index)
                    background: Rectangle { radius: Theme.rSm; color: parent.hovered ? Theme.surfaceHover : "transparent" }
                    contentItem: Item {
                        Cover { x: 4; anchors.verticalCenter: parent.verticalCenter; width: 40; height: 40; url: modelData.cover_url || ""; pixels: 80 }
                        Text { x: 60; anchors.verticalCenter: parent.verticalCenter; width: parent.width - x - 100; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd; color: Theme.text }
                        Text { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: modelData.play_count + " 次"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs }
                    }
                }
            }
        }
        Column {
            width: parent.width; spacing: 12
            Hairline { width: parent.width; color: Theme.ruleInk }
            CssText { text: "聆听分析"; font.family: Theme.fontTitle; font.pixelSize: Theme.fsXl; color: Theme.text }
            Text { width: parent.width; text: "点击分析会将当前统计发送给 DeepSeek，按其 API 规则计费。密钥仅保留在当前页面内存。"; wrapMode: Text.Wrap; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
            TextField { id: apiKey; objectName: "analysisKey"; width: parent.width; placeholderText: "DeepSeek API Key"; echoMode: TextInput.Password; selectByMouse: true; color: Theme.text; background: Rectangle { color: Theme.surface; border.color: Theme.border; border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; radius: Theme.rSm } }
            Row { spacing: 8; ActionButton { text: analysisState.loading ? "分析中…" : "分析当前统计"; filled: true; enabled: !analysisState.loading && !statsState.loading && !statsState.error; onClicked: analysisState.analyze(apiKey.text, statsState.summary, statsState.songs, statsState.timeline) } ActionButton { visible: analysisState.loading; text: "取消"; onClicked: analysisState.cancel() } }
            Text { visible: analysisState.error.length > 0; width: parent.width; text: analysisState.error; wrapMode: Text.Wrap; color: Theme.warning; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
            Text { width: parent.width; text: analysisState.result; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd }
        }
    }
}
