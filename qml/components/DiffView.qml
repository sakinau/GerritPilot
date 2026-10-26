pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
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
    readonly property int fullRenderWarningLines: 20000

    function requestFullDiff() {
        if (totalLineCount > fullRenderWarningLines) fullRenderConfirm.open()
        else showAllDiff = true
    }

    AppDialog {
        id: fullRenderConfirm
        objectName: "fullDiffRenderConfirmation"
        title: "加载超长 Diff"
        preferredWidth: 460
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.showAllDiff = true
        contentItem: Label {
            text: "将一次显示 " + root.totalLineCount + " 行，可能占用较多内存并造成卡顿。\n取消后仍可通过分页、搜索查看全部改动。"
            wrapMode: Text.Wrap
            lineHeight: 1.15
            font.pixelSize: Theme.fontBody
            color: Theme.text
        }
    }

    readonly property var parsedRows: renderingEnabled ? DiffParser.parse(diffText) : []
    readonly property var pairedRows: renderingEnabled && sideBySide ? DiffParser.paired(parsedRows) : []
    // Page in display order. Paired rows are not monotonic in sourceIndex:
    // a long removal block may pair with additions thousands of source rows later.
    readonly property var navigationRows: sideBySide ? pairedRows : parsedRows
    readonly property int totalLineCount: navigationRows.length
    readonly property bool isTruncated: !showAllDiff && totalLineCount > maxRenderedLines
    readonly property int windowEnd: isTruncated ? Math.min(totalLineCount, windowStart + maxRenderedLines) : totalLineCount

    readonly property var displayParsedRows: {
        if (!isTruncated) return parsedRows
        return parsedRows.slice(windowStart, windowEnd)
    }

    readonly property var displayPairedRows: {
        if (!isTruncated) return pairedRows
        return pairedRows.slice(windowStart, windowEnd)
    }

    readonly property bool isBinary: {
        if (workspace && isWorktreeDiff && workspace.isBinaryDiff) return true
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

    readonly property var searchIndex: renderingEnabled && searchQuery.trim().length
        ? DiffParser.searchIndex(parsedRows, searchQuery) : ({count: 0, rows: []})
    readonly property int searchMatchCount: searchIndex.count

    onHunkTotalChanged: {
        if (currentHunkIndex >= hunkTotal)
            currentHunkIndex = Math.max(0, hunkTotal - 1)
    }

    onDiffTextChanged: {
        fullRenderConfirm.close()
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
        windowStart = 0
        ensureHunkInWindow(currentHunkIndex)
        Qt.callLater(scrollToCurrentHunk)
    }

    onRenderingEnabledChanged: {
        if (!renderingEnabled) fullRenderConfirm.close()
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
            result += "<span style='background-color:" + Theme.diffSearchHighlight + ";color:" + Theme.diffContextText + ";font-weight:bold'>"
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
            return isActiveHunk ? "<span style='background-color:" + Theme.diffContextActive + "'> </span>" : " "
        }

        let color = Theme.diffContextText
        let background = "transparent"
        let prefix = " "
        let isHunkHeader = false

        if (row.kind === "add") {
            color = Theme.diffAddText
            background = isActiveHunk ? Theme.diffAddBackgroundActive : Theme.diffAddBackground
            prefix = "+"
        } else if (row.kind === "remove" || row.kind === "marker") {
            color = Theme.diffRemoveText
            background = isActiveHunk ? Theme.diffRemoveBackgroundActive : Theme.diffRemoveBackground
            prefix = row.kind === "remove" ? "−" : " "
        } else if (row.kind === "hunk") {
            isHunkHeader = true
            color = isActiveHunk ? "#FFFFFF" : Theme.diffHunkText
            background = isActiveHunk ? Theme.diffHunkActiveBackground : Theme.diffHunkBackground
            prefix = "@"
        } else if (row.kind === "context") {
            background = isActiveHunk ? Theme.diffContextActive : "transparent"
        } else {
            color = Theme.diffMetaText
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

        const numColor = isHunkHeader && isActiveHunk ? Theme.diffHunkActiveNumber : Theme.diffLineNumber
        const numBg = isHunkHeader && isActiveHunk ? Theme.diffHunkActiveBackground : (isActiveHunk && background !== "transparent" ? background : "transparent")

        return "<span style='color:" + numColor + ";background-color:" + numBg + "'>" + numbers
             + "</span><span style='color:" + color + ";background-color:" + background + (isHunkHeader && isActiveHunk ? ";font-weight:bold" : "") + "'> "
             + prefix + " " + textFormatted + "</span>"
    }

    function renderedDiff(oldSide, unified, query, fontSize) {
        const rows = unified ? root.displayParsedRows : root.displayPairedRows
        if (!rows || !rows.length)
            return "<span style='color:" + Theme.diffLineNumber + "'>当前没有可显示的代码 Diff</span>"
        const effectiveQuery = (query !== undefined ? query : root.searchQuery)
        const size = (fontSize || root.codeFontSize || 11)
        // Each display row must be a separate QTextDocument block. Thousands
        // of <br> lines in one <pre> become a single oversized scene-graph text
        // block, which can disappear when its middle enters the viewport.
        const blockStart = "<pre style='font-family:DejaVu Sans Mono;font-size:" + size + "pt;margin:0'>"
        const blocks = []
        if (unified) {
            for (const row of rows)
                blocks.push(blockStart + rowHtml(row, false, true, row.hunkIndex, effectiveQuery) + "</pre>")
        } else {
            for (const pair of rows)
                blocks.push(blockStart + rowHtml(oldSide ? pair.left : pair.right, oldSide, false, pair.hunkIndex, effectiveQuery) + "</pre>")
        }
        return blocks.join("")
    }

    function ensureHunkInWindow(hunkIndex) {
        if (!root.isTruncated) return
        if (hunkIndex < 0 || root.hunkTotal <= 0) return

        let hunkRow = -1
        const rows = root.navigationRows
        for (let i = 0; i < rows.length; ++i) {
            const row = root.sideBySide ? rows[i].left : rows[i]
            if (row && row.kind === "hunk" && row.hunkIndex === hunkIndex) {
                hunkRow = i
                break
            }
        }
        if (hunkRow < 0) return

        if (hunkRow < root.windowStart || hunkRow >= root.windowEnd) {
            const halfWindow = Math.floor(root.maxRenderedLines / 2)
            let newStart = Math.max(0, hunkRow - halfWindow)
            let newEnd = Math.min(root.totalLineCount, newStart + root.maxRenderedLines)
            if (newEnd === root.totalLineCount) {
                newStart = Math.max(0, root.totalLineCount - root.maxRenderedLines)
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
        const targetParsedRow = DiffParser.matchRow(root.searchIndex, matchIdx)

        if (targetParsedRow < 0) return

        let targetDisplayRow = targetParsedRow
        if (root.sideBySide) {
            targetDisplayRow = root.pairedRows.findIndex(pair =>
                (pair.left && pair.left.sourceIndex === targetParsedRow)
                || (pair.right && pair.right.sourceIndex === targetParsedRow))
        }
        if (targetDisplayRow < 0) return

        if (root.isTruncated && (targetDisplayRow < root.windowStart || targetDisplayRow >= root.windowEnd)) {
            const halfWindow = Math.floor(root.maxRenderedLines / 2)
            let newStart = Math.max(0, targetDisplayRow - halfWindow)
            let newEnd = Math.min(root.totalLineCount, newStart + root.maxRenderedLines)
            if (newEnd === root.totalLineCount) {
                newStart = Math.max(0, root.totalLineCount - root.maxRenderedLines)
            }
            root.windowStart = newStart
        }

        Qt.callLater(function() {
            const rows = root.sideBySide ? root.displayPairedRows : root.displayParsedRows
            if (!rows || rows.length === 0) return
            const rowInDisplay = targetDisplayRow - (root.isTruncated ? root.windowStart : 0)
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
        Qt.callLater(function() {
            if (diffScroll.contentItem) diffScroll.contentItem.contentY = 0
        })
    }

    function nextWindowPage() {
        if (windowEnd >= totalLineCount) return
        windowStart += maxRenderedLines
        Qt.callLater(function() {
            if (diffScroll.contentItem) diffScroll.contentItem.contentY = 0
        })
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

                SearchField {
                    id: searchInput
                    width: Math.min(280, toolbarRow.width)
                    height: Theme.controlHeight
                    placeholderText: "搜索差异"
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
                        text: root.searchMatchCount > 0 ? (root.currentMatchIndex + 1) + "/" + root.searchMatchCount : "无匹配"
                        color: root.searchMatchCount > 0 ? Theme.secondaryText : Theme.redText
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.Medium
                        visible: root.searchQuery.trim().length > 0
                        Layout.rightMargin: 2
                    }
                    IconButton {
                        visible: root.searchMatchCount > 1
                        iconName: "chevron-up"
                        toolTip: "上一个匹配"
                        implicitWidth: 22
                        implicitHeight: 22
                        focusPolicy: Qt.NoFocus
                        onClicked: root.prevSearchMatch()
                    }
                    IconButton {
                        visible: root.searchMatchCount > 1
                        iconName: "chevron-down"
                        toolTip: "下一个匹配（Enter）"
                        implicitWidth: 22
                        implicitHeight: 22
                        focusPolicy: Qt.NoFocus
                        onClicked: root.nextSearchMatch()
                    }
                }

                // Each operation participates in wrapping, even at minimum pane width.
                Flow {
                    id: hunkActions
                    // Natural single-row width, so it can share the row with search
                    // and only wraps when the pane is narrow.
                    readonly property real naturalWidth: {
                        let total = 0
                        let count = 0
                        for (let i = 0; i < children.length; ++i) {
                            if (!children[i].visible) continue
                            total += children[i].width
                            ++count
                        }
                        return total + Math.max(0, count - 1) * spacing
                    }
                    width: Math.min(toolbarRow.width, naturalWidth)
                    visible: root.hunkTotal > 0
                    spacing: 6

                    // Flow children use actual dimensions, not Layout attached hints.

                    Rectangle {
                        width: hunkNav.implicitWidth + 4
                        height: Theme.controlHeight
                        radius: Theme.controlRadius
                        color: Theme.fillQuaternary
                        Row {
                            id: hunkNav
                            anchors.centerIn: parent
                            spacing: 0
                            IconButton {
                                iconName: "chevron-left"
                                toolTip: "上一个代码块"
                                implicitWidth: 26
                                implicitHeight: 26
                                enabled: root.currentHunkIndex > 0
                                onClicked: root.currentHunkIndex = Math.max(0, root.currentHunkIndex - 1)
                            }
                            Text {
                                text: "块 " + (root.currentHunkIndex + 1) + " / " + root.hunkTotal
                                height: 26
                                leftPadding: 4
                                rightPadding: 4
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.fontSecondary
                                font.weight: Font.Medium
                                color: Theme.secondaryText
                            }
                            IconButton {
                                iconName: "chevron-right"
                                toolTip: "下一个代码块"
                                implicitWidth: 26
                                implicitHeight: 26
                                enabled: root.currentHunkIndex < root.hunkTotal - 1
                                onClicked: root.currentHunkIndex = Math.min(root.hunkTotal - 1, root.currentHunkIndex + 1)
                            }
                        }
                    }

                    FileActionButton {
                        implicitWidth: Theme.controlHeight
                        implicitHeight: Theme.controlHeight
                        visible: root.workspace && !root.workspace.selectedFileStaged && root.isWorktreeDiff
                        iconName: "plus"
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
                        iconName: "plus-square"
                        toolTip: "暂存整个文件"

                        enabled: root.workspace && !root.workspace.busy
                        onClicked: root.workspace.stageFiles([root.workspace.selectedFile])
                    }

                    FileActionButton {
                        implicitWidth: Theme.controlHeight
                        implicitHeight: Theme.controlHeight
                        visible: root.workspace && root.workspace.selectedFileStaged && root.isWorktreeDiff
                        iconName: "minus"
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
                        iconName: "minus-square"
                        toolTip: "取消暂存整个文件"

                        enabled: root.workspace && !root.workspace.busy
                        onClicked: root.workspace.unstageFiles([root.workspace.selectedFile])
                    }

                    FileActionButton {
                        implicitWidth: Theme.controlHeight
                        implicitHeight: Theme.controlHeight
                        visible: root.workspace && !root.workspace.selectedFileStaged && root.isWorktreeDiff
                        iconName: "undo"
                        destructive: true
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

        // Large diff: windowed paging
        Banner {
            objectName: "largeDiffBanner"
            visible: root.isTruncated
            Layout.fillWidth: true
            tone: "warning"
            iconName: "file-diff"
            actionsRightAligned: false
            title: "当前显示 " + (root.windowStart + 1) + "–" + root.windowEnd + " / " + root.totalLineCount + " 行"
            message: "改动较大，已分页渲染；可翻页或搜索查看全部内容"

            PrimaryButton {
                objectName: "previousDiffPageButton"
                kind: "plain"
                compact: true
                iconName: "chevron-left"
                text: "上一页"
                focusPolicy: Qt.NoFocus
                enabled: root.windowStart > 0
                onClicked: root.prevWindowPage()
            }
            PrimaryButton {
                objectName: "nextDiffPageButton"
                kind: "plain"
                compact: true
                text: "下一页"
                iconName: "chevron-right"
                focusPolicy: Qt.NoFocus
                enabled: root.windowEnd < root.totalLineCount
                onClicked: root.nextWindowPage()
            }
            PrimaryButton {
                objectName: "showAllDiffButton"
                kind: "tinted"
                compact: true
                text: "加载全部"
                onClicked: root.requestFullDiff()
            }
            PrimaryButton {
                visible: root.workspace && root.workspace.selectedFile && root.workspace.selectedFile.length > 0
                kind: "plain"
                compact: true
                iconName: "arrow-up-right"
                text: "系统程序打开"
                onClicked: root.workspace.openFile(root.workspace.selectedFile)
            }
        }

        // Full diff loaded after confirmation
        Banner {
            visible: root.totalLineCount > root.maxRenderedLines && root.showAllDiff
            Layout.fillWidth: true
            tone: "info"
            message: "已加载完整差异（共 " + root.totalLineCount + " 行）"
            PrimaryButton {
                text: "恢复分页"
                kind: "plain"
                compact: true
                onClicked: {
                    root.windowStart = 0
                    root.showAllDiff = false
                    root.ensureHunkInWindow(root.currentHunkIndex)
                    Qt.callLater(root.scrollToCurrentHunk)
                }
            }
        }

        // Binary file notice
        Banner {
            visible: root.isBinary
            Layout.fillWidth: true
            tone: "neutral"
            iconName: "package"
            title: "二进制文件，无法显示文本差异"
            message: root.workspace && root.workspace.selectedFile
                ? root.workspace.selectedFile
                : "此文件为二进制文件或不可显示的格式"
            PrimaryButton {
                visible: root.workspace && root.workspace.selectedFile.length > 0
                kind: "gray"
                compact: true
                iconName: "arrow-up-right"
                text: "在系统默认程序中打开"
                onClicked: root.workspace.openFile(root.workspace.selectedFile)
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
        preferredWidth: 400
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace)
                root.workspace.discardHunk(root.currentHunkIndex)
        }
        contentItem: Text {
            text: "确定要丢弃当前选中的代码块 #" + (root.currentHunkIndex + 1) + " 吗？\n丢弃后此代码块的工作区修改将永久丢失。"
            color: Theme.text
            font.pixelSize: Theme.fontSecondary
            wrapMode: Text.Wrap
        }
    }
}
