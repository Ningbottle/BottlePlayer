import QtQuick
import QtQuick.Shapes
import BottleMusic

// The press plate: a --paper-2 sheet inside a 1px --rule hairline, with an
// optional dashed inner frame inset 5px — the front page's headline box.
// Not a card: the fill is darker paper, the rule is translucent ink, and the
// dashed mark is what stops the box reading as a UI rectangle.
Item {
    id: root
    property color fill: Theme.surface
    // innerRule draws the dashed plate mark the hero carries.
    property bool innerRule: false

    Rectangle {
        anchors.fill: parent
        color: root.fill
        border.pixelAligned: false; antialiasing: true; border.width: Theme.hairline; border.color: Theme.border
        PixelSnap.fit: true
    }
    Shape {
        id: plate
        anchors.fill: parent; anchors.margins: Theme.snap(5)
        visible: root.innerRule
        ShapePath {
            strokeWidth: Theme.hairline
            strokeColor: Theme.border
            strokeStyle: ShapePath.DashLine
            dashPattern: [5, 4]
            capStyle: ShapePath.FlatCap
            fillColor: "transparent"
            PathRectangle { x: 0; y: 0; width: plate.width; height: plate.height }
        }
    }
}
