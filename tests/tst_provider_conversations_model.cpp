#include "models/DesktopSettings.hpp"
#include "models/ProviderConversationsModel.hpp"
#include "models/SessionCatalogModel.hpp"
#include "platform/DesktopFileIntegration.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <memory>
#include <functional>
#include <utility>

namespace {

class FakeDispatcher final : public kodosi::CommandDispatcher {
public:
    Result send(
        const kodosi::CommandLane lane,
        const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (lane != kodosi::CommandLane::AgentIntel
            || !document.isObject() || rejectNext) {
            rejectNext = false;
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        rawCommands.push_back(json.toByteArray());
        commands.push_back(document.object());
        return {};
    }

    QVector<QJsonObject> commands;
    QVector<QByteArray> rawCommands;
    bool rejectNext = false;
};

class FakeDirectoryPicker final : public kodosi::DirectoryPicker {
public:
    void open(Request request, Completion completion) override
    {
        lastRequest = std::move(request);
        pendingCompletion = std::move(completion);
        ++openCount;
    }

    void cancel() override
    {
        ++cancelCount;
        if (pendingCompletion) {
            auto completion = std::move(pendingCompletion);
            completion({.outcome = Outcome::Cancelled});
        }
    }

    void select(const QString& directory)
    {
        QVERIFY(pendingCompletion);
        auto completion = std::move(pendingCompletion);
        completion({
            .outcome = Outcome::Selected,
            .selectedDirectory = QUrl::fromLocalFile(directory),
        });
    }

    Request lastRequest;
    Completion pendingCompletion;
    int openCount = 0;
    int cancelCount = 0;
};

struct Fixture {
    QTemporaryDir root {
        QDir::current().filePath(
            QStringLiteral("provider-conversations-XXXXXX"))
    };
    QString project;
    QString otherProject;
    FakeDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    FakeDirectoryPicker* picker = nullptr;
    std::unique_ptr<kodosi::DesktopFileIntegration> desktopFiles;
    std::unique_ptr<kodosi::DesktopSettings> settings;
    std::unique_ptr<kodosi::ProviderConversationsModel> model;

