pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Rectangle {
    id: root

    required property int category
    required property string sessionId
    required property string sessionName
    required property string title
    required property string summary
    required property int risk
    required property int tone
    required property int actionKind
    required property bool canApprove
    required property bool canDeny
    required property bool canJump
    required property string attentionToken
    required property string stableId
    property bool compact: false
    property bool legacyApprovalIds: false
    property string accessiblePrefix: "attention"

    objectName: accessiblePrefix + ".card." + stableId
    Accessible.id: objectName
    Accessible.role: Accessible.ListItem
    Accessible.name: sessionName.length > 0
        ? qsTr("%1: %2").arg(sessionName).arg(title)
        : title
    Accessible.description: summary
    implicitHeight: content.implicitHeight + (compact ? 16 : 20)
    radius: KodosiTheme.radiusSmall
    color: root.toneColor(root.tone, 0.06)
    border.width: 1
    border.color: root.toneColor(root.tone, 0.26)

    function toneColor(value, alpha) {
        const base = value === Models.Attention.Danger
            ? KodosiTheme.danger
            : value === Models.Attention.Warning
              ? KodosiTheme.warning
              : value === Models.Attention.Accent
                ? KodosiTheme.accent
                : KodosiTheme.textSecondary
        return Qt.rgba(base.r, base.g, base.b, alpha)
    }

    function riskText(value) {
        switch (value) {
        case Models.Attention.Safe: return qsTr("SAFE")
        case Models.Attention.Network: return qsTr("NETWORK")
        case Models.Attention.Credential: return qsTr("CREDENTIAL")
        case Models.Attention.Destructive: return qsTr("DESTRUCTIVE")
        case Models.Attention.Unknown: return qsTr("UNKNOWN")
        default: return ""
        }
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: root.compact
            ? KodosiTheme.spacing3
            : KodosiTheme.spacing4
        spacing: KodosiTheme.spacing2

        RowLayout {
            Layout.fillWidth: true
            spacing: KodosiTheme.spacing2

            Rectangle {
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                radius: 4
                color: root.toneColor(root.tone, 1)
            }

            PlainLabel {
                Layout.fillWidth: true
                text: root.category === Models.Attention.BulkSafe
                    ? root.title
                    : root.sessionName.length > 0
                    ? root.sessionName
                    : root.title
                color: KodosiTheme.textPrimary
                font.pixelSize: root.compact ? 11 : 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            PlainLabel {
                visible: text.length > 0
                text: root.riskText(root.risk)
                color: root.toneColor(root.tone, 1)
                font.pixelSize: 9
                font.weight: Font.Bold
                font.letterSpacing: 0.8
            }
        }

        PlainLabel {
            Layout.fillWidth: true
            text: root.category === Models.Attention.BulkSafe
                ? root.summary
                : root.sessionName.length > 0
                ? root.title
                : root.summary
            color: KodosiTheme.textPrimary
            font.pixelSize: 11
            font.weight: Font.Medium
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        PlainLabel {
            Layout.fillWidth: true
            visible: root.category !== Models.Attention.BulkSafe
                && root.sessionName.length > 0
                && root.summary.length > 0
            text: root.summary
            color: KodosiTheme.textSecondary
            font.pixelSize: 10
            wrapMode: Text.Wrap
            maximumLineCount: root.compact ? 1 : 2
            elide: Text.ElideMiddle
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: KodosiTheme.spacing2

            AttentionButton {
                accessibleId: root.accessiblePrefix + ".jump."
                    + root.stableId
                visible: root.canJump
                    && root.category !== Models.Attention.Approval
                text: qsTr("Open")
                Accessible.name: qsTr("Open %1").arg(root.sessionName)
                onClicked: Models.Attention.jump(root.attentionToken)
            }

            AttentionButton {
                accessibleId: root.accessiblePrefix + ".review."
                    + root.stableId
                visible: root.actionKind === Models.Attention.Review
                text: qsTr("Review")
                Accessible.name: qsTr("Review %1 approval").arg(
                    root.sessionName)
                onClicked: Models.Attention.review(root.attentionToken)
            }

            Item {
                Layout.fillWidth: true
            }

            AttentionButton {
                accessibleId: root.legacyApprovalIds
                        && root.category === Models.Attention.Approval
                    ? "approval.deny." + root.attentionToken
                    : root.accessiblePrefix + ".deny."
                        + root.stableId
                intentColor: KodosiTheme.danger
                foregroundColor: KodosiTheme.danger
                visible: root.category === Models.Attention.Approval
                text: qsTr("Deny")
                enabled: root.canDeny
                Accessible.name: qsTr("Deny %1").arg(root.title)
                onClicked: Models.Attention.deny(root.attentionToken)
            }

            AttentionButton {
                id: approveButton
                accessibleId: root.legacyApprovalIds
                        && root.category === Models.Attention.Approval
                    ? "approval.allow." + root.attentionToken
                    : root.accessiblePrefix + ".approve."
                        + root.stableId
                visible: root.actionKind === Models.Attention.Approve
                text: root.canApprove ? qsTr("Allow") : qsTr("Waiting")
                enabled: root.canApprove
                primary: true
                Accessible.name: qsTr("Allow %1").arg(root.title)
                onClicked: Models.Attention.approve(root.attentionToken)
            }

            AttentionButton {
                id: approveAllButton
                accessibleId: root.accessiblePrefix + ".approveAll."
                    + root.stableId
                visible: root.category === Models.Attention.BulkSafe
                text: qsTr("Approve all")
                enabled: root.canApprove
                primary: true
                Accessible.name: qsTr("Approve all safe reads")
                onClicked: Models.Attention.approveAll(root.attentionToken)
            }
        }
    }
}
