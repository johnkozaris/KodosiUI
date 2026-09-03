pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "panel.resumeAgentWork"

    property bool resumePending: false
    readonly property bool compact: width < 850 || height < 570

    parent: Overlay.overlay
    width: Math.min(900, parent ? parent.width - 20 : 900)
    height: Math.min(620, parent ? parent.height - 20 : 620)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Models.SessionActions.creating
        ? Popup.NoAutoClose
        : Popup.CloseOnEscape

    function openModal() {
        resumePending = false
        sessionName.clear()
        Models.SessionActions.clearError()
        Models.ProviderConversations.open()
        open()
    }

    function closeModal() {
        if (Models.SessionActions.creating)
            return false
        resumePending = false
        close()
        return true
    }

    function submitResume() {
        if (!Models.ProviderConversations.canResume
                || sessionName.text.trim().length === 0
                || Models.SessionActions.creating)
            return
        resumePending = Models.SessionActions.createResumed(
            sessionName.text,
            Models.ProviderConversations.selectedPresentationId)
    }

    onOpened: {
        Models.ProviderConversations.open()
        focusTimer.restart()
    }
    onClosed: {
        focusTimer.stop()
        resumePending = false
        Models.ProviderConversations.close()
    }

    Timer {
        id: focusTimer
        interval: 120
        repeat: false
        onTriggered: searchField.forceActiveFocus(Qt.PopupFocusReason)
    }

    Connections {
        target: Models.SessionActions

        function onSessionCreated() {
            if (root.resumePending)
                root.closeModal()
        }

        function onStateChanged() {
            if (root.resumePending && !Models.SessionActions.creating
                    && Models.SessionActions.lastError.length > 0)
                root.resumePending = false
        }
    }

    Overlay.modal: Rectangle { color: KodosiTheme.overlayDim }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 10
            anchors.leftMargin: 7
            radius: KodosiTheme.radiusModal
            color: KodosiTheme.shadow
            opacity: 0.46
        }
        Rectangle {
            anchors.fill: parent
            radius: KodosiTheme.radiusModal
            color: KodosiTheme.canvas
        }
    }

    contentItem: ColumnLayout {
        objectName: "panel.resumeAgentWork"
        Accessible.id: objectName
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Resume Agent Work")
        Accessible.description:
            qsTr("Continue a provider conversation in a new Kodosi session")
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            color: KodosiTheme.surfaceElevated
            topLeftRadius: KodosiTheme.radiusModal
            topRightRadius: KodosiTheme.radiusModal

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 12
                spacing: KodosiTheme.spacing3

                KIcon {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    name: "history"
                    color: KodosiTheme.accent
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    PlainLabel {
                        Layout.fillWidth: true
                        text: qsTr("Resume Agent Work")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: qsTr(
                            "Continue a provider conversation in a new session.")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                KIconButton {
                    id: closeButton
                    objectName: "resumeAgentWork.close"
                    Accessible.id: objectName
                    glyph: "close"
                    Accessible.name: qsTr("Close Resume Agent Work")
                    enabled: !Models.SessionActions.creating
                    onClicked: root.closeModal()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight:
                Models.ProviderConversations.state
                    === Models.ProviderConversations.Ready
                    && Models.ProviderConversations.error.length > 0
                ? (root.compact ? 132 : 124)
                : (root.compact ? 112 : 104)
            color: KodosiTheme.surface

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 10
                anchors.bottomMargin: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: KodosiTheme.spacing3

                    KSegmentedBar {
                        Layout.preferredWidth: root.compact ? 190 : 220
                        Layout.preferredHeight: 34

                        KButton {
                            objectName: "resumeAgentWork.provider.claude"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            compact: true
                            checkable: true
                            checked: Models.ProviderConversations.provider
                                === Models.ProviderConversations.Claude
                            tonalSelection: true
                            text: qsTr("Claude")
                            Accessible.name: text
                            Accessible.selected: checked
                            onClicked: Models.ProviderConversations.provider =
                                Models.ProviderConversations.Claude
                        }

                        KButton {
                            objectName: "resumeAgentWork.provider.copilot"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            compact: true
                            checkable: true
                            checked: Models.ProviderConversations.provider
                                === Models.ProviderConversations.Copilot
                            tonalSelection: true
                            text: qsTr("Copilot")
                            Accessible.name: text
                            Accessible.selected: checked
                            onClicked: Models.ProviderConversations.provider =
                                Models.ProviderConversations.Copilot
                        }
                    }

                    KIcon {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        name: "folder"
                        color: KodosiTheme.textSecondary
                    }

                    PlainLabel {
                        objectName: "resumeAgentWork.folder"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        text: Models.ProviderConversations
                            .workingDirectoryLabel
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Accessible.name:
                            qsTr("Project folder %1").arg(text)
                    }

                    KButton {
                        objectName: "resumeAgentWork.folder.browse"
                        Accessible.id: objectName
                        text: Models.ProviderConversations.browsing
                            ? qsTr("Choosing")
                            : qsTr("Browse")
                        compact: true
                        enabled: !Models.ProviderConversations.browsing
                            && !Models.DesktopFiles.busy
                        Accessible.name:
                            qsTr("Browse for provider conversation project folder")
                        onClicked:
                            Models.ProviderConversations.browseFolder()
                    }
                }

                KTextField {
                    id: searchField
                    objectName: "resumeAgentWork.search"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Filter conversations")
                    Accessible.name: placeholderText
                    maximumLength: 256
                    text: Models.ProviderConversations.searchText
                    onTextEdited:
                        Models.ProviderConversations.searchText = text
                }

                PlainLabel {
                    objectName: "resumeAgentWork.folder.error"
                    Accessible.id: objectName
                    visible: Models.ProviderConversations.state
                        === Models.ProviderConversations.Ready
                        && Models.ProviderConversations.error.length > 0
                    Layout.fillWidth: true
                    text: Models.ProviderConversations.error
                    color: KodosiTheme.danger
                    font.pixelSize: 9
                    elide: Text.ElideRight
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

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            KBusyIndicator {
                anchors.centerIn: parent
                visible: Models.ProviderConversations.loading
                    && Models.ProviderConversations.totalCount === 0
                running: visible
                Accessible.name:
                    qsTr("Loading %1 conversations").arg(
                        Models.ProviderConversations.providerLabel)
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(430, parent.width - 40)
                visible: Models.ProviderConversations.state
                    === Models.ProviderConversations.Failed
                    && Models.ProviderConversations.totalCount === 0
                spacing: KodosiTheme.spacing3

                KIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    name: "warning"
                    color: KodosiTheme.danger
                }
                PlainLabel {
                    objectName: "resumeAgentWork.catalog.error"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    text: qsTr("Could not read conversations")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }
                PlainLabel {
                    Layout.fillWidth: true
                    text: Models.ProviderConversations.error
                    color: KodosiTheme.textSecondary
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.name: text
                }
                KButton {
                    objectName: "resumeAgentWork.catalog.retry"
                    Accessible.id: objectName
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Retry")
                    Accessible.name: text
                    onClicked: Models.ProviderConversations.retry()
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(430, parent.width - 40)
                visible: Models.ProviderConversations.state
                    === Models.ProviderConversations.Ready
                    && Models.ProviderConversations.count === 0
                spacing: KodosiTheme.spacing2

                KIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    name: "history"
                    color: KodosiTheme.textSecondary
                }
                PlainLabel {
                    objectName: Models.ProviderConversations.totalCount === 0
                        ? "resumeAgentWork.empty"
                        : "resumeAgentWork.noMatches"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    text: Models.ProviderConversations.totalCount === 0
                        ? qsTr("No %1 conversations").arg(
                            Models.ProviderConversations.providerLabel)
                        : qsTr("No matching conversations")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.name: text
                }
                PlainLabel {
                    Layout.fillWidth: true
                    text: Models.ProviderConversations.totalCount === 0
                        ? qsTr("Choose another project folder or provider.")
                        : qsTr("Try a different title, date, or provider.")
                    color: KodosiTheme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                KButton {
                    objectName: "resumeAgentWork.noMatches.loadMore"
                    Accessible.id: objectName
                    visible: Models.ProviderConversations.hasMore
                    Layout.alignment: Qt.AlignHCenter
                    text: Models.ProviderConversations.loadingMore
                        ? qsTr("Loading")
                        : qsTr("Load more")
                    enabled: !Models.ProviderConversations.loadingMore
                    Accessible.name: text
                    onClicked: Models.ProviderConversations.loadMore()
                }

                PlainLabel {
                    objectName: "resumeAgentWork.noMatches.cappedNotice"
                    Accessible.id: objectName
                    visible: Models.ProviderConversations.capped
                    Layout.fillWidth: true
                    text: Models.ProviderConversations.cappedNotice
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 9
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.name: text
                }

                PlainLabel {
                    objectName: "resumeAgentWork.noMatches.loadMoreError"
                    Accessible.id: objectName
                    visible: Models.ProviderConversations
                        .loadMoreError.length > 0
                    Layout.fillWidth: true
                    text: Models.ProviderConversations.loadMoreError
                    color: KodosiTheme.danger
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.name: text
                }

                KButton {
                    objectName: "resumeAgentWork.noMatches.retryLoadMore"
                    Accessible.id: objectName
                    visible: Models.ProviderConversations
                        .loadMoreError.length > 0
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Retry loading")
                    Accessible.name: text
                    onClicked:
                        Models.ProviderConversations.retryLoadMore()
                }
            }

            RowLayout {
                anchors.fill: parent
                visible: Models.ProviderConversations.state
                    === Models.ProviderConversations.Ready
                    && Models.ProviderConversations.count > 0
                spacing: 0

                Rectangle {
                    Layout.preferredWidth: root.compact ? 292 : 330
                    Layout.fillHeight: true
                    color: KodosiTheme.surface

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        ListView {
                            id: conversationList
                            objectName: "resumeAgentWork.conversations"
                            Accessible.id: objectName
                            Accessible.role: Accessible.List
                            Accessible.name: qsTr("Provider conversations")
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: Models.ProviderConversations
                            spacing: 3
                            topMargin: 8
                            bottomMargin: 8
                            leftMargin: 8
                            rightMargin: 8
                            boundsBehavior: Flickable.StopAtBounds

                            delegate: KButton {
                                id: conversationButton
                                required property string presentationId
                                required property string title
                                required property string date
                                required property string providerLabel
                                required property string summary
                                required property string accessibleId
                                readonly property bool conversationSelected:
                                    Models.ProviderConversations
                                        .selectedPresentationId
                                        === presentationId

                                objectName:
                                    "resumeAgentWork.conversation."
                                    + accessibleId
                                Accessible.id: objectName
                                width: ListView.view.width - 16
                                implicitHeight: root.compact ? 56 : 64
                                checkable: true
                                checked: conversationSelected
                                tonalSelection: true
                                variant: "quiet"
                                contentLeftAligned: true
                                Accessible.role: Accessible.ListItem
                                Accessible.name: title
                                Accessible.description:
                                    providerLabel + ". "
                                    + (date.length > 0
                                       ? date
                                       : qsTr("Date unavailable"))
                                Accessible.selected: conversationSelected
                                onClicked:
                                    Models.ProviderConversations.select(
                                        presentationId)

                                contentItem: ColumnLayout {
                                    spacing: 2
                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: conversationButton.title
                                        color: KodosiTheme.textPrimary
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                        elide: Text.ElideRight
                                    }
                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: conversationButton.date.length > 0
                                            ? conversationButton.date
                                            : qsTr("Date unavailable")
                                        color: KodosiTheme.textSecondary
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Rectangle {
                            visible: Models.ProviderConversations.hasMore
                                || Models.ProviderConversations.capped
                                || Models.ProviderConversations
                                    .loadMoreError.length > 0
                            Layout.fillWidth: true
                            implicitHeight: footerColumn.implicitHeight + 12
                            color: KodosiTheme.surfaceElevated

                            ColumnLayout {
                                id: footerColumn
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 4

                                KButton {
                                    objectName:
                                        "resumeAgentWork.loadMore"
                                    Accessible.id: objectName
                                    visible:
                                        Models.ProviderConversations.hasMore
                                    Layout.alignment: Qt.AlignHCenter
                                    text: Models.ProviderConversations
                                        .loadingMore
                                        ? qsTr("Loading")
                                        : qsTr("Load more")
                                    compact: true
                                    enabled: !Models.ProviderConversations
                                        .loadingMore
                                    Accessible.name: text
                                    onClicked:
                                        Models.ProviderConversations.loadMore()
                                }

                                PlainLabel {
                                    objectName:
                                        "resumeAgentWork.cappedNotice"
                                    Accessible.id: objectName
                                    visible:
                                        Models.ProviderConversations.capped
                                    Layout.fillWidth: true
                                    text: Models.ProviderConversations
                                        .cappedNotice
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 9
                                    wrapMode: Text.Wrap
                                    Accessible.name: text
                                }

                                RowLayout {
                                    visible: Models.ProviderConversations
                                        .loadMoreError.length > 0
                                    Layout.fillWidth: true
                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: Models.ProviderConversations
                                            .loadMoreError
                                        color: KodosiTheme.danger
                                        font.pixelSize: 9
                                        wrapMode: Text.Wrap
                                    }
                                    KButton {
                                        objectName:
                                            "resumeAgentWork.loadMore.retry"
                                        Accessible.id: objectName
                                        text: qsTr("Retry")
                                        compact: true
                                        Accessible.name: text
                                        onClicked: Models
                                            .ProviderConversations
                                            .retryLoadMore()
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: KodosiTheme.seam
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: KodosiTheme.canvas

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: root.compact ? 12 : 16
                        spacing: 8

                        PlainLabel {
                            objectName: "resumeAgentWork.detail.title"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            text: Models.ProviderConversations.selectedTitle
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            maximumLineCount: 2
                            wrapMode: Text.Wrap
                            elide: Text.ElideRight
                            Accessible.name: text
                        }

                        PlainLabel {
                            objectName: "resumeAgentWork.detail.metadata"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            text: Models.ProviderConversations
                                .selectedProviderLabel
                                + (Models.ProviderConversations
                                    .selectedDate.length > 0
                                   ? " · "
                                     + Models.ProviderConversations
                                         .selectedDate
                                   : "")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Accessible.name: text
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: KodosiTheme.seam
                        }

                        KBusyIndicator {
                            Layout.alignment: Qt.AlignCenter
                            visible: Models.ProviderConversations
                                .previewLoading
                            running: visible
                            Accessible.name: qsTr("Loading preview")
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: Models.ProviderConversations
                                .previewError.length > 0
                            spacing: KodosiTheme.spacing2

                            Item { Layout.fillHeight: true }
                            PlainLabel {
                                objectName:
                                    "resumeAgentWork.preview.error"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                text: Models.ProviderConversations
                                    .previewError
                                color: KodosiTheme.danger
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                Accessible.name: text
                            }
                            KButton {
                                objectName:
                                    "resumeAgentWork.preview.retry"
                                Accessible.id: objectName
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("Retry preview")
                                Accessible.name: text
                                onClicked: Models.ProviderConversations
                                    .retryPreview()
                            }
                            Item { Layout.fillHeight: true }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: !Models.ProviderConversations
                                .previewLoading
                                && Models.ProviderConversations
                                    .previewError.length === 0
                                && Models.ProviderConversations
                                    .preview.count === 0
                            spacing: KodosiTheme.spacing2

                            Item { Layout.fillHeight: true }
                            KIcon {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                name: "document"
                                color: KodosiTheme.textSecondary
                            }
                            PlainLabel {
                                objectName: "resumeAgentWork.preview.empty"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                text: Models.ProviderConversations
                                    .previewNotice.length > 0
                                    ? Models.ProviderConversations
                                        .previewNotice
                                    : qsTr("No preview available")
                                color: KodosiTheme.textSecondary
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                Accessible.name: text
                            }
                            Item { Layout.fillHeight: true }
                        }

                        KScrollView {
                            objectName: "resumeAgentWork.preview"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: Models.ProviderConversations
                                .preview.count > 0
                                && !Models.ProviderConversations
                                    .previewLoading
                                && Models.ProviderConversations
                                    .previewError.length === 0
                            Accessible.name: qsTr("Conversation preview")

                            Column {
                                width: parent.width
                                spacing: 8

                                PlainLabel {
                                    width: parent.width
                                    visible: Models.ProviderConversations
                                        .previewNotice.length > 0
                                    text: Models.ProviderConversations
                                        .previewNotice
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 9
                                    wrapMode: Text.Wrap
                                    Accessible.name: text
                                }

                                Repeater {
                                    model: Models.ProviderConversations.preview

                                    Rectangle {
                                        id: previewCard
                                        required property string role
                                        required property string content
                                        required property string toolName
                                        required property string timestamp

                                        width: parent.width
                                        implicitHeight:
                                            previewEntry.implicitHeight + 16
                                        color: KodosiTheme.surfaceElevated
                                        radius: KodosiTheme.radiusSmall

                                        ColumnLayout {
                                            id: previewEntry
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 3
                                            PlainLabel {
                                                text: previewCard.role
                                                color: KodosiTheme.accent
                                                font.pixelSize: 9
                                                font.weight: Font.Bold
                                            }
                                            PlainLabel {
                                                Layout.fillWidth: true
                                                text: previewCard.content
                                                color:
                                                    KodosiTheme.textPrimary
                                                font.pixelSize: 10
                                                wrapMode: Text.Wrap
                                                Accessible.name: text
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? 76 : 72
            color: KodosiTheme.surfaceElevated
            bottomLeftRadius: KodosiTheme.radiusModal
            bottomRightRadius: KodosiTheme.radiusModal

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: KodosiTheme.spacing3

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    KTextField {
                        id: sessionName
                        objectName: "resumeAgentWork.sessionName"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        placeholderText: qsTr("New session name")
                        Accessible.name: placeholderText
                        maximumLength: 128
                        enabled: !Models.SessionActions.creating
                        onAccepted: root.submitResume()
                    }

                    PlainLabel {
                        objectName: "resumeAgentWork.creation.error"
                        Accessible.id: objectName
                        visible: Models.SessionActions.lastError.length > 0
                            && !Models.SessionActions.creating
                        Layout.fillWidth: true
                        text: Models.SessionActions.lastError
                        color: KodosiTheme.danger
                        font.pixelSize: 9
                        elide: Text.ElideRight
                        Accessible.name: text
                    }
                }

                KButton {
                    objectName: "resumeAgentWork.cancel"
                    Accessible.id: objectName
                    text: qsTr("Cancel")
                    variant: "quiet"
                    enabled: !Models.SessionActions.creating
                    Accessible.name: text
                    onClicked: root.closeModal()
                }

                KButton {
                    objectName: "resumeAgentWork.resume"
                    Accessible.id: objectName
                    text: Models.SessionActions.creating
                        ? qsTr("Resuming")
                        : qsTr("Resume")
                    variant: "primary"
                    enabled: Models.ProviderConversations.canResume
                        && sessionName.text.trim().length > 0
                        && !Models.SessionActions.creating
                    Accessible.name: text
                    onClicked: root.submitResume()
                }
            }
        }
    }
}
