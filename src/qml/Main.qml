import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

ApplicationWindow {
    id: window

    readonly property bool modalOpen: startup.visible || details.opened || history.opened

    function openCreate(folder) {
        Models.Workspace.createSession("", folder || Models.DesktopSettings.effectiveWorkingDirectory, "", "");
    }

    color: KodosiTheme.canvas
    height: 800
    minimumHeight: 560
    minimumWidth: 820
    objectName: "window.main"
    title: qsTr("Kodosi")
    visible: true
    width: 1240

    header: Rectangle {
        color: KodosiTheme.surface
        height: 54

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            spacing: 8

            Image {
                Layout.rightMargin: 16
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                source: "assets/kodosi-logo-dark.png"
                fillMode: Image.PreserveAspectFit
                Accessible.name: qsTr("Kodosi")
            }
            NavTab {
                accessibleId: "sessions"
                iconName: "terminal"
                selected: Models.DesktopState.activeView === 0
                title: qsTr("Sessions")

                onClicked: Models.DesktopState.activeView = 0
            }
            NavTab {
                accessibleId: "missions"
                iconName: "mission"
                selected: Models.DesktopState.activeView === 1
                title: qsTr("Missions")

                onClicked: Models.DesktopState.activeView = 1
            }
            NavTab {
                accessibleId: "people"
                iconName: "people"
                selected: Models.DesktopState.activeView === 2
                title: qsTr("People")

                onClicked: Models.DesktopState.activeView = 2
            }
            Item {
                Layout.fillWidth: true
            }
            KButton {
                Accessible.id: objectName
                objectName: "header.sign-in"
                text: qsTr("Sign in")
                visible: !Models.Workspace.signedIn
                variant: "primary"
                onClicked: { Models.DesktopState.activeView = 3; Models.Workspace.login(); }
            }
            KIconButton {
                Accessible.id: objectName
                Accessible.name: qsTr("Settings")
                objectName: "header.settings"
                glyph: "settings"
                onClicked: Models.DesktopState.activeView = 3
            }
        }
    }

    onClosing: close => {
        close.accepted = false;
        window.hide();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: errorRow.implicitHeight + 16
            color: KodosiTheme.surfaceRaised
            visible: Models.Workspace.error.length > 0 || Models.DesktopState.lastError.length > 0 || Models.DesktopFiles.errorMessage.length > 0

            RowLayout {
                id: errorRow

                anchors.fill: parent
                anchors.margins: 8

                PlainLabel {
                    Layout.fillWidth: true
                    color: KodosiTheme.danger
                    text: Models.Workspace.error || Models.DesktopState.lastError || Models.DesktopFiles.errorMessage
                    wrapMode: Text.WordWrap
                }
                KIconButton {
                    Accessible.id: objectName
                    Accessible.name: qsTr("Dismiss error")
                    glyph: "close"
                    objectName: "window.error.dismiss"

                    onClicked: {
                        Models.Workspace.clearError();
                        Models.DesktopState.clearError();
                        Models.DesktopFiles.clearError();
                    }
                }
            }
        }
        StackLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            currentIndex: Models.DesktopState.activeView

            RowLayout {
                spacing: 0

                SessionSidebar {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 230
                    visible: Models.DesktopState.sidebarOpen

                    onDetailsRequested: sessionId => details.openSession(sessionId)
                    onNewSessionRequested: folder => window.openCreate(folder)
                    onHistoryRequested: history.openModal()
                }
                TerminalStage {
                    id: stage

                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    interactionEnabled: !window.modalOpen && Models.DesktopState.activeView === 0
                    sidebarOpen: Models.DesktopState.sidebarOpen

                    onInspectSessionRequested: (sessionId, name) => details.openSession(sessionId)
                    onNewSessionRequested: window.openCreate("")
                    onShareSessionRequested: (sessionId, name) => details.openSession(sessionId)
                    onShowSidebarRequested: Models.DesktopState.sidebarOpen = true
                }
            }
            MissionsView {
            }
            PeopleView {
            }
            SettingsView {
            }
        }
    }
    Connections {
        function onDirectoryPicked(purpose, path) {
            if (purpose === "new") {
                Models.DesktopSettings.setWorkingDirectory(path);
                window.openCreate(path);
            }
        }

        target: Models.DesktopFiles
    }
    Connections {
        function onSessionActivated(sessionId) {
            window.show();
            window.raise();
            stage.scheduleTerminalFocus();
        }

        target: Models.Workspace
    }
    SessionDetails {
        id: details
    }
    HistoryView {
        id: history
    }
    StartupOverlay {
        id: startup
    }
    Shortcut {
        enabled: !window.modalOpen
        sequence: "Ctrl+Shift+N"

        onActivated: window.openCreate()
    }
    Shortcut {
        enabled: !window.modalOpen
        sequence: "Ctrl+Shift+B"

        onActivated: Models.DesktopState.sidebarOpen = !Models.DesktopState.sidebarOpen
    }
    Shortcut {
        enabled: !window.modalOpen && Models.DesktopState.activeView === 0
        sequence: "Ctrl+Shift+F"

        onActivated: Models.DesktopState.toggleFocusForSelectedSession()
    }
    Shortcut {
        enabled: !window.modalOpen && Models.DesktopState.activeView === 0
        sequence: "Ctrl+Shift+W"

        onActivated: Models.Workspace.closeView(Models.DesktopState.selectedSessionId)
    }
}
