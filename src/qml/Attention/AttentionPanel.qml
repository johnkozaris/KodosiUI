pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "panel.attention"

    parent: Overlay.overlay
    width: Math.min(440, parent ? parent.width - 32 : 440)
    height: Math.min(680, parent ? parent.height - 32 : 680)
    x: parent ? parent.width - width - 16 : 0
    y: 16
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: closeButton.forceActiveFocus()
    palette.window: KodosiTheme.surface
    palette.windowText: KodosiTheme.textPrimary
    palette.base: KodosiTheme.surface
    palette.alternateBase: KodosiTheme.surfaceElevated
    palette.button: KodosiTheme.surfaceElevated
    palette.buttonText: KodosiTheme.textPrimary
    palette.text: KodosiTheme.textPrimary
    palette.highlight: KodosiTheme.accent
    palette.highlightedText: KodosiTheme.accentForeground
    palette.placeholderText: KodosiTheme.textSecondary
    palette.mid: KodosiTheme.seam

    Overlay.modal: Rectangle {
        color: KodosiTheme.overlayDim
    }

    background: Rectangle {
        color: KodosiTheme.surface
        radius: KodosiTheme.radiusLarge
        border.width: 1
        border.color: KodosiTheme.seam
    }

    contentItem: ColumnLayout {
        objectName: "panel.attention"
        Accessible.id: objectName
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Attention")
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: KodosiTheme.surfaceElevated
            topLeftRadius: KodosiTheme.radiusLarge
            topRightRadius: KodosiTheme.radiusLarge

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing5
                anchors.rightMargin: KodosiTheme.spacing5
                spacing: KodosiTheme.spacing3

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    PlainLabel {
                        text: Models.Attention.count > 0
                            ? qsTr("Needs you · %1").arg(
                                Models.Attention.count)
                            : qsTr("Attention")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }

                    PlainLabel {
                        text: Models.Attention.runningCount > 0
                            ? qsTr("%1 agents running").arg(
                                Models.Attention.runningCount)
                            : qsTr("Approvals and agent status")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 10
                    }
                }

                AttentionButton {
                    id: closeButton
                    accessibleId: "panel.attention.close"
                    text: qsTr("Close")
                    Accessible.name: qsTr("Close Attention")
                    onClicked: root.close()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KodosiTheme.seam
            }
        }

        Rectangle {
            objectName: "panel.attention.operationError"
            Accessible.id: objectName
            Accessible.role: Accessible.AlertMessage
            Accessible.name: Models.Attention.operationError
            visible: Models.Attention.operationError.length > 0
            Layout.fillWidth: true
            implicitHeight: visible ? operationErrorRow.implicitHeight + 14 : 0
            color: Qt.rgba(
                KodosiTheme.danger.r,
                KodosiTheme.danger.g,
                KodosiTheme.danger.b,
                0.08)

            RowLayout {
                id: operationErrorRow
                anchors.fill: parent
                anchors.margins: 7
                spacing: KodosiTheme.spacing2

                PlainLabel {
                    Layout.fillWidth: true
                    text: Models.Attention.operationError
                    color: KodosiTheme.danger
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }

                AttentionButton {
                    accessibleId: "panel.attention.operationError.dismiss"
                    text: qsTr("Dismiss")
                    Accessible.name: qsTr("Dismiss Attention error")
                    onClicked: Models.Attention.clearOperationError()
                }
            }
        }

        Rectangle {
            objectName: "panel.attention.authorityError"
            Accessible.id: objectName
            Accessible.role: Accessible.AlertMessage
            Accessible.name: Models.Attention.authorityError
            visible: Models.Attention.count > 0
                && Models.Attention.authorityState
                    === Models.Attention.Failed
            Layout.fillWidth: true
            implicitHeight: visible ? authorityErrorRow.implicitHeight + 14 : 0
            color: Qt.rgba(
                KodosiTheme.danger.r,
                KodosiTheme.danger.g,
                KodosiTheme.danger.b,
                0.08)

            RowLayout {
                id: authorityErrorRow
                anchors.fill: parent
                anchors.margins: 7
                spacing: KodosiTheme.spacing2

                PlainLabel {
                    Layout.fillWidth: true
                    text: Models.Attention.authorityError
                    color: KodosiTheme.danger
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }

                AttentionButton {
                    accessibleId: "panel.attention.authorityError.retry"
                    primary: true
                    text: qsTr("Retry")
                    Accessible.name: qsTr(
                        "Retry incomplete Attention results")
                    onClicked: Models.Attention.refresh()
                }
            }
        }

        Item {
            objectName: "panel.attention.loading"
            Accessible.id: objectName
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Checking what needs you")
            visible: Models.Attention.count === 0
                && Models.Attention.authorityState
                    === Models.Attention.Loading
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(330, parent.width - 32)
                spacing: KodosiTheme.spacing3

                AttentionSpinner {
                    Layout.alignment: Qt.AlignHCenter
                    running: Models.Attention.count === 0
                        && Models.Attention.authorityState
                            === Models.Attention.Loading
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Checking what needs you…")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Kodosi is reconciling sessions and pending approvals.")
                    color: KodosiTheme.textSecondary
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Item {
            objectName: "panel.attention.error"
            Accessible.id: objectName
            Accessible.role: Accessible.AlertMessage
            Accessible.name: qsTr("Attention is unavailable")
            visible: Models.Attention.count === 0
                && Models.Attention.authorityState
                    === Models.Attention.Failed
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(350, parent.width - 32)
                spacing: KodosiTheme.spacing3

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Attention sources unavailable")
                    color: KodosiTheme.danger
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: Models.Attention.authorityError
                    color: KodosiTheme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                AttentionButton {
                    accessibleId: "panel.attention.retry"
                    Layout.alignment: Qt.AlignHCenter
                    primary: true
                    text: qsTr("Retry")
                    Accessible.name: qsTr(
                        "Retry Attention sources")
                    onClicked: Models.Attention.refresh()
                }
            }
        }

        Item {
            objectName: "panel.attention.empty"
            Accessible.id: objectName
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Nothing needs you")
            visible: Models.Attention.count === 0
                && Models.Attention.authorityState
                    === Models.Attention.Loaded
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(330, parent.width - 32)
                spacing: KodosiTheme.spacing3

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: KodosiTheme.radiusSmall
                    color: Qt.rgba(
                        KodosiTheme.success.r,
                        KodosiTheme.success.g,
                        KodosiTheme.success.b,
                        0.10)
                    border.width: 1
                    border.color: Qt.rgba(
                        KodosiTheme.success.r,
                        KodosiTheme.success.g,
                        KodosiTheme.success.b,
                        0.35)

                    Rectangle {
                        anchors.centerIn: parent
                        width: 7
                        height: 7
                        radius: 4
                        color: KodosiTheme.success
                    }
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Nothing needs you")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Pending approvals and waiting agents will appear here.")
                    color: KodosiTheme.textSecondary
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        ListView {
            id: attentionList
            objectName: "panel.attention.list"
            Accessible.id: objectName
            Accessible.name: qsTr("Items needing attention")
            visible: Models.Attention.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: KodosiTheme.spacing4
            spacing: KodosiTheme.spacing3
            clip: true
            model: Models.Attention

            delegate: AttentionCard {
                width: attentionList.width
                accessiblePrefix: "panel.attention"
            }
        }
    }
}
