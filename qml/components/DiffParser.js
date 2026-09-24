.pragma library

function parse(value) {
    if (!value.length) return []
    let oldNumber = 0
    let newNumber = 0
    let oldRemaining = 0
    let newRemaining = 0
    let currentHunk = -1
    const result = []
    let cursor = 0
    while (cursor < value.length) {
        const newline = value.indexOf("\n", cursor)
        const line = newline < 0 ? value.substring(cursor) : value.substring(cursor, newline)
        cursor = newline < 0 ? value.length : newline + 1
        const hunk = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/.exec(line)
        let row = {kind: "meta", text: line, oldNumber: 0, newNumber: 0, hunkIndex: -1}
        if (hunk) {
            currentHunk++
            oldNumber = Number(hunk[1])
            newNumber = Number(hunk[3])
            oldRemaining = hunk[2] === undefined ? 1 : Number(hunk[2])
            newRemaining = hunk[4] === undefined ? 1 : Number(hunk[4])
            row.kind = "hunk"
            row.hunkIndex = currentHunk
        } else if (line.startsWith("\\ No newline")) {
            row.kind = "marker"
            row.hunkIndex = currentHunk
        } else if (line.startsWith("diff --git")) {
            oldRemaining = 0
            newRemaining = 0
            currentHunk = -1
            row.hunkIndex = -1
        } else if ((oldRemaining > 0 || newRemaining > 0) && line.startsWith("+")) {
            row = {kind: "add", text: line.substring(1), oldNumber: 0, newNumber: newNumber++, hunkIndex: currentHunk}
            --newRemaining
        } else if ((oldRemaining > 0 || newRemaining > 0) && line.startsWith("-")) {
            row = {kind: "remove", text: line.substring(1), oldNumber: oldNumber++, newNumber: 0, hunkIndex: currentHunk}
            --oldRemaining
        } else if ((oldRemaining > 0 || newRemaining > 0) && line.startsWith(" ")) {
            row = {kind: "context", text: line.substring(1), oldNumber: oldNumber++, newNumber: newNumber++, hunkIndex: currentHunk}
            --oldRemaining
            --newRemaining
        }
        row.sourceIndex = result.length
        result.push(row)
    }
    return result
}

function paired(rows) {
    const result = []
    let removed = []
    let added = []
    let leftMarker = null
    let rightMarker = null
    function flush() {
        const maxLen = Math.max(removed.length, added.length)
        for (let i = 0; i < maxLen; ++i) {
            const left = removed[i] || null
            const right = added[i] || null
            const hIdx = left && left.hunkIndex !== undefined ? left.hunkIndex : (right && right.hunkIndex !== undefined ? right.hunkIndex : -1)
            let sIdx = -1
            let maxSIdx = -1
            if (left && left.sourceIndex !== undefined) {
                sIdx = left.sourceIndex
                maxSIdx = left.sourceIndex
            }
            if (right && right.sourceIndex !== undefined) {
                if (sIdx === -1 || right.sourceIndex < sIdx) sIdx = right.sourceIndex
                if (right.sourceIndex > maxSIdx) maxSIdx = right.sourceIndex
            }
            result.push({left: left, right: right, hunkIndex: hIdx, sourceIndex: sIdx, maxSourceIndex: maxSIdx})
        }
        if (leftMarker || rightMarker) {
            const marker = leftMarker || rightMarker
            const hIdx = marker && marker.hunkIndex !== undefined ? marker.hunkIndex : -1
            const sIdx = marker && marker.sourceIndex !== undefined ? marker.sourceIndex : -1
            result.push({left: leftMarker, right: rightMarker, hunkIndex: hIdx, sourceIndex: sIdx, maxSourceIndex: sIdx})
        }
        removed = []
        added = []
        leftMarker = null
        rightMarker = null
    }
    for (const row of rows) {
        if (row.kind === "remove") removed.push(row)
        else if (row.kind === "add") added.push(row)
        else if (row.kind === "marker" && (removed.length || added.length)) {
            // Markers have their own display row, never paired with source code.
            if (added.length) rightMarker = row
            else leftMarker = row
        } else {
            flush()
            result.push({left: row, right: row, hunkIndex: row.hunkIndex !== undefined ? row.hunkIndex : -1, sourceIndex: row.sourceIndex, maxSourceIndex: row.sourceIndex})
        }
    }
    flush()
    return result
}
