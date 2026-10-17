import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

// Player bar geometry: --playerbar-h strip (Mint 76 / Aurora 84) across the window
// with a --rule-w --rule-ink top rule, grid
// [1fr | clamp(18rem, 36vw, 36rem) | 1fr] with a 24px gap and 20px inline
// padding. Track (cover, title/artist, heart) · loop, transport and lyrics
// over progress · queue + volume. Every glyph here is 18px (--icon-size).
Rectangle {
    id: root
    objectName: "paperPlayerDock"
    color: Theme.barBg
    height: Theme.playerbarH
    property bool queueOpen: false
    property bool lyricView: false
    signal openPlayer()
    signal openImmersive()
    signal toggleLyrics()
    signal toggleQueue()
    signal addToPlaylist(var track)

    // border-top: var(--rule-w) solid var(--rule-ink).
    Hairline { visible: Theme.rules; width: parent.width; color: Theme.ruleInk }

    readonly property real centerWidth: Math.min(Theme.rem(36), Math.max(Theme.rem(18), width * 0.36))
    readonly property real sideWidth: Math.max(0, (width - Theme.s5 * 2 - Theme.s6 * 2 - centerWidth) / 2)

    // Track
    Row {
        x: Theme.s5; width: root.sideWidth; anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s3
        AbstractButton {
            id: coverButton
            width: Theme.coverInBar; height: Theme.coverInBar
            enabled: music.queueIndex >= 0
            hoverEnabled: true
            onClicked: root.openPlayer()
            Accessible.name: "打开歌词"
            contentItem: Cover {
                radius: Theme.rMd; pixels: 200
                url: music.current.image || ""; title: ""
                layer.enabled: Theme.hasShadow(1)
                layer.effect: ShadowEffect { level: 1 }
            }
            background: null
        }
        // .player-bar-info-button (flex 0 1 auto): as wide as its text, so
        // the heart follows the words instead of the column's far edge.
        // Both lines inherit the button's --lh-tight; the artist line stays
        // in --font-ui even in Mint (only the title takes --font-title).
        Column {
            anchors.verticalCenter: parent.verticalCenter
            readonly property real natural: Math.max(trackTitle.implicitWidth,
                artist.implicitWidth + (status.visible ? Theme.s2 + status.implicitWidth : 0))
            width: Math.max(0, Math.min(natural, root.sideWidth - Theme.coverInBar - favorite.width - Theme.s3 * 2))
            spacing: 2
            Text {
                id: trackTitle
                width: parent.width; height: Theme.fsMd * Theme.lhTight; verticalAlignment: Text.AlignVCenter
                text: music.current.title || "未播放歌曲"; color: Theme.text; elide: Text.ElideRight
                font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd; font.weight: Theme.fwMedium
            }
            Row {
                width: parent.width; spacing: Theme.s2
                Text {
                    id: artist
                    width: Math.min(implicitWidth, parent.width - (status.visible ? status.implicitWidth + Theme.s2 : 0))
                    height: Theme.fsXs * Theme.lhTight; verticalAlignment: Text.AlignVCenter
                    text: music.current.artist || "选一首歌开始"; color: Theme.text2; elide: Text.ElideRight
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                }
                Text {
                    id: status
                    visible: music.preview
                    height: artist.height; verticalAlignment: Text.AlignVCenter
                    text: "试听"; color: Theme.warning
                    font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                }
            }
        }
        // .player-bar-favorite: --text-3 heart; pressed fills it in --accent.
        IconButton {
            id: favorite
            objectName: "favoriteButton"
            anchors.verticalCenter: parent.verticalCenter
            glyph: "heart"; glyphSize: 18; glyphFilled: music.favorite
            enabled: music.queueIndex >= 0
            checked: music.favorite; flatChecked: true
            tint: Theme.text3; checkedTint: Theme.accent
            Accessible.name: music.favorite ? "已收藏" : "收藏"
            onClicked: music.toggleFavorite()
        }
        IconButton { size: "sm"; glyph: "plus"; enabled: music.queueIndex >= 0; Accessible.name: "添加当前歌曲到歌单"; onClicked: root.addToPlaylist(music.current) }
    }

    // Loop · transport · lyrics over the progress line (2px row gap),
    // centred in the bar. The side keys rest in --text-3 and turn --accent,
    // without a fill, while pressed.
    Column {
        x: (root.width - width) / 2; width: root.centerWidth
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        RowLayout {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.s3
            IconButton {
                objectName: "loopButton"
                glyphSize: 18; tint: Theme.text3; checkedTint: Theme.accent; flatChecked: true
                glyph: music.loopMode === "random" ? "shuffle" : music.loopMode === "single" ? "repeat-1" : "repeat"
                enabled: music.queueIndex >= 0 && !music.radio
                checked: music.loopMode !== "list"
                Accessible.name: music.loopMode === "random" ? "随机播放" : music.loopMode === "single" ? "单曲循环" : "列表循环"
                onClicked: music.cycleLoopMode()
            }
            Transport { id: transport }
            IconButton {
                objectName: "lyricsButton"
                glyph: "mic-vocal"; glyphSize: 18; tint: Theme.text3; checkedTint: Theme.accent; flatChecked: true
                checked: root.lyricView
                Accessible.name: "歌词"; onClicked: root.toggleLyrics()
            }
        }
        SeekBar { width: parent.width }
    }

    // Actions: right-aligned, 8px apart, --text-2 ink.
    Row {
        anchors.right: parent.right; anchors.rightMargin: Theme.s5; anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s2
        ActionButton {
            objectName: "qualityButton"; size: "sm"; text: music.qualityText(music.actualQuality)
            enabled: music.queueIndex >= 0 && !String(music.current.hash).startsWith("file:")
            onClicked: qualityMenu.popup()
            QualityMenu { id: qualityMenu; objectName: "qualityMenu" }
        }
        IconButton {
            objectName: "queueButton"; glyph: "list-music"; glyphSize: 18
            checked: root.queueOpen
            Accessible.name: "播放队列"; onClicked: root.toggleQueue()
        }
        Row {
            id: volumeGroup
            spacing: Theme.s1
            property real restore: 0.7
            HoverHandler { id: volumeHover }
            IconButton {
                glyphSize: 18
                glyph: music.volume <= 0 ? "volume-x" : music.volume < 0.34 ? "volume" : music.volume < 0.67 ? "volume-1" : "volume-2"
                Accessible.name: music.volume > 0 ? "静音" : "取消静音"
                onClicked: {
                    if (music.volume > 0) { volumeGroup.restore = music.volume; music.volume = 0 }
                    else music.volume = Math.max(0.1, volumeGroup.restore)
                }
            }
            Slider {
                id: volume
                visible: root.width >= 860
                anchors.verticalCenter: parent.verticalCenter
                width: root.width < 1100 ? Theme.s10 : Theme.rem(6); height: Theme.s4; padding: 0
                from: 0; to: 1; value: music.volume; onMoved: music.volume = value
                Accessible.name: "音量"
                background: Rectangle {
                    y: (volume.height - height) / 2; width: volume.width; height: Theme.s1
                    radius: Theme.rFull; color: Theme.progressTrack
                    Rectangle {
                        width: volume.visualPosition * parent.width; height: parent.height; radius: Theme.rFull
                        color: volumeHover.hovered ? Theme.accent : Theme.text2
                    }
                }
                handle: Rectangle {
                    x: volume.visualPosition * (volume.width - width); y: (volume.height - height) / 2
                    width: Theme.s3; height: Theme.s3; radius: Theme.rFull; color: Theme.text
                    opacity: volumeHover.hovered || volume.pressed || volume.visualFocus ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
                    layer.enabled: Theme.hasShadow(1)
                    layer.effect: ShadowEffect { level: 1 }
                }
            }
        }
    }
}
