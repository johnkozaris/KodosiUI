pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "panel.diagnostics"
    property int logOpenSequence: 0
    property string logOpenRequestId: ""
    property string logOpenError: ""

    parent: Overlay.overlay
    width: Math.min(680, parent ? parent.width - 40 : 680)
    height: Math.min(600, parent ? parent.height - 40 : 600)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    function cleanupValue() {
        if (!Models.RuntimeDiagnostics.hasRuntimeHealth)
            return qsTr("Waiting for runtime")
        if (Models.RuntimeDiagnostics.cleanupState
                === Models.RuntimeDiagnostics.Unavailable
                || Models.RuntimeDiagnostics.cleanupState
                === Models.RuntimeDiagnostics.Unknown)
            return qsTr("Temporarily unavailable")
        if (Models.RuntimeDiagnostics.cleanupState
                === Models.RuntimeDiagnostics.Quarantined
                && Models.RuntimeDiagnostics.cleanupQuarantinedCount === 0)
            return Models.RuntimeDiagnostics.cleanupMessage.length > 0
                ? Models.RuntimeDiagnostics.cleanupMessage
                : qsTr("Cleanup store is quarantined")
        if (Models.RuntimeDiagnostics.cleanupPendingCount === 0
                && Models.RuntimeDiagnostics.cleanupQuarantinedCount === 0)
            return qsTr("No pending cleanup")
        if (Models.RuntimeDiagnostics.cleanupQuarantinedCount === 0)
            return Models.RuntimeDiagnostics.cleanupPendingCount === 1
                ? qsTr("1 pending")
                : qsTr("%1 pending").arg(
                    Models.RuntimeDiagnostics.cleanupPendingCount)
        const pending = Models.RuntimeDiagnostics.cleanupPendingCount === 1
            ? qsTr("1 pending")
            : qsTr("%1 pending").arg(
                Models.RuntimeDiagnostics.cleanupPendingCount)
        const quarantined =
            Models.RuntimeDiagnostics.cleanupQuarantinedCount === 1
            ? qsTr("1 quarantined")
            : qsTr("%1 quarantined").arg(
                Models.RuntimeDiagnostics.cleanupQuarantinedCount)
        return pending + " · " + quarantined
    }

    function cleanupTone() {
        if (!Models.RuntimeDiagnostics.hasRuntimeHealth)
            return KodosiTheme.textSecondary
        return Models.RuntimeDiagnostics.cleanupState
                === Models.RuntimeDiagnostics.Healthy
            ? KodosiTheme.textPrimary
            : KodosiTheme.danger
    }

    function healthLabel(kind) {
        if (kind === "healthy")
            return qsTr("Healthy")
        if (kind === "unreachable")
            return qsTr("Unreachable")
        if (kind === "misconfigured")
            return qsTr("Misconfigured")
        return qsTr("Waiting for probe")
    }

    function healthTone(kind) {
        if (kind === "healthy")
            return KodosiTheme.textPrimary
        if (kind === "unreachable" || kind === "misconfigured")
            return KodosiTheme.danger
        return KodosiTheme.textSecondary
    }

    function openLogDirectory() {
        logOpenSequence += 1
        logOpenRequestId = "diagnostics.logs." + logOpenSequence
        logOpenError = ""
        Models.DesktopFiles.openLogDirectory(
            logOpenRequestId,
            Models.DesktopFiles.DiagnosticsLogDirectory)
    }

    function logSize() {
        const bytes = Models.ApplicationLog.sizeBytes
        if (bytes < 1024)
            return qsTr("%1 B").arg(bytes)
        if (bytes < 1024 * 1024)
            return qsTr("%1 KiB").arg(Math.round(bytes / 1024))
        return qsTr("%1 MiB").arg(
            (bytes / (1024 * 1024)).toFixed(1))
    }

    Connections {
        target: Models.DesktopFiles

        function onPathOpened(requestId, purpose) {
            if (requestId === root.logOpenRequestId
                    && purpose
                        === Models.DesktopFiles.DiagnosticsLogDirectory) {
                root.logOpenRequestId = ""
                root.logOpenError = ""
            }
        }

        function onOperationFailed(requestId, purpose, errorCode, message) {
            if (requestId === root.logOpenRequestId
                    && purpose
                        === Models.DesktopFiles.DiagnosticsLogDirectory) {
                root.logOpenRequestId = ""
                root.logOpenError = message
            }
        }
    }

    onOpened: closeButton.forceActiveFocus()

    background: Rectangle {
        color: KodosiTheme.surface
        radius: KodosiTheme.radiusLarge
    }

    contentItem: ColumnLayout {
        objectName: "panel.diagnostics"
        Accessible.id: objectName
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Kodosi Diagnostics")
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: KodosiTheme.surfaceElevated
            topLeftRadius: KodosiTheme.radiusLarge
            topRightRadius: KodosiTheme.radiusLarge

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing6
                anchors.rightMargin: KodosiTheme.spacing6
                spacing: KodosiTheme.spacing4

                KIcon {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    name: "diagnostics"
                    color: KodosiTheme.accent
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Diagnostics")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }

                PlainLabel {
                    visible: root.width >= 560
                    text: qsTr("Ctrl+Shift+D")
                    color: KodosiTheme.textSecondary
                    font.family: "monospace"
                    font.pixelSize: 11
                }

                KIconButton {
                    id: closeButton
                    objectName: "panel.diagnostics.close"
                    Accessible.id: objectName
                    glyph: "close"
                    Accessible.name: qsTr("Close Diagnostics")
                    onClicked: root.close()
                }
            }
        }

        KScrollView {
            id: diagnosticsScroll
            objectName: "panel.diagnostics.scroll"
            Accessible.id: objectName
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: diagnosticsScroll.availableWidth
                spacing: KodosiTheme.spacing7

                DiagnosticSection {
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing7
                    Layout.rightMargin: KodosiTheme.spacing7
                    Layout.topMargin: KodosiTheme.spacing7
                    title: qsTr("Runtime")

                    DiagnosticRow {
                        label: qsTr("Runtime")
                        value: Models.RuntimeDiagnostics.runtimeRunning
                            ? qsTr("running")
                            : qsTr("stopped")
                    }
                    DiagnosticRow {
                        label: qsTr("System ready")
                        value: Models.RuntimeDiagnostics.systemReady
                            ? qsTr("yes")
                            : qsTr("no")
                    }
                    DiagnosticRow {
                        label: qsTr("Connection format")
                        value: qsTr("v%1").arg(
                            Models.RuntimeDiagnostics.protocolVersion)
                    }
                    DiagnosticRow {
                        label: qsTr("Runtime contract")
                        value: Models.RuntimeDiagnostics.runtimeContract
                    }
                    DiagnosticRow {
                        objectName:
                            "panel.diagnostics.runtime.collaborationCleanup"
                        Accessible.id: objectName
                        label: qsTr("Collaboration cleanup")
                        value: root.cleanupValue()
                        tone: root.cleanupTone()
                    }
                    DiagnosticRow {
                        visible:
                            Models.RuntimeDiagnostics.runtimeError.length > 0
                        label: qsTr("Runtime error")
                        value: Models.RuntimeDiagnostics.runtimeError
                        tone: KodosiTheme.danger
                    }
                }

                DiagnosticSection {
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing7
                    Layout.rightMargin: KodosiTheme.spacing7
                    title: qsTr("Logging")

                    DiagnosticRow {
                        objectName: "panel.diagnostics.logging.health"
                        Accessible.id: objectName
                        label: qsTr("Log health")
                        value: Models.ApplicationLog.healthy
                            ? qsTr("ready")
                            : qsTr("unavailable")
                        tone: Models.ApplicationLog.healthy
                            ? KodosiTheme.textPrimary
                            : KodosiTheme.danger
                    }
                    DiagnosticRow {
                        label: qsTr("Current file")
                        value: Models.ApplicationLog.path
                    }
                    DiagnosticRow {
                        label: qsTr("Current size")
                        value: root.logSize()
                    }
                    DiagnosticRow {
                        label: qsTr("Retained archives")
                        value: String(Models.ApplicationLog.rotationCount)
                    }
                    DiagnosticRow {
                        visible: Models.ApplicationLog.lastError.length > 0
                        label: qsTr("Logging error")
                        value: Models.ApplicationLog.lastError
                        tone: KodosiTheme.danger
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: KodosiTheme.spacing2
                        spacing: KodosiTheme.spacing3

                        KButton {
                            objectName:
                                "panel.diagnostics.logging.openFolder"
                            Accessible.id: objectName
                            text: qsTr("Open Log Folder")
                            Accessible.name: text
                            enabled: Models.ApplicationLog.directoryAvailable
                                && root.logOpenRequestId.length === 0
                            onClicked: root.openLogDirectory()
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: qsTr("Persisted file logs are redacted and retained locally. Original stderr or journald output may remain developer output.")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                    }

                    Rectangle {
                        objectName: "panel.diagnostics.logging.error"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        visible: root.logOpenError.length > 0
                        implicitHeight: errorRow.implicitHeight
                            + KodosiTheme.spacing4
                        radius: KodosiTheme.radiusSmall
                        color: KodosiTheme.surfaceRaised
                        Accessible.name: qsTr("Open Log Folder error")
                        Accessible.description: root.logOpenError

                        RowLayout {
                            id: errorRow
                            anchors.fill: parent
                            anchors.margins: KodosiTheme.spacing2
                            spacing: KodosiTheme.spacing2

                            PlainLabel {
                                Layout.fillWidth: true
                                text: root.logOpenError
                                color: KodosiTheme.danger
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            KIconButton {
                                objectName:
                                    "panel.diagnostics.logging.error.dismiss"
                                Accessible.id: objectName
                                glyph: "close"
                                Accessible.name:
                                    qsTr("Dismiss Open Log Folder error")
                                onClicked: root.logOpenError = ""
                            }
                        }
                    }
                }

                DiagnosticSection {
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing7
                    Layout.rightMargin: KodosiTheme.spacing7
                    title: qsTr("Sessions")

                    DiagnosticRow {
                        label: qsTr("Sessions")
                        value: String(Models.Sessions.count)
                    }
                    DiagnosticRow {
                        label: qsTr("Inactive cleanup")
                        value: String(
                            Models.SessionActions.inactiveCleanupCount)
                    }
                    DiagnosticRow {
                        label: qsTr("Missions")
                        value: String(Models.Missions.count)
                    }
                    DiagnosticRow {
                        label: qsTr("Friends")
                        value: qsTr("%1 (%2 incoming)")
                            .arg(Models.People.friendCount)
                            .arg(Models.People.incomingCount)
                    }
                    DiagnosticRow {
                        label: qsTr("Devices")
                        value: String(Models.Devices.count)
                    }
                    DiagnosticRow {
                        label: qsTr("Trust pins")
                        value: String(Models.Trust.count)
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing7
                    Layout.rightMargin: KodosiTheme.spacing7
                    spacing: KodosiTheme.spacing3

                    PlainLabel {
                        text: qsTr("MCP SERVER HEALTH")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.0
                    }

                    Rectangle {
                        visible: Models.AgentGlobal.mcpServers.count === 0
                        Layout.fillWidth: true
                        implicitHeight: 68
                        radius: KodosiTheme.radiusMedium
                        color: KodosiTheme.surfaceRaised

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: KodosiTheme.spacing4
                            spacing: 2
                            PlainLabel {
                                text: qsTr("Waiting for the first probe")
                                color: KodosiTheme.textPrimary
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            PlainLabel {
                                Layout.fillWidth: true
                                text: qsTr("Health probes arrive every 60 seconds from the runtime's MCP sidecar. Empty until at least one user-scope server has been reachable.")
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Repeater {
                        model: Models.AgentGlobal.mcpServers

                        delegate: DiagnosticRow {
                            required property string vendor
                            required property string cwd
                            required property string scope
                            required property string name
                            required property string healthKind
                            required property string healthReason

                            Layout.fillWidth: true
                            label: name
                            detail: vendor + " · " + scope
                            value: healthReason.length > 0
                                ? root.healthLabel(healthKind)
                                    + ": " + healthReason
                                : root.healthLabel(healthKind)
                            tone: root.healthTone(healthKind)
                        }
                    }
                }

                Item { Layout.preferredHeight: KodosiTheme.spacing7 }
            }
        }
    }

    component DiagnosticSection: ColumnLayout {
        id: section

        required property string title
        default property alias content: sectionRows.data
        spacing: KodosiTheme.spacing3

        PlainLabel {
            text: section.title.toUpperCase()
            color: KodosiTheme.textSecondary
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 1.0
        }

        ColumnLayout {
            id: sectionRows
            Layout.fillWidth: true
            spacing: 1
        }
    }

    component DiagnosticRow: Item {
        id: row

        required property string label
        required property string value
        property string detail: ""
        property color tone: KodosiTheme.textPrimary

        Layout.fillWidth: true
        implicitHeight: detail.length > 0 ? 48 : 36
        Accessible.name: detail.length > 0
            ? label + ", " + detail + ", " + value
            : label + ", " + value
        Accessible.role: Accessible.StaticText

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: KodosiTheme.spacing2
            anchors.rightMargin: KodosiTheme.spacing2
            spacing: KodosiTheme.spacing4

            ColumnLayout {
                Layout.preferredWidth: Math.min(160, parent.width * 0.34)
                Layout.maximumWidth: Layout.preferredWidth
                spacing: 1
                PlainLabel {
                    Layout.fillWidth: true
                    text: row.label
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                PlainLabel {
                    Layout.fillWidth: true
                    visible: row.detail.length > 0
                    text: row.detail
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            KReadOnlyText {
                Layout.fillWidth: true
                text: row.value
                color: row.tone
                font.family: "monospace"
                font.pixelSize: 11
                wrapMode: TextEdit.Wrap
                activeFocusOnTab: false
                Accessible.name: text
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
}
