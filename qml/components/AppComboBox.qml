import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

ComboBox {
    id: control
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontSecondary
    leftPadding: 10
    rightPadding: 30
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.controlRadius
        color: !control.enabled ? Theme.surfaceMuted : control.hovered ? Theme.surfaceMuted : Theme.surface
        border.width: control.visualFocus || control.popup.visible ? 2 : 1
        border.color: control.visualFocus || control.popup.visible ? Theme.focusRing : Theme.separator
    }
    indicator: Icon {
        x: control.width - width - 10
        y: (control.height - height) / 2
        name: "chevrons-up-down"
        size: 14
        color: Theme.secondaryText
    }
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.tertiaryText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    delegate: ItemDelegate {
        id: option
        required property var model
        required property int index
        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: 30
        leftPadding: 10
        rightPadding: 10
        highlighted: control.highlightedIndex === index
        text: control.textRole
            ? (Array.isArray(control.model) ? model.modelData[control.textRole] : model[control.textRole])
            : model.modelData
        contentItem: Text {
            text: option.text
            font: control.font
            color: option.highlighted ? Theme.textOnAccent : Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: option.highlighted ? Theme.accent : "transparent"
        }
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 320)
        padding: 5
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: RightScrollBar { }
        }
        background: FloatingSurface { radius: Theme.radiusMedium }
    }
}
