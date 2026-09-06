import QtQuick
import Kodosi.Models 1.0 as Models

KTextField {
    objectName: "missions.task.due.date"
    Accessible.id: objectName
    visible: Models.MissionActions.taskDraftHasDueAt
    implicitWidth: 120
    placeholderText: qsTr("YYYY-MM-DD")
    maximumLength: 10
    text: Models.MissionActions.taskDraftDueDateText
    Accessible.name: qsTr("Task due date")
    Accessible.description: Models.MissionActions.taskDraftDueDateError
    onTextEdited: Models.MissionActions.setTaskDraftDueDateText(text)
}
