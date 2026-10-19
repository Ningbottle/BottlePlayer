import QtQuick
import QtQuick.Shapes
import BottleMusic

// The press-list dotted separator: 1px --rule-soft dots, like the dotted
// leaders between the paper's entries. A Shape dash renders round dots at
// any pitch; the hairline Rectangle can only make solid rules.
Shape {
    id: root
    property color ink: Theme.borderSoft
    implicitHeight: Theme.hairline + 0.5
    ShapePath {
        strokeWidth: Theme.hairline
        strokeColor: root.ink
        strokeStyle: ShapePath.DashLine
        // Round caps turn a near-zero dash into a dot of strokeWidth diameter.
        dashPattern: [0.2, 3.4]
        capStyle: ShapePath.RoundCap
        fillColor: "transparent"
        PathMove { x: 0; y: root.height / 2 }
        PathLine { x: root.width; y: root.height / 2 }
    }
}
