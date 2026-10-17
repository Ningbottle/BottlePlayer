import QtQuick
import QtQuick.Controls
import BottleMusic

// Shared quality picker menu. Trigger surfaces differ (player-bar chip,
// listening panel field, player-page card) but the option list and the
// checked state stay identical to the player-bar behavior.
Menu {
    id: menu
    palette.window: Theme.surface; palette.text: Theme.text
    palette.highlight: Theme.accentSoft; palette.highlightedText: Theme.text
    Repeater {
        model: music.availableQualities.length ? music.availableQualities : [{quality:"128"},{quality:"320"},{quality:"flac"}]
        MenuItem {
            required property var modelData
            objectName: "quality_" + modelData.quality
            text: music.qualityText(modelData.quality); checkable: true; checked: music.quality === modelData.quality
            onTriggered: music.quality = modelData.quality
        }
    }
}
