pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot
import "DiffParser.js" as DiffParser

Item {
    id: root
    clip: true

    property string diffText: ""
    property bool sideBySide: false
    property bool renderingEnabled: true
    property var workspace: null
    property string searchQuery: ""
    property int currentHunkIndex: 0
    property int currentMatchIndex: 0
    readonly property int codeFontSize: Math.max(8, Math.min(22, Theme.codeFontPointSize))

    // Large diff handling & windowing
    readonly property int maxRenderedLines: 2000
    property bool showAllDiff: false
    property int windowStart: 0

    FontMetrics {
        id: monoMetrics
        font.family: "DejaVu Sans Mono"
        font.pointSize: root.codeFontSize || 11
    }

    readonly property int maxLineChars: {
        let maxLen = 40
        const rows = displayParsedRows
        for (let i = 0; i < rows.length; ++i) {
            const t = rows[i].text
            if (t) {
                let len = 0
                for (let c = 0; c < t.length; ++c) {
                    len += (t.charCodeAt(c) === 9) ? 8 : 1
                }
                if (len > maxLen) maxLen = len
            }
        }
        return maxLen
    }

    readonly property real estimatedLineWidth: {
        const charWidth = Math.max(7.5, monoMetrics.averageCharacterWidth)
        return (maxLineChars + 24) * charWidth + 80
    }

    readonly property var parsedRows: renderingEnabled ? DiffParser.parse(diffText) : []
    readonly property var pairedRows: renderingEnabled && sideBySide ? DiffParser.paired(parsedRows) : []
    readonly property int totalLineCount: parsedRows.length
    readonly property bool isTruncated: !showAllDiff && totalLineCount > maxRenderedLines
    readonly property int windowEnd: isTruncated ? Math.min(totalLineCount, windowStart + maxRenderedLines) : totalLineCount

    readonly property var displayParsedRows: {
        if (!isTruncated) return parsedRows
        return parsedRows.slice(windowStart, windowEnd)
    }

    readonly property var displayPairedRows: {
        if (!isTruncated) return pairedRows
        const pTotal = pairedRows.length
        if (pTotal === 0) return []
        let pStart = 0
        while (pStart < pTotal && (pairedRows[pStart].maxSourceIndex !== undefined ? pairedRows[pStart].maxSourceIndex : pairedRows[pStart].sourceIndex) < windowStart) {
            pStart++
        }
        let pEnd = pStart
        while (pEnd < pTotal && pairedRows[pEnd].sourceIndex < windowEnd) {
            pEnd++
        }
        return pairedRows.slice(pStart, pEnd)
    }

    readonly property bool isBinary: {
        if (workspace && workspace.isBinaryDiff) return true
        return diffText.indexOf("Binary files ") >= 0 || diffText.indexOf("GIT binary patch") >= 0
    }
    readonly property bool isWorktreeDiff: workspace && workspace.selectedFile && diffText === workspace.diffText
    readonly property int parsedHunkCount: {
        let count = 0
        for (let i = 0; i < parsedRows.length; ++i) {
            if (parsedRows[i].kind === "hunk") count++
        }
        return count
    }
    readonly property int hunkTotal: workspace && isWorktreeDiff && workspace.hunkCount !== undefined && Number(workspace.hunkCount) > 0
        ? Number(workspace.hunkCount)
        : parsedHunkCount

    readonly property int searchMatchCount: {
        if (!searchQuery.trim() || !diffText) return 0
        const query = searchQuery.trim().toLowerCase()
        let count = 0
        let pos = 0
        const lower = diffText.toLowerCase()
        while ((pos = lower.indexOf(query, pos)) !== -1) {
            count++
            pos += query.length
        }
        return count
    }

    onHunkTotalChanged: {
        if (currentHunkIndex >= hunkTotal)
            currentHunkIndex = Math.max(0, hunkTotal - 1)
    }

    onDiffTextChanged: {
        currentHunkIndex = 0
        windowStart = 0
        showAllDiff = false
        searchQuery = ""
        currentMatchIndex = 0
        searchDebounceTimer.stop()
        if (diffScroll.contentItem) {
            diffScroll.contentItem.contentY = 0
            diffScroll.contentItem.contentX = 0
        }
    }

    onCurrentHunkIndexChanged: {
        ensureHunkInWindow(currentHunkIndex)
        Qt.callLater(scrollToCurrentHunk)
    }

    onSideBySideChanged: {
        Qt.callLater(scrollToCurrentHunk)
    }

    Timer {
        id: searchDebounceTimer
        interval: 200
        repeat: false
        onTriggered: {
            root.searchQuery = searchInput.text.trim()
            root.currentMatchIndex = 0
            if (root.searchQuery.length > 0)
                root.scrollToSearchMatch(0)
        }
    }

    function escaped(value) {
        return value.replace(/&/g, "&amp;")
                    .replace(/</g, "&lt;")
                    .replace(/>/g, "&gt;")
    }

    function highlightSearch(text, query) {
        if (!query || !text) return escaped(text)
        const lowerText = text.toLowerCase()
        const lowerQuery = query.toLowerCase()
        let lastPos = 0
        let pos = 0
        let result = ""
        while ((pos = lowerText.indexOf(lowerQuery, lastPos)) !== -1) {
            if (pos > lastPos) {
                result += escaped(text.substring(lastPos, pos))
            }
            result += "<span style='background-color:#FFEBAA;color:#1C1C1E;font-weight:bold'>"
                    + escaped(text.substring(pos, pos + query.length))
                    + "</span>"
            lastPos = pos + query.length
        }
        if (lastPos < text.length) {
            result += escaped(text.substring(lastPos))
        }
        return result
    }

    function paddedLine(value) {
        return value > 0 ? String(value).padStart(5, " ") : "     "
    }

    function rowHtml(row, oldSide, unified, parentHunkIndex, query) {
        const hunkIdx = row ? (row.hunkIndex !== undefined ? row.hunkIndex : -1)
                            : (parentHunkIndex !== undefined ? parentHunkIndex : -1)
        const isActiveHunk = root.hunkTotal > 0 && hunkIdx === root.currentHunkIndex && hunkIdx >= 0

        if (!row) {
            return isActiveHunk ? "<span style='background-color:#F8FAFC'> </span><br>" : "<br>"
        }

        let color = "#3A3A3C"
        let background = "transparent"
        let prefix = " "
        let isHunkHeader = false

        if (row.kind === "add") {
            color = isActiveHunk ? "#0B6634" : "#187A43"
            background = isActiveHunk ? "#D2F4E0" : "#E5F7ED"
            prefix = "+"
        } else if (row.kind === "remove") {
            color = isActiveHunk ? "#AB231C" : "#C4322A"
            background = isActiveHunk ? "#FFD5D1" : "#FFE8E6"
            prefix = "−"
        } else if (row.kind === "hunk") {
            isHunkHeader = true
            color = isActiveHunk ? "#FFFFFF" : "#0868C8"
            background = isActiveHunk ? "#0868C8" : "#E3F1FF"
            prefix = "@"
        } else if (row.kind === "marker") {
            color = isActiveHunk ? "#AB231C" : "#C4322A"
            background = isActiveHunk ? "#FFD5D1" : "#FFE8E6"
            prefix = " "
        } else if (row.kind === "context") {
            color = "#1C1C1E"
            background = isActiveHunk ? "#F2F5F8" : "transparent"
            prefix = " "
        } else {
            color = "#6E6E73"
            background = "transparent"
            prefix = " "
        }

        const numbers = unified ? paddedLine(row.oldNumber) + " " + paddedLine(row.newNumber)
                                : paddedLine(oldSide ? row.oldNumber : row.newNumber)

        const activeQuery = (query !== undefined ? query : root.searchQuery)
        let textFormatted = activeQuery ? highlightSearch(row.text, activeQuery) : escaped(row.text)

        if (isHunkHeader && row.hunkIndex >= 0) {
            const hunkBadge = isActiveHunk
                ? "<b>[当前代码块 #" + (row.hunkIndex + 1) + "]</b> "
                : "[代码块 #" + (row.hunkIndex + 1) + "] "
            textFormatted = hunkBadge + textFormatted
        }

        const numColor = isHunkHeader && isActiveHunk ? "#D0E8FF" : "#8E8E93"
        const numBg = isHunkHeader && isActiveHunk ? "#0868C8" : (isActiveHunk && background !== "transparent" ? background : "transparent")

        return "<span style='color:" + numColor + ";background-color:" + numBg + "'>" + numbers
             + "</span><span style='color:" + color + ";background-color:" + background + (isHunkHeader && isActiveHunk ? ";font-weight:bold" : "") + "'> "
             + prefix + " " + textFormatted + "</span><br>"
    }

    function renderedDiff(oldSide, unified, query, fontSize) {
        const rows = unified ? root.displayParsedRows : root.displayPairedRows
        if (!rows || !rows.length)
            return "<span style='color:#8E8E93'>当前没有可显示的代码 Diff</span>"
        const effectiveQuery = (query !== undefined ? query : root.searchQuery)
        const size = (fontSize || root.codeFontSize || 11)
        let html = "<pre style='font-family:DejaVu Sans Mono;font-size:" + size + "pt;margin:0'>"
        if (unified) {
            for (const row of rows) html += rowHtml(row, false, true, row.hunkIndex, effectiveQuery)
        } else {
            for (const pair of rows) html += rowHtml(oldSide ? pair.left : pair.right, oldSide, false, pair.hunkIndex, effectiveQuery)
        }
        return html + "</pre>"
    }

    function ensureHunkInWindow(hunkIndex) {
        if (root.showAllDiff || root.parsedRows.length <= root.maxRenderedLines) return
        if (hunkIndex < 0 || root.hunkTotal <= 0) return

        let hunkRow = -1
        for (let i = 0; i < root.parsedRows.length; ++i) {
            if (root.parsedRows[i].kind === "hunk" && root.parsedRows[i].hunkIndex === hunkIndex) {
                hunkRow = i
                break
            }
        }
        if (hunkRow < 0) return

        if (hunkRow < root.windowStart || hunkRow >= root.windowEnd) {
            const halfWindow = Math.floor(root.maxRenderedLines / 2)
            let newStart = Math.max(0, hunkRow - halfWindow)
            let newEnd = Math.min(root.parsedRows.length, newStart + root.maxRenderedLines)
            if (newEnd === root.parsedRows.length) {
                newStart = Math.max(0, root.parsedRows.length - root.maxRenderedLines)
            }
            root.windowStart = newStart
        }
    }

    function scrollToCurrentHunk() {
        if (root.hunkTotal <= 0 || root.currentHunkIndex < 0) return
        const isSplit = root.sideBySide
        const rows = isSplit ? root.displayPairedRows : root.displayParsedRows
        if (!rows || rows.length === 0) return

        let targetRow = -1
        for (let i = 0; i < rows.length; ++i) {
            const item = rows[i]
            const hIdx = item.hunkIndex !== undefined ? item.hunkIndex : -1
            const isHunkHeader = isSplit ? ((item.left && item.left.kind === "hunk") || (item.right && item.right.kind === "hunk"))
                                         : (item.kind === "hunk")
            if (hIdx === root.currentHunkIndex && isHunkHeader) {
                targetRow = i
                break
            }
        }
        if (targetRow < 0) {
            for (let i = 0; i < rows.length; ++i) {
                if (rows[i].hunkIndex === root.currentHunkIndex) {
                    targetRow = i
                    break
                }
            }
        }
        if (targetRow < 0) return

        const fraction = targetRow / rows.length
        if (diffScroll.contentItem && diffScroll.contentItem.contentHeight > 0) {
            const cHeight = diffScroll.contentItem.contentHeight
            const vHeight = diffScroll.height
            const targetY = fraction * cHeight - (vHeight / 2)
            const maxY = Math.max(0, cHeight - vHeight)
            diffScroll.contentItem.contentY = Math.max(0, Math.min(targetY, maxY))
        }
        if (diffScroll.ScrollBar && diffScroll.ScrollBar.vertical) {
            const vbar = diffScroll.ScrollBar.vertical
            const targetPos = Math.max(0, Math.min(fraction - (vbar.size / 2), 1.0 - vbar.size))
            vbar.position = targetPos
        }
    }

    function nextSearchMatch() {
        if (searchMatchCount <= 0) return
        currentMatchIndex = (currentMatchIndex + 1) % searchMatchCount
        scrollToSearchMatch(currentMatchIndex)
    }

    function prevSearchMatch() {
        if (searchMatchCount <= 0) return
        currentMatchIndex = (currentMatchIndex - 1 + searchMatchCount) % searchMatchCount
        scrollToSearchMatch(currentMatchIndex)
    }

    function scrollToSearchMatch(matchIdx) {
        if (!root.searchQuery || searchMatchCount <= 0) return
        const query = root.searchQuery.toLowerCase()
        let matchCounter = 0
        let targetParsedRow = -1

        for (let i = 0; i < root.parsedRows.length; ++i) {
            const rowText = root.parsedRows[i].text.toLowerCase()
            let pos = 0
            while ((pos = rowText.indexOf(query, pos)) !== -1) {
                if (matchCounter === matchIdx) {
                    targetParsedRow = i
                    break
                }
                matchCounter++
                pos += query.length
            }
            if (targetParsedRow >= 0) break
        }

        if (targetParsedRow < 0) return

        if (root.isTruncated && (targetParsedRow < root.windowStart || targetParsedRow >= root.windowEnd)) {
            const halfWindow = Math.floor(root.maxRenderedLines / 2)
            let newStart = Math.max(0, targetParsedRow - halfWindow)
            let newEnd = Math.min(root.totalLineCount, newStart + root.maxRenderedLines)
            if (newEnd === root.totalLineCount) {
                newStart = Math.max(0, root.totalLineCount - root.maxRenderedLines)
            }
            root.windowStart = newStart
        }

        Qt.callLater(function() {
            const rows = root.sideBySide ? root.displayPairedRows : root.displayParsedRows
            if (!rows || rows.length === 0) return
            let rowInDisplay = -1
            if (!root.sideBySide) {
                rowInDisplay = targetParsedRow - (root.isTruncated ? root.windowStart : 0)
            } else {
                const fraction = targetParsedRow / Math.max(1, root.totalLineCount)
                rowInDisplay = Math.floor(fraction * rows.length)
            }
            if (rowInDisplay >= 0 && rowInDisplay < rows.length) {
                const fraction = rowInDisplay / rows.length
                if (diffScroll.contentItem && diffScroll.contentItem.contentHeight > 0) {
                    const cHeight = diffScroll.contentItem.contentHeight
                    const vHeight = diffScroll.height
                    const targetY = fraction * cHeight - (vHeight / 2)
                    const maxY = Math.max(0, cHeight - vHeight)
                    diffScroll.contentItem.contentY = Math.max(0, Math.min(targetY, maxY))
                }
            }
        })
    }

    function prevWindowPage() {
        windowStart = Math.max(0, windowStart - maxRenderedLines)
        if (diffScroll.contentItem) diffScroll.contentItem.contentY = 0
    }

    function nextWindowPage() {
        windowStart = Math.min(Math.max(0, totalLineCount - maxRenderedLines), windowStart + maxRenderedLines)
        if (diffScroll.contentItem) diffScroll.contentItem.contentY = 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Wrap actions instead of hiding them in a horizontally clipped toolbar.
        Item {
            id: toolbarFlickable
            objectName: "toolbarFlickable"
            Layout.fillWidth: true
            Layout.preferredHeight: toolbarRow.implicitHeight

            Flow {
                id: toolbarRow
                width: parent.width
                spacing: 8

                // Search Bar
                Rectangle {
                    width: Math.min(260, toolbarRow.width)
                    height: Theme.controlHeight
                    radius: 7
                    color: Theme.surfaceMuted
                    border.color: searchInput.activeFocus ? Theme.accent : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 6
                        spacing: 4

                        Text {
                            text: "🔍"
                            font.pixelSize: Theme.fontSecondary
                            color: Theme.tertiaryText
                        }

                        TextInput {
                            id: searchInput
                            Layout.fillWidth: true
                            font.pixelSize: Theme.fontSecondary
                            color: Theme.text
                            clip: true
                            selectByMouse: true
                            onTextChanged: {
                                if (text.trim() === "") {
                                    searchDebounceTimer.stop()
                                    root.searchQuery = ""
                                    root.currentMatchIndex = 0
                                } else {
                                    searchDebounceTimer.restart()
                                }
                            }
                            onAccepted: {
                                searchDebounceTimer.stop()
                                if (root.searchQuery === text.trim() && root.searchMatchCount > 0) {
                                    root.nextSearchMatch()
                                } else {
                                    root.searchQuery = text.trim()
                                    root.currentMatchIndex = 0
                                    if (root.searchQuery.length > 0)
                                        root.scrollToSearchMatch(0)
                                }
                            }

                            Text {
                                anchors.fill: parent
                                text: "搜索差异…"
                                color: Theme.placeholder
                                font.pixelSize: Theme.fontSecondary
                                visible: !searchInput.text.length && !searchInput.activeFocus
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Text {
                            text: root.searchQuery.trim().length > 0
                                ? (root.searchMatchCount > 0 ? (root.currentMatchIndex + 1) + "/" + root.searchMatchCount : "无匹配")
                                : ""
                            color: root.searchMatchCount > 0 ? Theme.accent : Theme.red
                            font.pixelSize: Theme.fontSecondary
                            visible: root.searchQuery.trim().length > 0
                        }

                        ToolButton {
                            visible: root.searchMatchCount > 1
                            text: "▲"
                            implicitWidth: 18
                            implicitHeight: 22
                            focusPolicy: Qt.NoFocus
                            onClicked: root.prevSearchMatch()
                        }

                        ToolButton {
                            visible: root.searchMatchCount > 1
                            text: "▼"
                            implicitWidth: 18
                            implicitHeight: 22
                            focusPolicy: Qt.NoFocus
                            onClicked: root.nextSearchMatch()
                        }

                        IconButton {
                            visible: searchInput.text.length > 0
                            glyph: "✕"
                            toolTip: "清除搜索"
                            onClicked: {
                                searchInput.text = ""
                                searchDebounceTimer.stop()
                                root.searchQuery = ""
                                root.currentMatchIndex = 0
                            }
                        }
                    }
                }

                // Each operation participates in wrapping, even at minimum pane width.
                Flow {
                    width: toolbarRow.width
                    visible: root.hunkTotal > 0
                    spacing: 6

                    // Flow children use actual dimensions, not Layout attached hints.

                    ToolButton {
                        id: prevHunkBtn
                        text: "◀"
                        implicitWidth: 26
                        implicitHeight: Theme.controlHeight
                        padding: 0
                        topInset: 0
                        bottomInset: 0
                        leftInset: 0
                        rightInset: 0
                        hoverEnabled: true
                        enabled: root.currentHunkIndex > 0
                        background: Rectangle {
                            radius: 6
                            color: prevHunkBtn.down ? "#DEE0E8" : prevHunkBtn.hovered ? Theme.surfaceMuted : "transparent"
                        }
                        contentItem: Text {
                            text: prevHunkBtn.text
                            color: prevHunkBtn.enabled ? Theme.text : Theme.tertiaryText
                            font.pixelSize: Theme.fontSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.NoButton
                        }
                        onClicked: root.currentHunkIndex = Math.max(0, root.currentHunkIndex - 1)
                    }

                    Text {
                        text: "块 " + (root.currentHunkIndex + 1) + " / " + root.hunkTotal
                        height: Theme.controlHeight
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fontSecondary
                        color: Theme.secondaryText
                    }

                    ToolButton {
                        id: nextHunkBtn
                        text: "▶"
                        implicitWidth: 26
                        implicitHeight: Theme.controlHeight
                        padding: 0
                        topInset: 0
                        bottomInset: 0
                        leftInset: 0
                        rightInset: 0
                        hoverEnabled: true
                        enabled: root.currentHunkIndex < root.hunkTotal - 1
                        background: Rectangle {
                            radius: 6
                            color: nextHunkBtn.down ? "#DEE0E8" : nextHunkBtn.hovered ? Theme.surfaceMuted : "transparent"
                        }
                        contentItem: Text {
                            text: nextHunkBtn.text
                            color: nextHunkBtn.enabled ? Theme.text : Theme.tertiaryText
                            font.pixelSize: Theme.fontSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.NoButton
                        }
                        onClicked: root.currentHunkIndex = Math.min(root.hunkTotal - 1, root.currentHunkIndex + 1)
                    }

                    FileActionButton {
                        implicitWidth: Theme.controlHeight
                        implicitHeight: Theme.controlHeight
                        visible: root.workspace && !root.workspace.selectedFileStaged && root.isWorktreeDiff
                        text: "＋"
                        toolTip: root.hunkTotal > 1 ? "暂存此块" : "暂存文件"
                        enabled: root.workspace && !root.workspace.busy
                        onClicked: {
                            if (root.hunkTotal > 1) {
                                root.workspace.stageHunk(root.currentHunkIndex)
                            } else {
                                root.workspace.stageFiles([root.workspace.selectedFile])
                            }
                        }
                    }

                    FileActionButton {
                        implicitWidth: Theme.controlHeight
                        implicitHeight: Theme.controlHeight
                        visible: root.workspace && !root.workspace.selectedFileStaged && root.isWorktreeDiff && root.hunkTotal > 1
                        objectName: "stageWholeFileButton"
                        text: "⊞"
                        toolTip: "暂存整个文件"

                        enabled: root.workspace && !root.workspace.busy
                        onClicked: root.workspace.stageFiles([root.workspace.selectedFile])
                    }

                    FileActionButton {
                        implicitWidth: Theme.controlHeight
                        implicitHeight: Theme.controlHeight
                        visible: root.workspace && root.workspace.selectedFileStaged && root.isWorktreeDiff
                        text: "−"
                        toolTip: root.hunkTotal > 1 ? "取消暂存此块" : "取消暂存文件"
                        enabled: root.workspace && !root.workspace.busy
                        onClicked: {
                            if (root.hunkTotal > 1) {
                                root.workspace.unstageHunk(root.currentHunkIndex)
                            } else {
                                root.workspace.unstageFiles([root.workspace.selectedFile])
                            }
                        }
                    }

                    FileActionButton {
                        implicitWidth: Theme.controlHeight
                        implicitHeight: Theme.controlHeight
                        visible: root.workspace && root.workspace.selectedFileStaged && root.isWorktreeDiff && root.hunkTotal > 1
                        text: "⊟"
                        toolTip: "取消暂存整个文件"

                        enabled: root.workspace && !root.workspace.busy
                        onClicked: root.workspace.unstageFiles([root.workspace.selectedFile])
                    }

                    FileActionButton {
                        implicitWidth: Theme.controlHeight
                        implicitHeight: Theme.controlHeight
                        visible: root.workspace && !root.workspace.selectedFileStaged && root.isWorktreeDiff
                        text: "↶"
                        toolTip: root.hunkTotal > 1 ? "丢弃此块" : "丢弃改动"


                        enabled: root.workspace && !root.workspace.busy
                        onClicked: {
                            if (root.hunkTotal > 1) {
                                discardHunkDialog.open()
                            } else {
                                root.workspace.discardFiles([root.workspace.selectedFile])
                            }
                        }
                    }
                }

            }
        }

        // Large Diff Notification Banner (Windowed mode)
        Rectangle {
            visible: root.isTruncated
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            radius: 8
            color: "#FFF9E6"
            border.color: "#FFE082"
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    text: "⚠️ Diff 较大（共 " + root.totalLineCount + " 行），当前窗口展示第 " + (root.windowStart + 1) + " ~ " + root.windowEnd + " 行"
                    color: "#7A5E0B"
                    font.pixelSize: Theme.fontSecondary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                ToolButton {
                    id: prevPageBtn
                    text: "◀ 上一页"
                    implicitHeight: 28
                    implicitWidth: prevPageText.implicitWidth + 16
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    enabled: root.windowStart > 0
                    focusPolicy: Qt.NoFocus
                    background: Rectangle {
                        radius: 6
                        color: prevPageBtn.down ? "#FFE082" : prevPageBtn.hovered ? "#FFF0B8" : "transparent"
                    }
                    contentItem: Text {
                        id: prevPageText
                        text: prevPageBtn.text
                        color: prevPageBtn.enabled ? "#7A5E0B" : "#BBA86B"
                        font.pixelSize: Theme.fontSecondary
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.NoButton
                    }
                    onClicked: root.prevWindowPage()
                }

                ToolButton {
                    id: nextPageBtn
                    text: "下一页 ▶"
                    implicitHeight: 28
                    implicitWidth: nextPageText.implicitWidth + 16
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    enabled: root.windowEnd < root.totalLineCount
                    focusPolicy: Qt.NoFocus
                    background: Rectangle {
                        radius: 6
                        color: nextPageBtn.down ? "#FFE082" : nextPageBtn.hovered ? "#FFF0B8" : "transparent"
                    }
                    contentItem: Text {
                        id: nextPageText
                        text: nextPageBtn.text
                        color: nextPageBtn.enabled ? "#7A5E0B" : "#BBA86B"
                        font.pixelSize: Theme.fontSecondary
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.NoButton
                    }
                    onClicked: root.nextWindowPage()
                }

                PrimaryButton {
                    text: "加载全部"
                    onClicked: root.showAllDiff = true
                }

                PrimaryButton {
                    visible: root.workspace && root.workspace.selectedFile && root.workspace.selectedFile.length > 0
                    text: "系统程序打开"
                    secondary: true
                    onClicked: root.workspace.openFile(root.workspace.selectedFile)
                }
            }
        }

        // Full Diff Loaded Banner (when user clicked "加载全部")
        Rectangle {
            visible: root.totalLineCount > root.maxRenderedLines && root.showAllDiff
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            radius: 8
            color: "#EBF3FB"
            border.color: "#BBD7F0"
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    text: "ℹ️ 已加载完整差异（共 " + root.totalLineCount + " 行）"
                    color: "#185A9D"
                    font.pixelSize: Theme.fontSecondary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                PrimaryButton {
                    text: "恢复窗口化"
                    secondary: true
                    onClicked: root.showAllDiff = false
                }
            }
        }

        // Binary File Notice Card
        Rectangle {
            visible: root.isBinary
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            radius: 9
            color: Theme.surfaceMuted
            border.color: Theme.separatorSoft
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Text {
                    text: "📦"
                    font.pixelSize: 24
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: "二进制文件，无法显示文本差异"
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: root.workspace && root.workspace.selectedFile
                            ? root.workspace.selectedFile
                            : "此文件为二进制文件或不可显示的格式"
                        color: Theme.secondaryText
                        font.pixelSize: Theme.fontSecondary
                        elide: Text.ElideMiddle
                    }
                }

                PrimaryButton {
                    visible: root.workspace && root.workspace.selectedFile.length > 0
                    text: "在系统默认程序中打开"
                    onClicked: root.workspace.openFile(root.workspace.selectedFile)
                }
            }
        }

        // Diff content area
        ScrollView {
            id: diffScroll
            objectName: "diffScroll"
            implicitWidth: 0
            implicitHeight: 0
            padding: 0
            rightPadding: 8
            bottomPadding: 12
            visible: !root.isBinary
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: diffLoader.width
            contentHeight: diffLoader.height

            ScrollBar.vertical: RightScrollBar { parent: diffScroll; z: 10; anchors.bottomMargin: 12 }
            ScrollBar.horizontal: ScrollBar {
                id: hScrollBar
                objectName: "diffHorizontalBar"
                parent: diffScroll
                z: 10
                orientation: Qt.Horizontal
                height: 12
                minimumSize: 0.05
                policy: ScrollBar.AsNeeded
                implicitHeight: 8
                hoverEnabled: true
                padding: 2
                background: Item { }
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.bottom: parent.bottom
                contentItem: Rectangle {
                    implicitHeight: 4
                    radius: height / 2
                    color: hScrollBar.pressed ? Theme.scrollThumbActive : Theme.scrollThumb
                    opacity: hScrollBar.size >= 1 ? 0 : hScrollBar.active || hScrollBar.hovered || hScrollBar.pressed ? 0.85 : 0.55
                }
            }

            Loader {
                id: diffLoader
                width: item ? item.implicitWidth : diffScroll.width
                height: item ? item.implicitHeight : diffScroll.height
                active: root.renderingEnabled && !root.isBinary
                sourceComponent: root.sideBySide ? sideBySideComponent : unifiedComponent
            }

            MouseArea {
                parent: diffScroll
                anchors.fill: parent
                anchors.rightMargin: 8
                anchors.bottomMargin: 12
                z: 5
                acceptedButtons: Qt.MiddleButton
                preventStealing: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                property real startX: 0
                property real startY: 0
                property real pressX: 0
                property real pressY: 0
                onPressed: mouse => {
                    pressX = mouse.x
                    pressY = mouse.y
                    startX = diffScroll.contentItem.contentX
                    startY = diffScroll.contentItem.contentY
                }
                onPositionChanged: mouse => { if (!pressed) return
                    diffScroll.contentItem.contentX = Math.max(0, Math.min(
                        diffScroll.contentWidth - diffScroll.availableWidth, startX + pressX - mouse.x))
                    diffScroll.contentItem.contentY = Math.max(0, Math.min(
                        diffScroll.contentHeight - diffScroll.availableHeight, startY + pressY - mouse.y))
                }
                onWheel: wheel => {
                    const dx = wheel.angleDelta.x !== 0 ? wheel.angleDelta.x
                        : (wheel.modifiers & Qt.ShiftModifier) ? wheel.angleDelta.y : 0
                    if (dx !== 0 && diffScroll.contentWidth > diffScroll.availableWidth) {
                        diffScroll.contentItem.contentX = Math.max(0, Math.min(
                            diffScroll.contentWidth - diffScroll.availableWidth,
                            diffScroll.contentItem.contentX - dx))
                        wheel.accepted = true
                    } else {
                        wheel.accepted = false
                    }
                }
            }
        }
    }

    Component {
        id: unifiedComponent
        TextArea {
            id: unifiedTextArea
            objectName: "unifiedDiffText"
            readonly property real neededWidth: Math.max(diffScroll.availableWidth, contentWidth + leftPadding + rightPadding)
            implicitWidth: neededWidth
            width: neededWidth
            implicitHeight: Math.max(diffScroll.availableHeight, contentHeight + topPadding + bottomPadding)
            height: implicitHeight
            readOnly: true
            textFormat: TextEdit.RichText
            text: root.renderedDiff(false, true, root.searchQuery, root.codeFontSize)
            color: Theme.text
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            padding: 12
            background: Rectangle { color: Theme.surfaceStrong }
        }
    }

    Component {
        id: sideBySideComponent
        Item {
            id: sbsRoot
            readonly property real halfAvail: Math.max(0, (diffScroll.availableWidth - 8) / 2)
            readonly property real colWidth: Math.max(halfAvail, oldDiff.contentWidth + 24, newDiff.contentWidth + 24)
            implicitWidth: Math.max(diffScroll.availableWidth, colWidth * 2 + 8)
            implicitHeight: Math.max(diffScroll.availableHeight, Math.max(oldDiff.contentHeight, newDiff.contentHeight) + 24)
            width: implicitWidth
            height: implicitHeight

            Row {
                anchors.fill: parent
                spacing: 8
                TextArea {
                    id: oldDiff
                    objectName: "oldDiffText"
                    width: sbsRoot.colWidth
                    height: parent.height
                    readOnly: true
                    textFormat: TextEdit.RichText
                    text: root.renderedDiff(true, false, root.searchQuery, root.codeFontSize)
                    color: Theme.text
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    padding: 12
                    background: Rectangle { color: Theme.surfaceStrong }
                }
                TextArea {
                    id: newDiff
                    objectName: "newDiffText"
                    width: sbsRoot.colWidth
                    height: parent.height
                    readOnly: true
                    textFormat: TextEdit.RichText
                    text: root.renderedDiff(false, false, root.searchQuery, root.codeFontSize)
                    color: Theme.text
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    padding: 12
                    background: Rectangle { color: Theme.surfaceStrong }
                }
            }
        }
    }

    AppDialog {
        id: discardHunkDialog
        title: "丢弃改动块"
        preferredWidth: 380
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace)
                root.workspace.discardHunk(root.currentHunkIndex)
        }
        contentItem: Text {
            text: "确定要丢弃当前选中的代码块 #" + (root.currentHunkIndex + 1) + " 吗？\n丢弃后此代码块的工作区修改将永久丢失。"
            color: Theme.red
            font.pixelSize: Theme.fontSecondary
            wrapMode: Text.Wrap
        }
    }
}
