#include "models/AppearanceModel.hpp"
#include "models/DesktopSettings.hpp"
#include "models/ProviderConversationsModel.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionCatalogModel.hpp"
#include "platform/DesktopFileIntegration.hpp"

#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <memory>

namespace {

class RecordingDispatcher final : public kodosi::CommandDispatcher {
public:
    Result send(kodosi::CommandLane, QByteArrayView json) override
    {
        commands.append(QJsonDocument::fromJson(json.toByteArray()).object());
        return {};
    }

    QJsonObject last(const QString& type) const
    {
        for (auto it = commands.crbegin(); it != commands.crend(); ++it) {
            if (it->value(QStringLiteral("type")).toString() == type)
                return *it;
        }
        return {};
    }

    QVector<QJsonObject> commands;
};

class UnusedDirectoryPicker final : public kodosi::DirectoryPicker {
public:
    void open(Request, Completion completion) override
    {
        completion({.outcome = Outcome::Cancelled});
    }
    void cancel() override {}
};

QByteArray accountEvent(QJsonObject event)
{
    event.insert(QStringLiteral("authority"), QStringLiteral("accountContext"));
    event.insert(QStringLiteral("accountUserId"), QStringLiteral("me"));
    event.insert(QStringLiteral("accountEpoch"), 1);
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

} // namespace

class ResumeModalTest final : public QObject {
    Q_OBJECT
private slots:
    void correlatedResumeSuccessClosesProductionModal();
};

void ResumeModalTest::correlatedResumeSuccessClosesProductionModal()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto project = QDir(temporary.path()).filePath(QStringLiteral("project"));
    const auto qml = QDir(temporary.path()).filePath(QStringLiteral("qml"));
    QVERIFY(QDir().mkpath(project));
    QVERIFY(QDir().mkpath(qml));

    // Load the unmodified shipping modal, controls, and theme together. Only the
    // module directory is test-local; no QML behavior or model surface is mocked.
    const QDir source(QStringLiteral(KODOSI_SOURCE_DIR "/src/qml"));
    const QStringList files {
        QStringLiteral("Workbench/ResumeAgentWorkModal.qml"),
        QStringLiteral("Theme/KodosiTheme.qml"),
        QStringLiteral("Controls/KPopover.qml"),
        QStringLiteral("Controls/KButton.qml"),
        QStringLiteral("Controls/KFocusIndicator.qml"),
        QStringLiteral("Controls/KIcon.qml"),
        QStringLiteral("Controls/KIconButton.qml"),
        QStringLiteral("Controls/KSegmentedBar.qml"),
        QStringLiteral("Controls/KTextField.qml"),
        QStringLiteral("Controls/KBusyIndicator.qml"),
        QStringLiteral("Controls/KScrollView.qml"),
        QStringLiteral("Controls/KScrollBar.qml"),
        QStringLiteral("Controls/PlainLabel.qml"),
    };
    for (const auto& relative : files) {
        QVERIFY2(QFile::copy(source.filePath(relative),
            QDir(qml).filePath(QFileInfo(relative).fileName())), qPrintable(relative));
    }
    QFile directory(QDir(qml).filePath(QStringLiteral("qmldir")));
    QVERIFY(directory.open(QIODevice::WriteOnly));
    QVERIFY(directory.write("singleton KodosiTheme 1.0 KodosiTheme.qml\n") > 0);
    directory.close();

    RecordingDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::DesktopFileIntegration desktopFiles(sessions,
        std::make_unique<UnusedDirectoryPicker>(), [](const QUrl&) { return false; });
    kodosi::DesktopSettings settings(std::make_unique<QSettings>(
        QDir(temporary.path()).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat));
    QVERIFY(settings.apply(QStringLiteral("Monospace"), 14, 0, 1.1, 10'000, false, true, project));
    kodosi::AppearanceModel appearance(std::make_unique<QSettings>(
        QDir(temporary.path()).filePath(QStringLiteral("appearance.ini")), QSettings::IniFormat),
        QDBusConnection(QStringLiteral("resume-modal-test")),
        {.colorScheme = [] { return Qt::ColorScheme::Dark; },
         .setColorScheme = [](Qt::ColorScheme) {}, .unsetColorScheme = [] {}}, false);
    appearance.injectReducedMotionForTesting(true);
    kodosi::ProviderConversationsModel conversations(dispatcher, desktopFiles, settings);
    kodosi::SessionActions actions(dispatcher, sessions, settings);
    actions.setProviderConversationResumeResolver(&conversations);
    const auto auth = QByteArrayLiteral(
        "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}");
    sessions.ingestAuthEvent(auth);
    actions.ingestAuthEvent(auth);
    conversations.ingestAuthEvent(auth);

    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "SessionActions", &actions);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "ProviderConversations", &conversations);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "DesktopFiles", &desktopFiles);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "Appearance", &appearance);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import "."
        ApplicationWindow {
            width: 1100; height: 800; visible: true
            property alias modal: resume
            ResumeAgentWorkModal { id: resume }
        }
    )", QUrl::fromLocalFile(QDir(qml).filePath(QStringLiteral("journey.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(window);
    auto* modal = root->property("modal").value<QObject*>();
    QVERIFY(modal);
    QVERIFY(QMetaObject::invokeMethod(modal, "openModal"));
    QTRY_VERIFY(modal->property("opened").toBool());

    const auto catalogRequest = dispatcher.last(QStringLiteral("agent.intel.discoverProviderConversations"));
    QVERIFY(!catalogRequest.isEmpty());
    const QJsonObject conversation {
        {QStringLiteral("provider"), QStringLiteral("claude")},
        {QStringLiteral("nativeConversationId"), QStringLiteral("01900000-0000-4000-8000-000000000123")},
        {QStringLiteral("workingDirectory"), project},
        {QStringLiteral("title"), QStringLiteral("Review the release")},
        {QStringLiteral("createdAt"), QStringLiteral("2026-09-01T09:00:00Z")},
        {QStringLiteral("updatedAt"), QStringLiteral("2026-09-03T11:00:00Z")},
    };
    conversations.ingestAgentIntelEvent(accountEvent({
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), catalogRequest.value(QStringLiteral("requestId"))},
        {QStringLiteral("payload"), QJsonObject {
            {QStringLiteral("items"), QJsonArray {conversation}},
            {QStringLiteral("nextCursor"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("hasMore"), false},
            {QStringLiteral("responseBytes"), QJsonDocument(conversation).toJson(QJsonDocument::Compact).size()},
        }},
    }));
    QCOMPARE(conversations.rowCount(), 1);
    QVERIFY(conversations.canResume());
    auto* resume = modal->findChild<QQuickItem*>(QStringLiteral("resumeAgentWork.resume"));
    auto* cancel = modal->findChild<QQuickItem*>(QStringLiteral("resumeAgentWork.cancel"));
    QVERIFY(resume);
    QVERIFY(cancel);
    QTRY_VERIFY(resume->isVisible() && resume->isEnabled());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        resume->mapToScene(QPointF(resume->width() / 2, resume->height() / 2)).toPoint());
    QTRY_VERIFY(modal->property("resumePending").toBool());
    QVERIFY(cancel->isEnabled());
    const auto requestId = actions.lastCreateRequestId();
    QCOMPARE(modal->property("resumeRequestId").toString(), requestId);
    QVERIFY(actions.isCreatePending(requestId));
    QVariant closedWhilePending;
    QVERIFY(QMetaObject::invokeMethod(modal, "closeModal", Q_RETURN_ARG(QVariant, closedWhilePending)));
    QVERIFY(closedWhilePending.toBool());
    QTRY_VERIFY(!modal->property("visible").toBool());
    QVERIFY(actions.isCreatePending(requestId));
    QVERIFY(QMetaObject::invokeMethod(modal, "openModal"));
    QTRY_VERIFY(modal->property("opened").toBool());
    QVERIFY(modal->property("resumePending").toBool());
    QCOMPARE(modal->property("resumeRequestId").toString(), requestId);
    QVERIFY(!resume->isEnabled());

    const auto incarnation = QStringLiteral("01900000-0000-7000-8000-000000000124");
    auto created = QJsonObject {
        {QStringLiteral("type"), QStringLiteral("session.created")},
        {QStringLiteral("requestId"), QStringLiteral("01900000-0000-7000-8000-000000000999")},
        {QStringLiteral("sessionId"), QStringLiteral("resumed")},
        {QStringLiteral("runtimeIncarnationId"), incarnation},
    };
    QSignalSpy resolved(&actions, &kodosi::SessionActions::sessionCreationResolved);
    actions.ingestSessionEvent(accountEvent(created));
    QVERIFY(modal->property("resumePending").toBool());
    QVERIFY(modal->property("opened").toBool());
    QVERIFY(resolved.isEmpty());

    created.insert(QStringLiteral("requestId"), requestId);
    actions.ingestSessionEvent(accountEvent(created));
    QVERIFY(modal->property("opened").toBool());
    QVERIFY(modal->property("resumePending").toBool());
    QVERIFY(resolved.isEmpty());
    const auto projected = accountEvent({
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"), QJsonObject {
            {QStringLiteral("kind"), QStringLiteral("local")},
            {QStringLiteral("id"), QStringLiteral("resumed")},
            {QStringLiteral("incarnationId"), incarnation},
            {QStringLiteral("name"), QStringLiteral("Review the release")},
            {QStringLiteral("project"), project},
            {QStringLiteral("mode"), QStringLiteral("normal")},
            {QStringLiteral("status"), QStringLiteral("active")},
            {QStringLiteral("recovery"), QStringLiteral("live")},
            {QStringLiteral("scope"), QStringLiteral("justMe")},
            {QStringLiteral("access"), QStringLiteral("approve")},
        }},
    });
    sessions.ingestSessionEvent(projected);
    actions.ingestSessionEvent(projected);
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.at(0).at(0).toString(), requestId);
    QVERIFY(actions.lastError().isEmpty());
    QVERIFY(!actions.isCreatePending(requestId));
    QTRY_VERIFY(!modal->property("opened").toBool());
    QTRY_VERIFY(!modal->property("visible").toBool());
    QVERIFY(!modal->property("resumePending").toBool());
    QVERIFY(modal->property("resumeRequestId").toString().isEmpty());
    QCOMPARE(conversations.state(), kodosi::ProviderConversationsModel::State::Dormant);

    QVERIFY(QMetaObject::invokeMethod(modal, "openModal"));
    QTRY_VERIFY(modal->property("opened").toBool());
    QVERIFY(!modal->property("resumePending").toBool());
    QTRY_VERIFY(cancel->isEnabled());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        cancel->mapToScene(QPointF(cancel->width() / 2, cancel->height() / 2)).toPoint());
    QTRY_VERIFY(!modal->property("visible").toBool());
}

QTEST_MAIN(ResumeModalTest)
#include "tst_resume_modal.moc"
