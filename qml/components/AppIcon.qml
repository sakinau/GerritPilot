import QtQuick

// Application logo. The same SVG feeds the window icon (see main.cpp) and the
// installed desktop icon, so the brand has a single source of truth.
Item {
    id: root
    implicitWidth: 34
    implicitHeight: 34
    Accessible.name: "GerritPilot"

    Image {
        anchors.fill: parent
        source: Qt.resolvedUrl("../assets/gerritpilot.svg")
        sourceSize: Qt.size(Math.ceil(root.width * 2), Math.ceil(root.height * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }
}
