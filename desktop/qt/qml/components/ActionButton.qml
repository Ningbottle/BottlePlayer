import QtQuick
import QtQuick.Controls
import BottleMusic

// Action button variants: filled→primary, outlined→secondary, quiet→ghost.
// Heights ride the --control-* ladder (sm 28 / md 34 / lg 40), radius --r-sm,
// optional 16px leading/trailing Lucide glyphs with an 8px gap. Background,
// border, ink and opacity ease over --dur-fast like the CSS transitions; the
// press dims to .86 and secondary/ghost presses fill --surface-active.
Button {
    id: root
    property bool filled: false
    property bool quiet: false
    property bool active: false
    property string size: "md"
    property string glyph: ""
    property string media: ""
    property real worldScale: 1
    property color focusInk: Theme.focus
    property color hoverFill: Theme.surfaceHover
    property color downFill: Theme.surfaceActive
    readonly property var mediaSpec: Theme.mediaStyle[media] || Theme.mediaStyle.play
    property bool glyphFilled: false
    property real glyphSize: Theme.iconSize
    property real glyphStroke: Theme.iconStroke
    property string trailingGlyph: ""
    // Degrees applied to the trailing glyph (chevron flips ease over --dur-base).
    property real trailingRotation: 0
    property color restFill: variant === "secondary" ? Theme.surface : "transparent"
    property color edgeColor: variant === "primary" ? Theme.accent : variant === "ghost" ? "transparent" : Theme.border
    property real bgRadius: Theme.rSm
    property int typeSize: size === "sm" ? Theme.fsXs : size === "lg" ? Theme.fsMd : Theme.fsSm
    property int weight: Theme.fwMedium
    property int typeRendering: Text.NativeRendering
    readonly property string variant: filled ? "primary" : quiet ? "ghost" : "secondary"
    readonly property bool lit: enabled && (hovered || visualFocus)
    property color restInk: variant === "primary" ? Theme.accentContrast : variant === "ghost" ? Theme.text2 : Theme.text
    property color hoverInk: variant === "primary" ? Theme.accentContrast : Theme.text
    readonly property color ink: active || lit ? hoverInk : restInk
    readonly property color fill: variant === "primary" ? (down ? Theme.accentHover : Theme.accent)
        : down || active ? downFill
        : lit ? hoverFill
        : restFill.a > 0 ? restFill : Theme.clear(hoverFill)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    scale: pressed ? Theme.pressScale : 1
    Behavior on scale { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durPop; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeBounce } }
    padding: 0
    // padding-inline plus the 1px border every variant carries (transparent
    // on ghost), which Chromium lays out as one device pixel.
    leftPadding: (size === "sm" ? Theme.s2 : size === "lg" ? Theme.s4 : Theme.s3) + Theme.hairline
    rightPadding: leftPadding
    implicitHeight: size === "sm" ? Theme.controlSm : size === "lg" ? Theme.controlLg : Theme.controlMd
    font.family: Theme.fontUi
    font.pixelSize: typeSize
    font.weight: weight
    opacity: !enabled ? Theme.disabledOpacity : down ? Theme.pressedOpacity : 1
    Behavior on opacity { NumberAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
    background: Rectangle {
        radius: root.bgRadius
        color: root.fill
        border.width: Theme.hairline
        border.color: root.edgeColor
        // Frame edges on whole device pixels, like Blink's snapped paint rect.
        PixelSnap.fit: true
        Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
        Behavior on border.color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeSoft } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -4 / root.worldScale; radius: parent.radius + 4 / root.worldScale
            color: "transparent"; border.width: 2 / root.worldScale; border.color: root.focusInk
            visible: root.visualFocus
        }
    }
    contentItem: Item {
        id: content
        readonly property real extras: (root.glyph || root.media ? root.glyphSize + Theme.s2 : 0) + (root.trailingGlyph ? root.glyphSize + Theme.s2 : 0)
        // Sized from label.implicitWidth (not width) so eliding never feeds
        // back into the button's implicit size.
        implicitWidth: Theme.snapUp(labelMetrics.advanceWidth + extras + 1 / Theme.dpr)
        implicitHeight: Math.max(label.implicitHeight, root.glyph || root.media || root.trailingGlyph ? root.glyphSize : 0)
        Row {
            anchors.centerIn: parent
            spacing: Theme.s2
            Icon { objectName: root.objectName ? root.objectName + "_glyph" : ""; visible: root.glyph !== "" || root.media !== ""; name: root.media ? root.mediaSpec.glyph : root.glyph; size: root.glyphSize; stroke: root.media ? root.mediaSpec.stroke : root.glyphStroke; filled: root.media ? true : root.glyphFilled; stroked: root.media ? root.mediaSpec.stroked : true; rasterScale: root.worldScale; color: root.ink; anchors.verticalCenter: parent.verticalCenter }
            Text {
                id: label
                objectName: root.objectName ? root.objectName + "_label" : ""
                text: root.text; textFormat: Text.PlainText; font: root.font; color: root.ink
                renderType: root.typeRendering
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, content.width - content.extras)
                elide: Text.ElideRight
                Behavior on color { ColorAnimation { duration: music.reducedMotion ? 0 : Theme.durHover } }
            }
            Icon {
                visible: root.trailingGlyph !== ""; name: root.trailingGlyph; color: root.ink
                size: root.glyphSize; stroke: root.glyphStroke; rasterScale: root.worldScale
                anchors.verticalCenter: parent.verticalCenter
                rotation: root.trailingRotation
                Behavior on rotation { NumberAnimation { duration: Theme.durBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeStandard } }
            }
        }
        TextMetrics { id: labelMetrics; text: root.text; font: root.font; renderType: root.typeRendering }
    }
    Accessible.name: text
}
