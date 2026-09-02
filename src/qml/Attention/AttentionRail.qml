pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Rectangle {
    id: root

    objectName: "sidebar.attention.rail"
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Attention")
    visible: Models.Attention.count > 0
    implicitHeight: visible
        ? Math.min(260, header.implicitHeight + attentionList.contentHeight + 18)
        : 0
    color: KodosiTheme.surface

    Item {
        objectName: "stage.approvals"
        Accessible.id: objectName
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Approvals")
        anchors.fill: parent
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: KodosiTheme.spacing2

        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing5
            Layout.rightMargin: KodosiTheme.spacing5
            Layout.topMargin: KodosiTheme.spacing3
            spacing: KodosiTheme.spacing2

            Rectangle {
                Layout.preferredWidth: 6
                Layout.preferredHeight: 6
                radius: 3
                color: KodosiTheme.accent
            }

            PlainLabel {
                text: qsTr("Needs you")
                color: KodosiTheme.textPrimary
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            PlainLabel {
                text: String(Models.Attention.count)
                color: KodosiTheme.accent
                font.pixelSize: 10
                font.weight: Font.Bold
            }

            Item {
                Layout.fillWidth: true
            }

            PlainLabel {
                visible: Models.Attention.runningCount > 0
                text: qsTr("%1 running").arg(
                    Models.Attention.runningCount)
                color: KodosiTheme.textSecondary
                font.pixelSize: 9
            }
        }

        ListView {
            id: attentionList
            objectName: "sidebar.attention.list"
            Accessible.id: objectName
            Accessible.name: qsTr("Items needing attention")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: KodosiTheme.spacing3
            Layout.rightMargin: KodosiTheme.spacing3
            Layout.bottomMargin: KodosiTheme.spacing3
            spacing: KodosiTheme.spacing2
            clip: true
            model: Models.Attention

            delegate: AttentionCard {
                width: attentionList.width
                compact: true
                legacyApprovalIds: true
                accessiblePrefix: "sidebar.attention"
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Qt.rgba(
            KodosiTheme.accent.r,
            KodosiTheme.accent.g,
            KodosiTheme.accent.b,
            0.5)
    }
}
