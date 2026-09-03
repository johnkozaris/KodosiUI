import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "startup.overlay.popup"

    property bool diagnosticsOpen: false
    readonly property bool failed:
        Models.ApplicationLifecycle.state
        === Models.ApplicationLifecycle.Failed
    readonly property bool contentAvailable:
        root.visible && !root.diagnosticsOpen

    signal diagnosticsRequested()

    function restorePrimaryFocus() {
        if (!visible || diagnosticsOpen)
            return
        if (failed)
            retryButton.forceActiveFocus(Qt.PopupFocusReason)
        else
            startupScope.forceActiveFocus(Qt.PopupFocusReason)
    }

    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    visible:
        Models.ApplicationLifecycle.state
        !== Models.ApplicationLifecycle.Ready
    enabled: visible
    modal: true
    dim: false
    focus: visible && !diagnosticsOpen
    closePolicy: Popup.NoAutoClose
    padding: 0

    onVisibleChanged: {
        if (visible)
            Qt.callLater(restorePrimaryFocus)
    }

    onFailedChanged:
        Qt.callLater(restorePrimaryFocus)

    background: Rectangle {
        color: KodosiTheme.canvas
    }

    contentItem: FocusScope {
        id: startupScope
        objectName: "startup.overlay"
        Accessible.id: objectName
        focus: true
        Accessible.role: Accessible.Dialog
        Accessible.name: root.failed
            ? qsTr("Kodosi couldn’t start")
            : qsTr("Starting Kodosi")
        Accessible.description: root.failed
            ? Models.ApplicationLifecycle.errorText
            : qsTr("Kodosi is starting.")
        Accessible.ignored: !root.visible || root.diagnosticsOpen

        ColumnLayout {
            id: startupContent
            objectName: "startup.content"
            Accessible.id: objectName
            anchors.centerIn: parent
            width: Math.min(460, Math.max(280, parent.width - 48))
            spacing: KodosiTheme.spacing5
            visible: root.contentAvailable
            enabled: root.contentAvailable
            Accessible.ignored: !root.contentAvailable

            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56

                Rectangle {
                    anchors.fill: parent
                    radius: KodosiTheme.radiusLarge
                    color: KodosiTheme.surfaceElevated
                }

                KBusyIndicator {
                    id: busyIndicator
                    objectName: "startup.busy"
                    Accessible.id: objectName
                    Accessible.ignored: true
                    anchors.centerIn: parent
                    running: root.contentAvailable && !root.failed
                }

                KIcon {
                    objectName: "startup.failure.icon"
                    Accessible.id: objectName
                    Accessible.ignored: true
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    visible: root.contentAvailable && root.failed
                    name: "warning"
                    color: KodosiTheme.danger
                    strokeWidth: 1.9
                }
            }

            PlainLabel {
                id: titleLabel
                objectName: "startup.title"
                Accessible.id: objectName
                Layout.fillWidth: true
                text: root.failed
                    ? qsTr("Kodosi couldn’t start")
                    : qsTr("Starting Kodosi…")
                color: root.failed
                    ? KodosiTheme.textPrimary
                    : KodosiTheme.textSecondary
                font.pixelSize: root.failed ? 20 : 14
                font.weight: root.failed ? Font.DemiBold : Font.Medium
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Accessible.ignored: !root.contentAvailable
            }

            PlainLabel {
                id: detailLabel
                objectName: "startup.failure.detail"
                Accessible.id: objectName
                Layout.fillWidth: true
                Layout.topMargin: root.failed
                    ? 0
                    : -KodosiTheme.spacing2
                visible: root.contentAvailable && root.failed
                text: Models.ApplicationLifecycle.errorText
                color: KodosiTheme.textSecondary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                maximumLineCount: 5
                elide: Text.ElideRight
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Accessible.ignored: !root.contentAvailable
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: root.failed
                    ? KodosiTheme.spacing3
                    : 0
                spacing: KodosiTheme.spacing3
                visible: root.contentAvailable && root.failed

                KButton {
                    id: retryButton
                    objectName: "startup.retry"
                    Accessible.id: objectName
                    text: qsTr("Try Again")
                    variant: "primary"
                    iconName: "refresh"
                    Accessible.name: text
                    Accessible.ignored: !root.contentAvailable
                    onClicked: Models.ApplicationLifecycle.retry()
                }

                KButton {
                    id: diagnosticsButton
                    objectName: "startup.diagnostics"
                    Accessible.id: objectName
                    text: qsTr("Diagnostics")
                    variant: "secondary"
                    iconName: "diagnostics"
                    Accessible.name: text
                    Accessible.ignored: !root.contentAvailable
                    onClicked: root.diagnosticsRequested()
                }
            }
        }
    }
}
