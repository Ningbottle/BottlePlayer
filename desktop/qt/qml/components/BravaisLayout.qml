pragma Singleton
import QtQuick

// Native packing on the latest Folia Bravais 128 + 8, 12 × 8 block grid.
// The four measured tile arrangements follow Folia (7701347); see licenses/THIRD_PARTY.md.
// Reflow uses native exact-cover packing.
QtObject {
    readonly property int pitch: 136
    readonly property int gap: 8
    readonly property int columns: 12
    readonly property int rows: 8
    readonly property int slots: 12
    readonly property real blockWidth: columns * pitch
    readonly property real blockHeight: rows * pitch
    readonly property var templates: [
        [[0,0,3,2],[3,0,6,4],[9,0,3,2],[0,2,3,2],[9,2,3,4],[0,4,4,4],
            [4,4,2,2],[6,4,3,2],[4,6,2,2],[6,6,2,2],[8,6,2,2],[10,6,2,2]],
        [[0,0,2,3],[2,0,2,3],[4,0,2,3],[6,0,6,4],[0,3,6,2],[6,4,2,4],
            [8,4,2,4],[10,4,2,2],[0,5,2,3],[2,5,2,3],[4,5,2,3],[10,6,2,2]],
        [[0,0,6,4],[6,0,6,3],[6,3,4,3],[10,3,2,2],[0,4,4,2],[4,4,2,2],
            [10,5,2,3],[0,6,2,2],[2,6,2,2],[4,6,2,2],[6,6,2,2],[8,6,2,2]],
        [[0,0,2,2],[2,0,2,2],[4,0,4,3],[8,0,2,4],[10,0,2,4],[0,2,2,3],
            [2,2,2,2],[4,3,4,3],[2,4,2,2],[8,4,4,4],[0,5,2,3],[2,6,6,2]]
    ]
    function mod(value, size) { return (value % size + size) % size }
    function cells(column, row) {
        var result = templates[mod(column + row * 2, templates.length)]
        var hash = Math.imul(column, 0x9e3779b1) ^ Math.imul(row, 0x85ebca6b)
        hash = Math.imul(hash ^ (hash >>> 15), 0x2c1b3c6d)
        var reflection = ((hash ^ (hash >>> 12)) >>> 0) % 4
        return result.map(function(p) {
            return [reflection % 2 ? 12 - p[0] - p[2] : p[0],
                    reflection >= 2 ? 8 - p[1] - p[3] : p[1], p[2], p[3]]
        })
    }
    function period(count) {
        var blocks = Math.ceil(Math.max(1, count) / slots)
        var cols = blocks <= 1 ? 1 : Math.max(1, Math.round(Math.sqrt(2.2 * blocks * 8 / 12)))
        return { columns: cols, rows: Math.ceil(blocks / cols) }
    }
    function rect(column, row, cell, slot, count) {
        var p = period(count)
        return { key: column + ":" + row + ":" + slot, column: column, row: row, slot: slot,
            address: ((mod(row, p.rows) * p.columns + mod(column, p.columns)) * slots + slot) % Math.max(1, count),
            x: column * blockWidth + cell[0] * pitch, y: row * blockHeight + cell[1] * pitch,
            w: cell[2] * pitch - gap, h: cell[3] * pitch - gap }
    }
    function at(column, row, slot, count) { return rect(column, row, cells(column, row)[slot], slot, count) }
    function expand(picked, count) {
        var source = cells(picked.column, picked.row)
        if (picked.slot < 0 || picked.slot >= slots) return source.map((cell,i) => rect(picked.column,picked.row,cell,i,count))
        var original = source[picked.slot]
        var right = original[0]+original[2]/2 >= 6, bottom = original[1]+original[3]/2 >= 4
        // Native exact cover: assign the nearest remaining centre to larger cells first.
        var targets = [[4,0,2,4],[0,0,4,2],[0,6,4,2],[0,4,3,2],[3,4,3,2],
                       [0,2,2,2],[2,2,2,2],[4,6,2,2],[6,0,2,2],[8,0,2,2],[10,0,2,2]]
        function reflect(c) { return [right?c[0]:12-c[0]-c[2],bottom?c[1]:8-c[1]-c[3],c[2],c[3]] }
        var remaining = [], assigned = []
        for (var i=0;i<slots;i++) if (i!==picked.slot) remaining.push(i)
        assigned[picked.slot] = reflect([6,2,6,6])
        for (var t=0;t<targets.length;t++) {
            var cell=reflect(targets[t]), best=0, distance=Infinity
            for (var j=0;j<remaining.length;j++) {
                var p=source[remaining[j]], dx=p[0]+p[2]/2-cell[0]-cell[2]/2, dy=p[1]+p[3]/2-cell[1]-cell[3]/2
                if (dx*dx+dy*dy<distance) { best=j; distance=dx*dx+dy*dy }
            }
            assigned[remaining.splice(best,1)[0]]=cell
        }
        return assigned.map((cell,i) => rect(picked.column,picked.row,cell,i,count))
    }
}
