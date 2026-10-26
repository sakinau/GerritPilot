import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

Menu {
    id: root
    implicitWidth: 220
    padding: 5
    background: FloatingSurface {
        implicitWidth: root.implicitWidth
        radius: Theme.radiusMedium
    }
    delegate: AppMenuItem { }
}
