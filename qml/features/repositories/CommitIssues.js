.pragma library

function formatMultipleIssues(rawText, defaultPrefix) {
    if (!rawText || !rawText.trim()) return ""
    var p = (defaultPrefix && defaultPrefix.trim().length > 0) ? defaultPrefix.trim() : "m-"
    if (!p.endsWith("-")) p += "-"
    var tokens = rawText.trim().split(/[,;，；\s]+/)
    var list = []
    for (var i = 0; i < tokens.length; ++i) {
        var tok = tokens[i].trim()
        if (!tok) continue
        if (/^m-/i.test(tok)) {
            tok = "m-" + tok.substring(2)
        } else if (/^r-/i.test(tok)) {
            tok = "r-" + tok.substring(2)
        } else {
            tok = p + tok
        }
        if (list.indexOf(tok) === -1) {
            list.push(tok)
        }
    }
    return list.join(",")
}
