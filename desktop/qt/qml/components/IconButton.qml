import QtQuick
import QtQuick.Controls
import BottleMusic

// Icon button geometry: square sm 28 / md 34 / lg 40, ghost by default, --r-sm
// corners, 16px glyph. `checked` mirrors aria-pressed (surface-active fill).
// Fills ease over --dur-fast; presses dim to .86 and fill --surface-active.
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
    property color primaryHoverFill: Theme.customized ? Theme.actionShade(primaryFill, .08) : primaryFill
    readonly property var mediaSpec: Theme.mediaStyle[media] || Theme.mediaStyle.play
    property string size: "md"
    property string variant: "ghost"
    property real glyphSize: Theme.iconSize
    property real stroke: Theme.iconStroke
    property bool glyphFilled: false
    property real glyphRotation: 0
    property real radius: Theme.rSm
    property color tint: variant === "primary" ? Theme.accentContrast : Theme.text2
    property color hoverTint: variant === "primary" ? Theme.accentContrast : Theme.text
    property color hoverFill: Theme.surfaceHover
    property color checkedTint: Theme.customized ? Theme.accent : Theme.text
    property color primaryFill: Theme.accent
    property color primaryDownFill: Theme.customized ? Theme.actionShade(primaryFill, .16) : Theme.accentHover
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
    scale: pressed ? Theme.pressScale : 1
    Behavior on scale { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeBounce } }
    onCheckedChanged: if (checked && !music.reducedMotion) glyphPop.restart()
    SequentialAnimation {
        id: glyphPop
        NumberAnimation { target: glyphIcon; property: "scale"; to: 1.18; duration: Theme.durPop / 2; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeBounce }
        NumberAnimation { target: glyphIcon; property: "scale"; to: 1; duration: Theme.durPop / 2; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeBounce }
    }
    opacity: !enabled ? Theme.disabledOpacity : down ? Theme.pressedOpacity : 1
    Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
    Accessible.role: Accessible.Button
    background: Rectangle {
        radius: root.radius
        color: root.variant === "primary" ? (root.down ? root.primaryDownFill : root.lit ? root.primaryHoverFill : root.primaryFill)
            : root.checked && root.flatChecked ? Theme.clear(root.hoverFill)
            : root.down || root.checked ? root.downFill
            : root.lit ? root.hoverFill
            : root.variant === "secondary" ? Theme.surface : Theme.clear(root.hoverFill)
        border.width: root.variant === "secondary" ? Theme.hairline : 0
        border.color: Theme.border
        // Fill edges on whole device pixels, like Blink's snapped paint rect.
        PixelSnap.fit: true
        layer.enabled: root.shadowLevel > 0 && root.enabled && Theme.hasShadow(root.shadowLevel)
        layer.effect: ShadowEffect { level: root.shadowLevel }
        Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -4 / root.worldScale; radius: parent.radius + 4 / root.worldScale
            color: "transparent"; border.width: 2 / root.worldScale; border.color: root.focusInk
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
