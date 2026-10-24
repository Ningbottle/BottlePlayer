import QtQuick
import QtQuick.Controls
import BottleMusic

// Shared quality picker menu. Trigger surfaces differ (player-bar chip,
// listening panel field, player-page card) but the option list and the
// checked state stay identical to the player-bar behavior.
Menu {
    id: menu
    // Playing tracks check what the server actually delivered; an empty queue
    // checks the preference that the next play will request.
    readonly property string selectedQuality: music.queueIndex < 0 ? music.quality : music.actualQuality
    palette.window: Theme.surface; palette.text: Theme.text
    palette.highlight: Theme.accentSoft; palette.highlightedText: Theme.text
    Repeater {
        model: music.queueIndex >= 0 && music.availableQualities.length ? music.availableQualities : music.qualityCatalog
        MenuItem {
            id: choice
            required property var modelData
            objectName: "quality_" + modelData.quality
            text: music.qualityText(modelData.quality); checkable: true; autoExclusive: false
            enabled: music.queueIndex < 0 || (music.ready && !music.loading)
            checked: menu.selectedQuality === modelData.quality
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
