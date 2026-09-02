import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root

    signal openDiagnosticsRequested()

    // Healthy may include routine pending cleanup; only degraded store states
    // require operator attention, matching the shipping Swift client.
    readonly property bool cleanupIssue:
        Models.RuntimeDiagnostics.hasRuntimeHealth
        && Models.RuntimeDiagnostics.cleanupState
            !== Models.RuntimeDiagnostics.Healthy
    readonly property bool runtimeIssue:
        Models.RuntimeDiagnostics.runtimeError.length > 0

    function cleanupText() {
        if (Models.RuntimeDiagnostics.cleanupState
                === Models.RuntimeDiagnostics.Unavailable
                || Models.RuntimeDiagnostics.cleanupState
                === Models.RuntimeDiagnostics.Unknown)
            return qsTr("Collaboration cleanup is temporarily unavailable.")
        if (Models.RuntimeDiagnostics.cleanupQuarantinedCount === 1)
            return qsTr("1 collaboration item needs attention.")
        if (Models.RuntimeDiagnostics.cleanupQuarantinedCount > 1)
            return qsTr("%1 collaboration items need attention.").arg(
                Models.RuntimeDiagnostics.cleanupQuarantinedCount)
        return qsTr("Collaboration cleanup needs attention.")
    }

    objectName: "banner.runtime.health"
    Accessible.id: objectName
    visible: runtimeIssue || cleanupIssue
    implicitHeight: visible ? banners.implicitHeight : 0

    ColumnLayout {
        id: banners
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        RuntimeBannerRow {
            objectName: "banner.runtime.error"
            Accessible.id: objectName
            Layout.fillWidth: true
            visible: root.runtimeIssue
            title: qsTr("Kodosi could not complete a runtime action.")
            detail: Models.RuntimeDiagnostics.runtimeError
            Accessible.name: qsTr("Runtime error")
            Accessible.description: detail
            dismissible: true
            onDiagnosticsRequested: root.openDiagnosticsRequested()
            onDismissRequested:
                Models.RuntimeDiagnostics.clearRuntimeError()
        }

        RuntimeBannerRow {
            objectName: "banner.runtime.collaborationCleanup"
            Accessible.id: objectName
            Layout.fillWidth: true
            visible: root.cleanupIssue
            title: root.cleanupText()
            detail: Models.RuntimeDiagnostics.cleanupMessage
            Accessible.name: qsTr("Collaboration cleanup warning")
            Accessible.description: detail.length > 0
                ? title + " " + detail
                : title
            onDiagnosticsRequested: root.openDiagnosticsRequested()
        }
    }

    component RuntimeBannerRow: Rectangle {
        id: banner

        signal diagnosticsRequested()
        signal dismissRequested()

        required property string title
        property string detail: ""
        property bool dismissible: false

        implicitHeight: Math.max(44, row.implicitHeight + 14)
        color: KodosiTheme.surfaceElevated

        RowLayout {
            id: row
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: KodosiTheme.spacing5
            anchors.rightMargin: KodosiTheme.spacing5
            spacing: KodosiTheme.spacing3

            KIcon {
                Layout.preferredWidth: 15
                Layout.preferredHeight: 15
                name: "warning"
                color: KodosiTheme.danger
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                PlainLabel {
                    Layout.fillWidth: true
                    text: banner.title
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                PlainLabel {
                    Layout.fillWidth: true
                    visible: banner.detail.length > 0
                    text: banner.detail
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            KButton {
                objectName: banner.dismissible
                    ? "banner.runtime.diagnostics"
                    : "banner.runtime.collaborationCleanup.diagnostics"
                Accessible.id: objectName
                text: qsTr("Diagnostics")
                compact: true
                Accessible.name: text
                onClicked: banner.diagnosticsRequested()
            }

            KIconButton {
                objectName: "banner.runtime.dismiss"
                Accessible.id: objectName
                visible: banner.dismissible
                glyph: "close"
                Accessible.name: qsTr("Dismiss runtime error")
                onClicked: banner.dismissRequested()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: KodosiTheme.danger
            opacity: 0.5
        }
    }
}
