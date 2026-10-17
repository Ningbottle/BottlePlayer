import QtQuick
import BottleMusic

// A 1px CSS rule. Chromium paints it floor(dpr) device pixels thick; a plain
// 1-logical-pixel Rectangle at 125% covers one or two device rows depending
// on where it lands, which is what made Qt's rules look soft. Rectangles
// without a radius are not antialiased, so a Theme.hairline-thick quad always
// fills exactly one row.
Rectangle {
    property bool vertical: false
    implicitWidth: vertical ? Theme.hairline : 0
    implicitHeight: vertical ? 0 : Theme.hairline
    color: Theme.border
}
