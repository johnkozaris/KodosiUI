import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    required property string title
    required property string caption
    required property string accessibleId
    property string iconName: "command"
    property bool selected: false
    property int badgeCount: 0

    objectName: "header.tab." + accessibleId
    Accessible.id: objectName
    Accessible.name: title
    Accessible.description: caption
    Accessible.selected: selected

    implicitHeight: 38
    leftPadding: 12
    rightPadding: 12
    topPadding: 4
    bottomPadding: 4

    contentItem: RowLayout {
        spacing: KodosiTheme.spacing2

        KIcon {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            name: root.iconName
            color: root.selected
                ? KodosiTheme.accent
                : KodosiTheme.textSecondary
        }

        ColumnLayout {
            spacing: 0

            Label {
                text: root.caption.toUpperCase()
                color: KodosiTheme.textSecondary
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 1.0
            }

            Label {
                text: root.title
                color: root.selected
                    ? KodosiTheme.textPrimary
                    : KodosiTheme.textSecondary
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            visible: root.badgeCount > 0
            Layout.preferredWidth: badgeLabel.implicitWidth + 10
            Layout.preferredHeight: 17
            radius: 8
            color: root.selected ? KodosiTheme.accent : KodosiTheme.surface

            Label {
                id: badgeLabel
                anchors.centerIn: parent
                text: root.badgeCount > 99 ? qsTr("99+") : root.badgeCount
                color: root.selected
                    ? KodosiTheme.accentForeground
                    : KodosiTheme.textPrimary
                font.pixelSize: 9
                font.weight: Font.Bold
            }
        }
    }

    background: Rectangle {
        radius: KodosiTheme.radiusSmall
        color: root.selected
            ? KodosiTheme.surfaceSelected
            : root.hovered ? KodosiTheme.surfaceElevated : "transparent"
        border.width: root.activeFocus ? 1 : 0
        border.color: KodosiTheme.focusRing

        Behavior on color {
            ColorAnimation { duration: KodosiTheme.motionFast }
        }
    }
}
