import QtQuick
import QtQuick.Controls
import BottleMusic

// Shared quality picker menu. Trigger surfaces differ (player-bar chip,
// listening panel field, player-page card) but the option list and the
// checked state stay identical to the player-bar behavior.
Menu {
    id: menu
    readonly property string selectedQuality: music.queueIndex < 0 ? music.quality : music.actualQuality
    padding: Theme.s1
    font.family: Theme.fontUi
    font.pixelSize: Theme.fsMd
    palette.window: Theme.surface; palette.text: Theme.text
    palette.windowText: Theme.text; palette.buttonText: Theme.text
    palette.highlight: Theme.accentSoft; palette.highlightedText: Theme.text
    background: Rectangle {
        implicitWidth: 160
        color: Theme.bgElevated; radius: Theme.rMd
        border.color: Theme.borderStrong
        border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline
    }
    Repeater {
        // With no track, choose a preference for the next play. A cleared
        // queue must not keep the previous track's available-only choices.
        model: music.queueIndex >= 0 && music.availableQualities.length ? music.availableQualities
            : music.qualityCatalog
        MenuItem {
            id: choice
            required property var modelData
            objectName: "quality_" + modelData.quality
            text: music.qualityText(modelData.quality); checkable: true; autoExclusive: false
            enabled: music.queueIndex < 0 || (music.ready && !music.loading)
            checked: menu.selectedQuality === modelData.quality
            background: Rectangle {
                radius: Theme.innerRadius(Theme.rMd, menu.padding)
                color: choice.highlighted ? Theme.surfaceActive : "transparent"
            }
            onTriggered: {
                music.quality = modelData.quality
                // MenuItem toggles itself before triggering, even if the
                // request is rejected or still loading. Restore the binding
                // so only a successfully loaded source receives the check.
                checked = Qt.binding(function() { return menu.selectedQuality === choice.modelData.quality })
            }
        }
    }
}
