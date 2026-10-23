import QtQuick
import QtQuick.Effects
import BottleMusic

// Artwork tile. Rounded corners clip the image through a MultiEffect mask
// (Item.clip only clips to the bounding box, which left square artwork
// poking out of rounded cards). Missing art shows the title monogram, or the
// Disc3 placeholder the player bar uses.
Item {
    id: root
    property string url: ""
    property string title: ""
    property int pixels: 400
    property real radius: 0
    // Elevation renders the shadow from a content-free silhouette (Plate's
    // pattern): layering the whole cover would rasterize the artwork into
    // the effect texture and resample it at fractional positions.
    property int elevation: 0
    property color color: Theme.surfaceHover
    property real glyphScale: 0.45
    property color glyphColor: Theme.text3
    readonly property bool ready: picture.status === Image.Ready
    // pixels is a minimum request bucket. The texture must also cover the
    // item's device-pixel box or PreserveAspectCrop upscales it: at 125% a
    // 400px bucket under a 352px tile still needs 440 device pixels. Snap up
    // to the next stdmusic bucket (the CDN's accepted sizes).
    readonly property int bucket: {
        const target = Math.max(root.pixels, Math.ceil(Math.max(width, height) * Theme.dpr))
        for (const b of [80, 100, 120, 200, 300, 400, 512, 800, 1000])
            if (b >= target) return b
        return 1000
    }

    PixelSnap.enabled: true

    // The rounded artwork is composited through offscreen mask/effect
    // textures, which are sized in device pixels: a fractional item box is
    // rendered at one size and drawn at another, blurring every edge. Frame
    // snaps the box to whole device pixels (PixelSnap already lands the
    // origin on the grid), keeping mask and artwork 1:1.
    Item {
        id: frame
        width: Theme.snap(root.width); height: Theme.snap(root.height)

        Rectangle {
            anchors.fill: parent; radius: root.radius; color: Theme.black
            visible: root.elevation > 0 && Theme.hasShadow(root.elevation)
            layer.enabled: visible
            layer.effect: ShadowEffect { level: root.elevation }
        }
        Rectangle { anchors.fill: parent; radius: root.radius; color: root.color; visible: !root.ready }
        Text {
            anchors.centerIn: parent; visible: !root.ready && root.title !== "" && frame.width >= Theme.fsMd && frame.height >= Theme.fsMd
            text: root.title.slice(0, 1)
            font.family: Theme.fontTitle; font.pixelSize: Theme.fsMd
            font.weight: Theme.fwMedium; color: Theme.text3
            renderType: Text.NativeRendering
        }
        Icon {
            anchors.centerIn: parent; visible: !root.ready && root.title === ""
            name: "disc-3"; color: root.glyphColor; size: Math.round(Math.min(frame.width, frame.height) * root.glyphScale)
        }
        Image {
            id: picture; anchors.fill: parent
            source: Theme.artwork(root.url, root.bucket)
            asynchronous: true; cache: false
            sourceSize.width: root.bucket; sourceSize.height: root.bucket
            fillMode: Image.PreserveAspectCrop
            visible: root.radius <= 0
        }
        Item {
            id: mask; anchors.fill: parent; visible: false; layer.enabled: root.radius > 0
            Rectangle { anchors.fill: parent; radius: root.radius; color: Theme.black }
        }
        MultiEffect {
            anchors.fill: parent; visible: root.radius > 0 && root.ready
            source: picture; maskEnabled: true; maskSource: mask
            maskThresholdMin: 0.5; maskSpreadAtMin: 1.0
        }
    }
}
