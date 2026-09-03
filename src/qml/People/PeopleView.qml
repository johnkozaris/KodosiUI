pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root

    objectName: "surface.missions"
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Missions")

    property string selectedMissionId
    property string selectedMissionName
    property bool createOpen: false
    readonly property bool compact: width < 1000
    signal signInRequested()

    function openMission(missionId, missionName) {
        if (!Models.MissionDetail.openMission(missionId))
            return
        selectedMissionId = missionId
        selectedMissionName = missionName
        createOpen = false
    }

    function closeMission() {
        Models.MissionDetail.closeMission()
        selectedMissionId = ""
        selectedMissionName = ""
    }

    Component.onCompleted: {
        selectedMissionId = Models.MissionDetail.missionId
        if (selectedMissionId.length > 0)
            selectedMissionName = qsTr("Mission")
    }

    Connections {
        target: Models.MissionDetail

        function onStateChanged() {
            if (Models.MissionDetail.missionId.length === 0) {
                root.selectedMissionId = ""
                root.selectedMissionName = ""
            } else if (root.selectedMissionId.length === 0) {
                root.selectedMissionId = Models.MissionDetail.missionId
                root.selectedMissionName = qsTr("Mission")
            }
        }
    }

    Connections {
        target: Models.MissionActions

        function onMissionCreated(missionId) {
            root.createOpen = false
        }
    }

    Rectangle {
        anchors.fill: parent
        color: KodosiTheme.canvas
    }

    AuthGate {
        anchors.fill: parent
        visible: !Models.AuthState.signedIn
        accessibleId: "auth.gate.missions"
        title: qsTr("Sign in to use Missions")
        detail: qsTr("My Agents stays local.")
        onSignInRequested: root.signInRequested()
    }

    RowLayout {
        anchors.fill: parent
        visible: Models.AuthState.signedIn
        spacing: 0

        Rectangle {
            visible: !root.compact || root.selectedMissionId.length === 0
            Layout.preferredWidth: root.compact ? -1 : 232
            Layout.fillWidth: root.compact
            Layout.fillHeight: true
            color: KodosiTheme.surface

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    Layout.leftMargin: KodosiTheme.spacing5
                    Layout.rightMargin: KodosiTheme.spacing3
                    spacing: KodosiTheme.spacing2

                    PlainLabel {
                        Layout.fillWidth: true
                        text: qsTr("Missions")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    KIconButton {
                        objectName: "missions.refresh"
                        Accessible.id: objectName
                        visible: Models.Missions.canRefresh
                        glyph: "refresh"
                        Accessible.name: qsTr("Refresh Missions")
                        onClicked: Models.Missions.refresh()
                    }

                    KIconButton {
                        objectName: "missions.create.open"
                        Accessible.id: objectName
                        glyph: "plus"
                        Accessible.name: qsTr("New Mission")
                        onClicked: root.createOpen = !root.createOpen
                    }
                }

                Rectangle {
                    visible: root.createOpen
                        || Models.MissionActions.hasCreateMissionDraft
                    Layout.fillWidth: true
                    implicitHeight: createColumn.implicitHeight + 16
                    color: KodosiTheme.surfaceRaised

                    ColumnLayout {
                        id: createColumn
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing3
                        spacing: KodosiTheme.spacing2

                        KTextField {
                            objectName: "missions.create.name"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            placeholderText: qsTr("Mission name")
                            text: Models.MissionActions.createMissionName
                            maximumLength: 128
                            onTextEdited:
                                Models.MissionActions.setCreateMissionName(text)
                        }

                        KTextField {
                            objectName: "missions.create.slug"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            placeholderText: qsTr("mission-slug")
                            text: Models.MissionActions.createMissionSlug
                            maximumLength: 64
                            onTextEdited:
                                Models.MissionActions.setCreateMissionSlug(text)
                            onAccepted: {
                                if (Models.MissionActions
                                    .createMissionCanSubmit)
                                    Models.MissionActions.createMission()
                            }
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignRight

                            KButton {
                                objectName: "missions.create.cancel"
                                Accessible.id: objectName
                                text: qsTr("Cancel")
                                variant: "quiet"
                                compact: true
                                enabled:
                                    Models.MissionActions.createMissionCanDiscard
                                onClicked: {
                                    if (Models.MissionActions
                                            .discardCreateMission())
                                        root.createOpen = false
                                }
                            }

                            KButton {
                                objectName: "missions.create.submit"
                                Accessible.id: objectName
                                text: qsTr("Create")
                                variant: "primary"
                                compact: true
                                enabled:
                                    Models.MissionActions.createMissionCanSubmit
                                onClicked:
                                    Models.MissionActions.createMission()
                            }
                        }

                        PlainLabel {
                            visible:
                                Models.MissionActions.createMissionError.length
                                > 0
                            Layout.fillWidth: true
                            text: Models.MissionActions.createMissionError
                            color: KodosiTheme.danger
                            wrapMode: Text.Wrap
                        }

                        RowLayout {
                            visible:
                                Models.MissionActions.createMissionCanCheck
                                || Models.MissionActions.createMissionCanRetry
                            Layout.alignment: Qt.AlignRight

                            KButton {
                                objectName: "missions.create.check"
                                Accessible.id: objectName
                                visible:
                                    Models.MissionActions.createMissionCanCheck
                                text: qsTr("Check")
                                compact: true
                                onClicked:
                                    Models.MissionActions.checkCreateMission()
                            }

                            KButton {
                                objectName: "missions.create.retry"
                                Accessible.id: objectName
                                visible:
                                    Models.MissionActions.createMissionCanRetry
                                text: qsTr("Retry")
                                compact: true
                                onClicked:
                                    Models.MissionActions.retryCreateMission()
                            }
                        }
                    }
                }

                PlainLabel {
                    visible: Models.Missions.lastError.length > 0
                    Layout.fillWidth: true
                    Layout.margins: KodosiTheme.spacing3
                    text: Models.Missions.lastError
                    color: KodosiTheme.danger
                    wrapMode: Text.Wrap
                }

                ListView {
                    id: missionList
                    objectName: "missions.directory"
                    Accessible.id: objectName
                    Accessible.name: qsTr("Missions")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Models.Missions
                    clip: true
                    spacing: 2
                    topMargin: KodosiTheme.spacing2
                    bottomMargin: KodosiTheme.spacing2

                    delegate: KButton {
                        required property string missionId
                        required property string name
                        readonly property string missionName: name

                        width: ListView.view.width
                            - KodosiTheme.spacing3 * 2
                        x: KodosiTheme.spacing3
                        implicitHeight: 42
                        objectName: "missions.item." + missionId
                        Accessible.id: objectName
                        text: name
                        variant: "quiet"
                        tonalSelection: true
                        checkable: true
                        checked: root.selectedMissionId === missionId
                        contentLeftAligned: true
                        onClicked:
                            root.openMission(missionId, missionName)
                    }
                }

                PlainLabel {
                    objectName: "missions.directory.empty"
                    Accessible.id: objectName
                    visible: Models.Missions.authorityState
                        === Models.Missions.Loaded
                        && Models.Missions.count === 0
                        && !root.createOpen
                    Layout.alignment: Qt.AlignCenter
                    text: qsTr("No Missions")
                    color: KodosiTheme.textSecondary
                }
            }
        }

        Rectangle {
            visible: !root.compact
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: KodosiTheme.seam
        }

        MissionDetailView {
            visible: root.selectedMissionId.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            missionId: root.selectedMissionId
            missionName: root.selectedMissionName
            onCloseRequested: root.closeMission()
        }

        Item {
            visible: !root.compact
                && root.selectedMissionId.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            PlainLabel {
                anchors.centerIn: parent
                text: qsTr("Select a Mission")
                color: KodosiTheme.textSecondary
            }
        }
    }
}
