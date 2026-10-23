import QtQuick
import QtQuick.Controls
import BottleMusic

// Stable icon targets with shared optical geometry and keyboard focus.
AbstractButton {
    id: root
    property string glyph: ""
    property string media: ""
    property real mediaBox: 0
    property real mediaGlyph: 0
    property real boxSize: 0
    property real worldScale: 1
    property color focusInk: Theme.focus
    property color downFill: Theme.surfaceActive
    property color primaryHoverFill: Theme.actionShade(primaryFill, .08)
    readonly property var mediaSpec: Theme.mediaStyle[media] || Theme.mediaStyle.play
    property string size: "md"
    property string variant: "ghost"
    property real glyphSize: Theme.iconSize
    property real stroke: Theme.iconStroke
    property bool glyphFilled: false
    property real glyphRotation: 0
    property real radius: Theme.rSm
    property color tint: variant === "primary" ? Theme.inkOn(down ? primaryDownFill : lit ? primaryHoverFill : primaryFill) : Theme.text2
    property color hoverTint: variant === "primary" ? tint : Theme.text
    property color hoverFill: Theme.surfaceHover
    property color checkedTint: Theme.accentText
    property color primaryFill: Theme.accent
    property color primaryDownFill: Theme.actionShade(primaryFill, .16)
    // Elevation renders on the content-free background face, like Plate:
    // layering the whole button would rasterize the glyph with the shadow.
    property int shadowLevel: 0
    // .player-bar-side / .player-bar-favorite [aria-pressed]: pressed reads as
    // ink only (checkedTint) and keeps a clear background under hover/press.
    property bool flatChecked: false
    readonly property real side: size === "sm" ? Theme.controlSm : size === "lg" ? Theme.controlLg : Theme.controlMd
    // SVGs use their own viewBox and stay centred independently of font metrics.
    property real glyphLift: 0
    readonly property bool lit: enabled && (hovered || visualFocus)
    implicitWidth: media ? Theme.snap(mediaBox) / worldScale : boxSize > 0 ? boxSize : side
    implicitHeight: implicitWidth
    PixelSnap.enabled: media !== ""
    // QML's default centred anchors round halves in logical coordinates.
    // Media sizes are already snapped in device pixels, so retain fractions.
    anchors.alignWhenCentered: media === ""
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Keys.onShortcutOverride: event => { if (enabled && event.key === Qt.Key_Space) event.accepted = true }
    scale: 1

    ToolTip.visible: enabled && hovered && Accessible.name.length > 0
    ToolTip.text: Accessible.name
    ToolTip.delay: 600
    ToolTip.timeout: 2500
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
    Accessible.role: Accessible.Button
    background: Rectangle {
        radius: root.radius
        color: root.variant === "primary" ? (root.down ? root.primaryDownFill : root.lit ? root.primaryHoverFill : root.primaryFill)
            : root.checked && root.flatChecked ? Theme.clear(root.hoverFill)
            : root.down || root.checked ? root.downFill
            : root.lit ? root.hoverFill
            : root.variant === "secondary" ? Theme.surface : Theme.clear(root.hoverFill)
        border.pixelAligned: false; antialiasing: true; border.width: root.variant === "secondary" ? Theme.hairline : 0
        border.color: Theme.border
        // Fill edges on whole device pixels, like Blink's snapped paint rect.
        PixelSnap.fit: true
        layer.enabled: root.shadowLevel > 0 && root.enabled && Theme.hasShadow(root.shadowLevel)
        layer.effect: ShadowEffect { level: root.shadowLevel }

        Rectangle {
            anchors.fill: parent; anchors.margins: -4 / root.worldScale; radius: parent.radius + 4 / root.worldScale
            color: "transparent"; border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline / root.worldScale; border.color: root.focusInk
            visible: root.visualFocus
        }
    }
    contentItem: Item {
        Icon {
            id: glyphIcon
            objectName: root.objectName ? root.objectName + "_glyph" : ""
            anchors.centerIn: parent; anchors.verticalCenterOffset: -root.glyphLift
            anchors.alignWhenCentered: root.media === ""
            anchors.horizontalCenterOffset: root.media ? Theme.mediaOffset(root.media, Theme.glyphFit(root.mediaGlyph, Theme.snap(root.mediaBox))) / root.worldScale : 0
            name: root.media ? root.mediaSpec.glyph : root.glyph
            size: root.media ? Theme.glyphFit(root.mediaGlyph, Theme.snap(root.mediaBox)) / root.worldScale : root.glyphSize
            stroke: root.media ? root.mediaSpec.stroke : root.stroke
            filled: root.media ? true : root.glyphFilled
            stroked: root.media ? root.mediaSpec.stroked : true
            rasterScale: root.worldScale
            rotation: root.glyphRotation
            Behavior on rotation { NumberAnimation { duration: Theme.durBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
            color: root.checked ? root.checkedTint : root.lit ? root.hoverTint : root.tint
        }
    }
}