    explicit Fixture(const qint64 timeoutMs = 15'000)
    {
        Q_ASSERT(root.isValid());
        project = QDir(root.path()).filePath(QStringLiteral("project"));
        otherProject =
            QDir(root.path()).filePath(QStringLiteral("other-project"));
        Q_ASSERT(QDir().mkpath(project));
        Q_ASSERT(QDir().mkpath(otherProject));
        project = QFileInfo(project).canonicalFilePath();
        otherProject = QFileInfo(otherProject).canonicalFilePath();

        auto pickerOwner = std::make_unique<FakeDirectoryPicker>();
        picker = pickerOwner.get();
        desktopFiles =
            std::make_unique<kodosi::DesktopFileIntegration>(
                sessions,
                std::move(pickerOwner),
                [](const QUrl&) { return true; });
        auto storage = std::make_unique<QSettings>(
            QDir(root.path()).filePath(QStringLiteral("settings.ini")),
            QSettings::IniFormat);
        settings =
            std::make_unique<kodosi::DesktopSettings>(std::move(storage));
        Q_ASSERT(settings->apply(
            QStringLiteral("Monospace"),
            14,
            0,
            1.1,
            10'000,
            false,
            true,
            project));
        model = std::make_unique<kodosi::ProviderConversationsModel>(
            dispatcher,
            *desktopFiles,
            *settings,
            timeoutMs);
    }

    void authenticate(
        const QString& user = QStringLiteral("me"),
        const quint64 epoch = 1)
    {
        model->ingestAuthEvent(
            QJsonDocument(QJsonObject {
                {QStringLiteral("type"), QStringLiteral("auth.ready")},
                {QStringLiteral("userId"), user},
                {QStringLiteral("accountEpoch"),
                 static_cast<double>(epoch)},
            }).toJson(QJsonDocument::Compact));
    }
};

QString nativeId(const int index)
{
    return QStringLiteral("01900000-0000-4000-8000-%1")
        .arg(index, 12, 10, QLatin1Char('0'));
}

QJsonObject catalogItem(
    const QString& provider,
    const QString& id,
    const QString& directory,
    const QString& title)
{
    return {
        {QStringLiteral("provider"), provider},
        {QStringLiteral("nativeConversationId"), id},
        {QStringLiteral("workingDirectory"), directory},
        {QStringLiteral("title"),
         title.isNull() ? QJsonValue(QJsonValue::Null)
                        : QJsonValue(title)},
        {QStringLiteral("createdAt"),
         QStringLiteral("2026-09-01T09:00:00Z")},
        {QStringLiteral("updatedAt"),
         QStringLiteral("2026-09-03T11:00:00Z")},
    };
}

QJsonObject catalogPage(
    const QJsonArray& items,
    const QString& nextCursor = {},
    const bool hasMore = false)
{
    qsizetype bytes = 0;
    for (const auto& item : items) {
        bytes += QJsonDocument(item.toObject())
                     .toJson(QJsonDocument::Compact)
                     .size();
    }
    return {
        {QStringLiteral("items"), items},
        {QStringLiteral("nextCursor"),
         nextCursor.isEmpty() ? QJsonValue(QJsonValue::Null)
                              : QJsonValue(nextCursor)},
        {QStringLiteral("hasMore"), hasMore},
        {QStringLiteral("responseBytes"), static_cast<double>(bytes)},
    };
}

QByteArray reply(
    const QString& requestId,
    const QJsonObject& payload,
    const QString& user = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"),
         QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), user},
        {QStringLiteral("accountEpoch"), static_cast<double>(epoch)},
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("payload"), payload},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject previewPage(const int count, const bool hasEarlier = false)
{
    QJsonArray entries;
    for (auto index = 0; index < count; ++index) {
        entries.append(QJsonObject {
            {QStringLiteral("role"),
             index % 2 == 0 ? QStringLiteral("user")
                            : QStringLiteral("assistant")},
            {QStringLiteral("content"),
             QStringLiteral("entry-%1").arg(index)},
            {QStringLiteral("toolName"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("timestamp"),
             QStringLiteral("2026-09-03T11:00:00Z")},
        });
    }
    return {
        {QStringLiteral("entries"), entries},
        {QStringLiteral("nextBeforeByte"),
         hasEarlier ? QJsonValue(100.0)
                    : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("sourceFileBytes"), 200.0},
        {QStringLiteral("readBytes"), 200.0},
        {QStringLiteral("sourceRecords"),
         static_cast<double>(std::min(count, 100))},
        {QStringLiteral("degradedReason"), QJsonValue(QJsonValue::Null)},
    };
}

QJsonObject lastCommand(
    const FakeDispatcher& dispatcher,
    const QString& type)
{
    for (auto it = dispatcher.commands.crbegin();
         it != dispatcher.commands.crend();
         ++it) {
        if (it->value(QStringLiteral("type")).toString() == type) {
            return *it;
        }
    }
    return {};
}

void deliverCatalog(
    Fixture& fixture,
    const QJsonArray& items,
    const QString& nextCursor = {},
    const bool hasMore = false,
    const QString& user = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    const auto command = lastCommand(
        fixture.dispatcher,
        QStringLiteral("agent.intel.discoverProviderConversations"));
    fixture.model->ingestAgentIntelEvent(reply(
        command.value(QStringLiteral("requestId")).toString(),
        catalogPage(items, nextCursor, hasMore),
        user,
        epoch));
}

} // namespace

class ProviderConversationsModelTest final : public QObject {
    Q_OBJECT

private slots:
    void dispatchesExactCatalogAndPreviewCommands();
    void rejectsMalformedCatalogReplies();
    void pagesDeduplicatesAndCapsRetention();
    void suppressesStaleGenerationAndAccountReplies();
    void fencesSelectionSearchAndFolderChanges();
    void timesOutAndRetries();
    void boundsPreviewAndRetainsLatestEntries();
    void reportsLocalPreviewTruncation();
    void correlatesNativeFolderPickerExactly();
    void qmlContractKeepsAuthorityNative();
};

void ProviderConversationsModelTest::
dispatchesExactCatalogAndPreviewCommands()
{
    Fixture fixture;
    fixture.authenticate();
    QVERIFY(fixture.model->open());
    QCOMPARE(fixture.dispatcher.commands.size(), 1);
    const auto command = fixture.dispatcher.commands.constFirst();
    const auto commandKeys = command.keys();
    QCOMPARE(
        QSet<QString>(commandKeys.cbegin(), commandKeys.cend()),
        QSet<QString>({
            QStringLiteral("type"),
            QStringLiteral("requestId"),
            QStringLiteral("provider"),
            QStringLiteral("workingDirectory"),
            QStringLiteral("limit"),
            QStringLiteral("maxBytes"),
        }));
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.discoverProviderConversations"));
    QCOMPARE(
        command.value(QStringLiteral("provider")).toString(),
        QStringLiteral("claude"));
    QCOMPARE(
        command.value(QStringLiteral("workingDirectory")).toString(),
        fixture.project);
    QCOMPARE(command.value(QStringLiteral("limit")).toInt(), 50);
    QCOMPARE(command.value(QStringLiteral("maxBytes")).toInt(), 131'072);
    QCOMPARE(
        QUuid(command.value(QStringLiteral("requestId")).toString()).version(),
        QUuid::UnixEpoch);
    QCOMPARE(
        fixture.dispatcher.rawCommands.constFirst(),
        QJsonDocument(command).toJson(QJsonDocument::Compact));

    const auto id = nativeId(1);
    deliverCatalog(
        fixture,
        {catalogItem(
            QStringLiteral("claude"),
            id,
            fixture.project,
            QStringLiteral("Release review"))});
    QCOMPARE(fixture.model->rowCount(), 1);
    const auto index = fixture.model->index(0);
    const auto presentationId = fixture.model
                                    ->data(
                                        index,
                                        kodosi::ProviderConversationsModel::
                                            PresentationIdRole)
                                    .toString();
    QVERIFY(presentationId.startsWith(QStringLiteral("conversation-")));
    QVERIFY(!presentationId.contains(id));
    QCOMPARE(
        fixture.model
            ->data(
                index,
                kodosi::ProviderConversationsModel::TitleRole)
            .toString(),
        QStringLiteral("Release review"));
    QVERIFY(!fixture.model->workingDirectoryLabel().contains(
        fixture.root.path()));

    const auto preview = lastCommand(
        fixture.dispatcher,
        QStringLiteral("agent.intel.readSessionConversation"));
    const auto previewKeys = preview.keys();
    QCOMPARE(
        QSet<QString>(previewKeys.cbegin(), previewKeys.cend()),
        QSet<QString>({
            QStringLiteral("type"),
            QStringLiteral("requestId"),
            QStringLiteral("agent"),
            QStringLiteral("cwd"),
            QStringLiteral("sessionId"),
            QStringLiteral("maxRecords"),
            QStringLiteral("maxBytes"),
        }));
    QCOMPARE(preview.value(QStringLiteral("agent")).toString(), QStringLiteral("claude"));
    QCOMPARE(preview.value(QStringLiteral("cwd")).toString(), fixture.project);
    QCOMPARE(preview.value(QStringLiteral("sessionId")).toString(), id);
    QCOMPARE(preview.value(QStringLiteral("maxRecords")).toInt(), 100);
    QCOMPARE(preview.value(QStringLiteral("maxBytes")).toInt(), 262'144);
    QCOMPARE(
        fixture.dispatcher.rawCommands.constLast(),
        QJsonDocument(preview).toJson(QJsonDocument::Compact));

    const auto target = fixture.model->resolveResumeTarget(presentationId);
    QVERIFY(target.has_value());
    QCOMPARE(target->nativeConversationId, id);
    QCOMPARE(target->workingDirectory, fixture.project);
    QCOMPARE(target->accountUserId, QStringLiteral("me"));
    QCOMPARE(target->accountEpoch, 1);
}

void ProviderConversationsModelTest::rejectsMalformedCatalogReplies()
{
    using State = kodosi::ProviderConversationsModel::State;
    const auto run = [](const std::function<void(QJsonObject&)>& mutate) {
        Fixture fixture;
        fixture.authenticate();
        QVERIFY(fixture.model->open());
        const auto command = fixture.dispatcher.commands.constFirst();
        auto payload = catalogPage({
            catalogItem(
                QStringLiteral("claude"),
                nativeId(1),
                fixture.project,
                QStringLiteral("Valid")),
        });
        mutate(payload);
        QSignalSpy decode(fixture.model.get(), &kodosi::ProviderConversationsModel::decodeError);
        fixture.model->ingestAgentIntelEvent(reply(
            command.value(QStringLiteral("requestId")).toString(),
            payload));
        QCOMPARE(fixture.model->state(), State::Failed);
        QCOMPARE(fixture.model->rowCount(), 0);
        QCOMPARE(decode.count(), 1);
    };

    run([](QJsonObject& payload) {
        payload.insert(QStringLiteral("extra"), true);
    });
    run([](QJsonObject& payload) {
        payload.insert(
            QStringLiteral("responseBytes"),
            payload.value(QStringLiteral("responseBytes")).toInt() + 1);
    });
    run([](QJsonObject& payload) {
        auto items = payload.value(QStringLiteral("items")).toArray();
        auto item = items.at(0).toObject();
        item.insert(QStringLiteral("nativeConversationId"), QStringLiteral("not-a-uuid"));
        items[0] = item;
        payload.insert(QStringLiteral("items"), items);
    });
    run([](QJsonObject& payload) {
        payload.insert(QStringLiteral("nextCursor"), QStringLiteral("cursor"));
        payload.insert(QStringLiteral("hasMore"), false);
    });
}

void ProviderConversationsModelTest::pagesDeduplicatesAndCapsRetention()
{
    Fixture fixture;
    fixture.authenticate();
    QVERIFY(fixture.model->open());

    int nextId = 1;
    for (auto page = 0; page < 11; ++page) {
        QJsonArray items;
        if (page == 1) {
            items.append(catalogItem(
                QStringLiteral("claude"),
                nativeId(1),
                fixture.project,
                QStringLiteral("Duplicate")));
        }
        while (items.size() < 50) {
            items.append(catalogItem(
                QStringLiteral("claude"),
                nativeId(nextId++),
                fixture.project,
                QStringLiteral("Conversation %1").arg(nextId)));
        }
        const auto cursor = QStringLiteral("cursor-%1").arg(page + 1);
        deliverCatalog(fixture, items, cursor, true);
        if (fixture.model->capped()) {
            break;
        }
        QVERIFY(fixture.model->loadMore());
        const auto command = fixture.dispatcher.commands.constLast();
        QCOMPARE(
            command.value(QStringLiteral("cursor")).toString(),
            cursor);
    }
    QCOMPARE(fixture.model->totalCount(), 500);
    QVERIFY(fixture.model->capped());
    QVERIFY(!fixture.model->hasMore());
    QVERIFY(!fixture.model->cappedNotice().isEmpty());
}

void ProviderConversationsModelTest::
suppressesStaleGenerationAndAccountReplies()
{
    Fixture fixture;
    fixture.authenticate();
    QVERIFY(fixture.model->open());
    const auto staleRequest =
        fixture.dispatcher.commands.constLast()
            .value(QStringLiteral("requestId"))
            .toString();

    fixture.model->setProvider(
        kodosi::ProviderConversationsModel::Provider::Copilot);
    QCOMPARE(fixture.dispatcher.commands.size(), 2);
    fixture.model->ingestAgentIntelEvent(reply(
        staleRequest,
        catalogPage({
            catalogItem(
                QStringLiteral("claude"),
                nativeId(1),
                fixture.project,
                QStringLiteral("Stale")),
        })));
    QCOMPARE(fixture.model->totalCount(), 0);

    const auto currentRequest =
        fixture.dispatcher.commands.constLast()
            .value(QStringLiteral("requestId"))
            .toString();
    fixture.authenticate(QStringLiteral("other"), 2);
    QCOMPARE(fixture.dispatcher.commands.size(), 3);
    fixture.model->ingestAgentIntelEvent(reply(
        currentRequest,
        catalogPage({
            catalogItem(
                QStringLiteral("copilot"),
                nativeId(2),
                fixture.project,
                QStringLiteral("Old account")),
        }),
        QStringLiteral("me"),
        1));
    QCOMPARE(fixture.model->totalCount(), 0);

    fixture.model->resetRuntimeAuthority();
    fixture.model->ingestAgentIntelEvent(reply(
        fixture.dispatcher.commands.constLast()
            .value(QStringLiteral("requestId"))
            .toString(),
        catalogPage({}),
        QStringLiteral("other"),
        2));
    QCOMPARE(fixture.model->totalCount(), 0);
}

void ProviderConversationsModelTest::fencesSelectionSearchAndFolderChanges()
{
    Fixture fixture;
    fixture.authenticate();
    QVERIFY(fixture.model->open());
    deliverCatalog(
        fixture,
        {
            catalogItem(
                QStringLiteral("claude"),
                nativeId(1),
                fixture.project,
                QStringLiteral("First")),
            catalogItem(
                QStringLiteral("claude"),
                nativeId(2),
                fixture.project,
                QStringLiteral("Second")),
        });
    const auto firstPresentation = fixture.model->selectedPresentationId();
    const auto firstPreviewRequest =
        fixture.dispatcher.commands.constLast()
            .value(QStringLiteral("requestId"))
            .toString();
    const auto secondPresentation = fixture.model
        ->data(
            fixture.model->index(1),
            kodosi::ProviderConversationsModel::PresentationIdRole)
        .toString();
    QVERIFY(fixture.model->select(secondPresentation));
    const auto secondPreviewRequest =
        fixture.dispatcher.commands.constLast()
            .value(QStringLiteral("requestId"))
            .toString();
    fixture.model->ingestAgentIntelEvent(
        reply(firstPreviewRequest, previewPage(1)));
    QCOMPARE(fixture.model->preview()->rowCount(), 0);
    fixture.model->ingestAgentIntelEvent(
        reply(secondPreviewRequest, previewPage(1)));
    QCOMPARE(fixture.model->preview()->rowCount(), 1);

    fixture.model->setSearchText(QStringLiteral("First"));
    QCOMPARE(fixture.model->rowCount(), 1);
    QVERIFY(!fixture.model->canResume());
    QVERIFY(!fixture.model->resolveResumeTarget(secondPresentation));
    fixture.model->setSearchText({});
    QVERIFY(fixture.model->canResume());
    QVERIFY(firstPresentation != secondPresentation);

    QVERIFY(fixture.model->reload());
    const auto staleCatalogRequest =
        fixture.dispatcher.commands.constLast()
            .value(QStringLiteral("requestId"))
            .toString();
    const auto catalogCount = fixture.dispatcher.commands.size();
    QVERIFY(fixture.model->browseFolder());
    fixture.picker->select(fixture.otherProject);
    QVERIFY(fixture.dispatcher.commands.size() > catalogCount);
    fixture.model->ingestAgentIntelEvent(reply(
        staleCatalogRequest,
        catalogPage({
            catalogItem(
                QStringLiteral("claude"),
                nativeId(3),
                fixture.project,
                QStringLiteral("Stale folder")),
        })));
    QCOMPARE(fixture.model->totalCount(), 0);
    QVERIFY(!fixture.model->resolveResumeTarget(secondPresentation));
}

void ProviderConversationsModelTest::timesOutAndRetries()
{
    Fixture fixture(0);
    fixture.authenticate();
    QVERIFY(fixture.model->open());
    QTRY_COMPARE(
        fixture.model->state(),
        kodosi::ProviderConversationsModel::State::Failed);
    QCOMPARE(fixture.dispatcher.commands.size(), 1);
    QVERIFY(fixture.model->retry());
    QCOMPARE(fixture.dispatcher.commands.size(), 2);
}

void ProviderConversationsModelTest::boundsPreviewAndRetainsLatestEntries()
{
    Fixture fixture;
    fixture.authenticate();
    QVERIFY(fixture.model->open());
    deliverCatalog(
        fixture,
        {catalogItem(
            QStringLiteral("claude"),
            nativeId(1),
            fixture.project,
            QStringLiteral("Preview"))});
    const auto requestId =
        fixture.dispatcher.commands.constLast()
            .value(QStringLiteral("requestId"))
            .toString();
    fixture.model->ingestAgentIntelEvent(
        reply(requestId, previewPage(60, true)));
    QCOMPARE(fixture.model->preview()->rowCount(), 50);
    QCOMPARE(
        fixture.model->preview()
            ->data(
                fixture.model->preview()->index(0),
                kodosi::ProviderConversationPreviewModel::ContentRole)
            .toString(),
        QStringLiteral("entry-10"));
    QVERIFY(!fixture.model->previewNotice().isEmpty());
}

void ProviderConversationsModelTest::reportsLocalPreviewTruncation()
{
    Fixture fixture;
    fixture.authenticate();
    QVERIFY(fixture.model->open());
    deliverCatalog(
        fixture,
        {catalogItem(
            QStringLiteral("claude"),
            nativeId(1),
            fixture.project,
            QStringLiteral("Preview"))});
    const auto requestId =
        fixture.dispatcher.commands.constLast()
            .value(QStringLiteral("requestId"))
            .toString();
    fixture.model->ingestAgentIntelEvent(
        reply(requestId, previewPage(60, false)));

    QCOMPARE(fixture.model->preview()->rowCount(), 50);
    QVERIFY(!fixture.model->previewNotice().isEmpty());
}

void ProviderConversationsModelTest::correlatesNativeFolderPickerExactly()
{
    Fixture fixture;
    fixture.authenticate();
    QVERIFY(fixture.model->open());
    QVERIFY(fixture.model->browseFolder());
    QVERIFY(fixture.model->browsing());
    QCOMPARE(fixture.picker->openCount, 1);
    QCOMPARE(
        fixture.picker->lastRequest.transientParent,
        QPointer<QWindow> {});

    fixture.desktopFiles->directoryPicked(
        QStringLiteral("unrelated"),
        kodosi::DesktopFileIntegration::Purpose::ResumeAgentWork,
        fixture.otherProject);
    QVERIFY(fixture.model->browsing());
    QCOMPARE(
        fixture.model->workingDirectoryLabel(),
        QFileInfo(fixture.project).fileName());

    QVERIFY(fixture.model->cancelFolderBrowse());
    QCOMPARE(fixture.picker->cancelCount, 1);
    QVERIFY(!fixture.model->browsing());
}

void ProviderConversationsModelTest::qmlContractKeepsAuthorityNative()
{
    QFile modal(QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral(
            "/src/qml/Workbench/ResumeAgentWorkModal.qml"));
    QVERIFY(modal.open(QIODevice::ReadOnly));
    const auto qml = modal.readAll();
    for (const auto& contract : {
             QByteArrayLiteral("objectName: \"panel.resumeAgentWork\""),
             QByteArrayLiteral(
                 "objectName: \"resumeAgentWork.provider.claude\""),
             QByteArrayLiteral(
                 "objectName: \"resumeAgentWork.provider.copilot\""),
             QByteArrayLiteral(
                 "objectName: \"resumeAgentWork.folder.browse\""),
             QByteArrayLiteral(
                 "objectName: \"resumeAgentWork.search\""),
             QByteArrayLiteral(
                 "objectName: \"resumeAgentWork.catalog.retry\""),
             QByteArrayLiteral(
                 "? \"resumeAgentWork.empty\""),
             QByteArrayLiteral(
                 ": \"resumeAgentWork.noMatches\""),
             QByteArrayLiteral(
                 "\"resumeAgentWork.cappedNotice\""),
             QByteArrayLiteral(
                 "\"resumeAgentWork.preview.retry\""),
             QByteArrayLiteral(
                 "objectName: \"resumeAgentWork.preview.empty\""),
             QByteArrayLiteral(
                 "objectName: \"resumeAgentWork.creation.error\""),
             QByteArrayLiteral(
                 "objectName: \"resumeAgentWork.resume\""),
             QByteArrayLiteral(
                 "Models.SessionActions.lastCreateRequestId"),
             QByteArrayLiteral(
                 "Models.SessionActions.isCreatePending("),
             QByteArrayLiteral("interval: 120"),
             QByteArrayLiteral("Models.SessionActions.createResumed("),
             QByteArrayLiteral("readonly property bool compact:"),
         }) {
        QVERIFY2(qml.contains(contract), contract.constData());
    }
    QVERIFY(!qml.contains("nativeConversationId"));
    QVERIFY(!qml.contains("agent.intel."));
    QVERIFY(!qml.contains("session.create"));
    QVERIFY(!qml.contains("resumeAgentWork.sessionName"));
    QVERIFY(!qml.contains("/home/"));
    QVERIFY(!qml.contains("/Users/"));

    QFile sidebar(QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/Workbench/SessionSidebar.qml"));
    QVERIFY(sidebar.open(QIODevice::ReadOnly));
    const auto sidebarQml = sidebar.readAll();
    QVERIFY(sidebarQml.contains(
        "objectName: \"sidebar.session.resumeAgentWork\""));
    QVERIFY(sidebarQml.contains(
        "onClicked: root.resumeAgentWorkRequested()"));
}

QTEST_MAIN(ProviderConversationsModelTest)

#include "tst_provider_conversations_model.moc"
