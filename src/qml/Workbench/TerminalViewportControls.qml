import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root
    required property Models.TerminalView terminalView
    required property string identifier
    visible: terminalView.terminalReady && !terminalView.canResize
    readonly property int topInset: visible ? 36 : 0
    readonly property int scrollInset: visible && !terminalView.fitToView ? 10 : 0

    RowLayout {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        spacing: 4
        KButton {
            objectName: root.identifier + ".fit"
            Accessible.id: objectName
            text: root.terminalView.fitToView ? qsTr("Fit · %1%").arg(Math.round(root.terminalView.viewportScale * 100)) : qsTr("Actual size")
            compact: true
            checkable: true
            checked: root.terminalView.fitToView
            Accessible.name: qsTr("Fit remote terminal to pane")
            onClicked: root.terminalView.fitToView = !root.terminalView.fitToView
        }
        KIconButton {
            objectName: root.identifier + ".cursor"
            Accessible.id: objectName
            glyph: "focus"
            size: 30
            visible: !root.terminalView.fitToView
            Accessible.name: qsTr("Show terminal cursor")
            onClicked: root.terminalView.revealCursor()
        }
    }

    KScrollBar {
        objectName: root.identifier + ".horizontal"
        Accessible.id: objectName
        Accessible.name: qsTr("Pan terminal horizontally")
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        orientation: Qt.Horizontal
        prominent: true
        visible: !root.terminalView.fitToView && root.terminalView.gridSize.width > root.terminalView.width
        size: Math.min(1, root.terminalView.width / Math.max(1, root.terminalView.gridSize.width))
        position: root.terminalView.panX / Math.max(1, root.terminalView.gridSize.width)
        onPositionChanged: if (pressed) root.terminalView.panX = position * root.terminalView.gridSize.width
    }
    KScrollBar {
        objectName: root.identifier + ".vertical"
        Accessible.id: objectName
        Accessible.name: qsTr("Pan terminal vertically")
        anchors.top: parent.top
        anchors.topMargin: 44
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        prominent: true
        visible: !root.terminalView.fitToView && root.terminalView.gridSize.height > root.terminalView.height
        size: Math.min(1, root.terminalView.height / Math.max(1, root.terminalView.gridSize.height))
        position: root.terminalView.panY / Math.max(1, root.terminalView.gridSize.height)
        onPositionChanged: if (pressed) root.terminalView.panY = position * root.terminalView.gridSize.height
    }
}
