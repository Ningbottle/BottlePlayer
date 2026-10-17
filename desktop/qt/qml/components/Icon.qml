import QtQuick
import BottleMusic

// Official Lucide SVG from assets/icons, tinted by the C++ icon provider. The
// wrapper owns logical dimensions; the inner image owns its device texture.
// Chromium rasterises an svg into its device-pixel-snapped box; the glyph is
// sized to whole device pixels and pinned to the pixel grid so the texture
// maps 1:1 instead of being filtered across two rows.
Item {
    id: root
    property string name: ""
    property color color: Theme.text2
    property real size: Theme.iconSize
    property real stroke: Theme.iconStroke
    property real rasterScale: 1
    property bool filled: false
    property bool stroked: true
    implicitWidth: Theme.snap(size * rasterScale) / rasterScale
    implicitHeight: Theme.snap(size * rasterScale) / rasterScale
    width: Theme.snap(size * rasterScale) / rasterScale
    height: width
    Image {
        anchors.fill: parent
        // Qt scales provider sourceSize by the window DPR again. Encode the
        // final raster size in the cache key so the provider owns exact pixels.
        source: root.name ? "image://icon/" + root.name + "/" + String(root.color).slice(1) + "/" + root.stroke + (root.filled ? "/fill" : "") + (root.stroked ? "" : "/nostroke") + "/px" + Math.max(1,Math.round(root.width * Theme.dpr * root.rasterScale)) : ""
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
    PixelSnap.enabled: true
}
