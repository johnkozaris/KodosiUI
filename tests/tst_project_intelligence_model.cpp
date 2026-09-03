#include "models/AgentAutoModeRulesModel.hpp"
#include "models/AgentConversationModel.hpp"
#include "models/AgentSessionIntelModel.hpp"
#include "models/ExternalDiscoveryModel.hpp"
#include "models/ProjectIntelligenceModel.hpp"
#include "models/SessionCatalogModel.hpp"
#include "platform/DesktopFileIntegration.hpp"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <utility>

namespace {

constexpr auto accountEpoch = 37;
const auto firstIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000101");
const auto secondIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000102");
const auto authorityIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000103");

class ProjectDispatcher final : public kodosi::CommandDispatcher {
public:
    Result send(kodosi::CommandLane, QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (!document.isObject()) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("invalid JSON"),
            });
        }
        commands.push_back(document.object());
        if (std::exchange(rejectNext, false)) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("command rejected"),
            });
        }
        return {};
    }

    [[nodiscard]] const QJsonObject& last() const
    {
        return commands.constLast();
    }

    [[nodiscard]] int count(const QString& type) const
    {
        return std::count_if(
            commands.cbegin(),
            commands.cend(),
            [&type](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString() == type;
            });
    }

    [[nodiscard]] QJsonObject lastOfType(const QString& type) const
    {
        for (auto index = commands.size(); index > 0; --index) {
            if (commands.at(index - 1)
                    .value(QStringLiteral("type")).toString()
                == type) {
                return commands.at(index - 1);
            }
        }
        return {};
    }

    QVector<QJsonObject> commands;
    bool rejectNext = false;
};

class NullDirectoryPicker final : public kodosi::DirectoryPicker {
public:
    void open(Request, Completion) override {}
    void cancel() override {}
};

struct DesktopHarness {
    QList<QUrl> openedUrls;
    bool openerResult = true;
    std::unique_ptr<kodosi::DesktopFileIntegration> integration;

    explicit DesktopHarness(kodosi::SessionCatalogModel& sessions)
    {
        integration = std::make_unique<kodosi::DesktopFileIntegration>(
            sessions,
            std::make_unique<NullDirectoryPicker>(),
            [this](const QUrl& url) {
                openedUrls.push_back(url);
                return openerResult;
            });
    }
};

QString uuidV7()
{
    return QUuid::createUuidV7().toString(QUuid::WithoutBraces);
}

#if defined(Q_OS_LINUX)
QString createBoundArtifact(
    const QString& root,
    const int descriptor)
{
    const auto directory = QDir(root).filePath(
        QStringLiteral("kodosi-open-") + uuidV7());
    if (!QDir().mkpath(directory)
        || ::chmod(QFile::encodeName(directory).constData(), 0700) != 0) {
        return {};
    }
    const auto artifact = QDir(directory).filePath(QStringLiteral("handoff"));
    const auto target = QStringLiteral("/proc/%1/fd/%2")
        .arg(QCoreApplication::applicationPid())
        .arg(descriptor);
    return ::symlink(
               QFile::encodeName(target).constData(),
               QFile::encodeName(artifact).constData())
            == 0
        ? artifact
        : QString {};
}
#endif

QByteArray auth()
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
    }).toJson(QJsonDocument::Compact);
}

QByteArray reply(const QString& requestId, const QJsonValue& payload)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("payload"), payload},
    }).toJson(QJsonDocument::Compact);
}

QByteArray runtimeError(
    const QString& requestId,
    const QString& failureKind = QStringLiteral("deterministic"),
    const QJsonValue& mutationId = QJsonValue(QJsonValue::Null),
    const bool reconciliationRequired = false,
    const QString& message = QStringLiteral("selection is stale"))
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("agent.intel.error")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("message"), message},
        {QStringLiteral("failureKind"), failureKind},
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("reconciliationRequired"), reconciliationRequired},
    }).toJson(QJsonDocument::Compact);
}

QString requestId(const QJsonObject& command)
{
    return command.value(QStringLiteral("requestId")).toString();
}

QString itemId(kodosi::PresentationListModel* model, const int row)
{
    return model->data(
                    model->index(row),
                    kodosi::PresentationListModel::ItemIdRole)
        .toString();
}

QJsonObject source(
    const QString& token,
    const QString& kind,
    const QString& label,
    const QJsonArray& activeSessionIds = {})
{
    QJsonValue agent = QJsonValue::Null;
    if (kind == QStringLiteral("claudeArchive")) {
        agent = QStringLiteral("claude");
    } else if (kind == QStringLiteral("copilotArchive")) {
        agent = QStringLiteral("copilot");
    }
    return {
        {QStringLiteral("selectionToken"), token},
        {QStringLiteral("sourceKind"), kind},
        {QStringLiteral("agent"), agent},
        {QStringLiteral("label"), label},
        {QStringLiteral("sessionCount"), activeSessionIds.size()},
        {QStringLiteral("memoryCount"), 1},
        {QStringLiteral("activeSessionIds"), activeSessionIds},
    };
}

QJsonObject sourcePage(
    const QJsonArray& items,
    const QString& nextCursor = {},
    const bool hasMore = false)
{
    return {
        {QStringLiteral("items"), items},
        {QStringLiteral("nextCursor"),
         nextCursor.isEmpty()
             ? QJsonValue::Null
             : QJsonValue(nextCursor)},
        {QStringLiteral("hasMore"), hasMore},
        {QStringLiteral("responseBytes"), 128},
    };
}

QJsonObject memorySummary(
    const QString& readToken,
    const QString& openToken,
    const QString& copyToken,
    const QString& filename = QStringLiteral("MEMORY.md"),
    const QJsonValue& memoryType = QStringLiteral("project"))
{
    return {
        {QStringLiteral("readSelectionToken"), readToken},
        {QStringLiteral("openSelectionToken"), openToken},
        {QStringLiteral("copySelectionToken"), copyToken},
        {QStringLiteral("filename"), filename},
        {QStringLiteral("memoryType"), memoryType},
    };
}

QJsonObject agentSummary(
    const QString& detailToken,
    const QString& openToken,
    const QString& name = QStringLiteral("reviewer"),
    const QString& description = QStringLiteral("Reviews changes"),
    const QJsonValue& model = QStringLiteral("sonnet"),
    const QJsonArray& tools = QJsonArray {QStringLiteral("Read")})
{
    return {
        {QStringLiteral("detailSelectionToken"), detailToken},
        {QStringLiteral("openSelectionToken"), openToken},
        {QStringLiteral("target"), QStringLiteral("claude")},
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), description},
        {QStringLiteral("model"), model},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("errorCount"), 0},
    };
}

QJsonObject projectSession(
    const QString& sessionId,
    const QString& incarnation)
{
    return {
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("runtimeIncarnationId"), incarnation},
        {QStringLiteral("title"), QStringLiteral("Current work")},
        {QStringLiteral("agent"), QStringLiteral("claude")},
        {QStringLiteral("status"), QStringLiteral("working")},
        {QStringLiteral("mode"), QJsonValue::Null},
        {QStringLiteral("startedAt"), QJsonValue::Null},
        {QStringLiteral("updatedAt"), QJsonValue::Null},
        {QStringLiteral("sizeBytes"), QJsonValue::Null},
        {QStringLiteral("hostType"), QJsonValue::Null},
        {QStringLiteral("transcriptAvailable"), true},
    };
}

QJsonObject projectSnapshot(
    const QString& replacementToken,
    const QString& kind,
    const QString& label,
    const QJsonArray& sessions = {},
    const QJsonArray& memories = {},
    const QJsonArray& agents = {},
    const QJsonArray& settings = {},
    const QJsonArray& customizations = {})
{
    QJsonValue agent = QJsonValue::Null;
    if (kind == QStringLiteral("claudeArchive")) {
        agent = QStringLiteral("claude");
    } else if (kind == QStringLiteral("copilotArchive")) {
        agent = QStringLiteral("copilot");
    }
    return {
        {QStringLiteral("sourceSelectionToken"), replacementToken},
        {QStringLiteral("sourceKind"), kind},
        {QStringLiteral("agent"), agent},
        {QStringLiteral("label"), label},
        {QStringLiteral("sessions"), sessions},
        {QStringLiteral("memories"), memories},
        {QStringLiteral("customAgents"), agents},
        {QStringLiteral("customizations"), customizations},
        {QStringLiteral("settings"), settings},
    };
}

QJsonObject customization(
    const QString& kind,
    const QString& name)
{
    return {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("name"), name},
        {QStringLiteral("scope"), QStringLiteral("project")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("status"), QStringLiteral("loaded")},
        {QStringLiteral("description"), QStringLiteral("Description")},
        {QStringLiteral("statusMessage"), QJsonValue::Null},
    };
}

QJsonObject memoryDetail(
    const QString& token,
    const QString& content = QStringLiteral("body"),
    const QString& canonicalCwd = QStringLiteral("/repo"),
    const QString& projectSlug = QStringLiteral("-repo"),
    const QString& filename = QStringLiteral("MEMORY.md"),
    const QJsonValue& memoryType = QStringLiteral("project"))
{
    return {
        {QStringLiteral("content"), content},
        {QStringLiteral("selectionToken"), token},
        {QStringLiteral("canonicalCwd"), canonicalCwd},
        {QStringLiteral("projectSlug"), projectSlug},
        {QStringLiteral("filename"), filename},
        {QStringLiteral("memoryType"), memoryType},
    };
}

QJsonObject agentDetail(
    const QString& token,
    const QJsonArray& disallowedTools = QJsonArray {QStringLiteral("Bash")},
    const QString& name = QStringLiteral("reviewer"),
    const QString& description = QStringLiteral("Reviews changes"),
    const QJsonValue& model = QStringLiteral("sonnet"),
    const QJsonArray& tools = QJsonArray {QStringLiteral("Read")},
    const QString& prompt = QStringLiteral("Review exactly."))
{
    return {
        {QStringLiteral("selectionToken"), token},
        {QStringLiteral("target"), QStringLiteral("claude")},
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), description},
        {QStringLiteral("model"), model},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("disallowedTools"), disallowedTools},
        {QStringLiteral("errors"), QJsonArray {}},
        {QStringLiteral("frontmatter"),
         QStringLiteral("name: ") + name},
        {QStringLiteral("prompt"), prompt},
    };
}

QJsonObject settingsNode(
    const quint32 nodeId,
    const QString& key,
    const QString& value)
{
    return {
        {QStringLiteral("nodeId"), static_cast<qint64>(nodeId)},
        {QStringLiteral("parentId"), QJsonValue::Null},
        {QStringLiteral("depth"), 0},
        {QStringLiteral("key"), key},
        {QStringLiteral("kind"), QStringLiteral("string")},
        {QStringLiteral("childCount"), 0},
        {QStringLiteral("stringValue"), value},
        {QStringLiteral("integerValue"), QJsonValue::Null},
        {QStringLiteral("numberValue"), QJsonValue::Null},
        {QStringLiteral("booleanValue"), QJsonValue::Null},
    };
}

QJsonObject settingsBundle(
    const QString& agent,
    const QString& scope,
    const QJsonArray& nodes)
{
    QJsonObject scopes {
        {QStringLiteral("managed"), QJsonValue::Null},
        {QStringLiteral("user"), QJsonValue::Null},
        {QStringLiteral("project"), QJsonValue::Null},
        {QStringLiteral("local"), QJsonValue::Null},
    };
    scopes.insert(
        scope,
        QJsonObject {{QStringLiteral("nodes"), nodes}});
    return {
        {QStringLiteral("agent"), agent},
        {QStringLiteral("settings"), scopes},
    };
}

QJsonObject autoRead(
    const QString& token,
    const QString& revision,
    const QJsonObject& rules)
{
    return {
        {QStringLiteral("targetToken"), token},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("rules"), rules},
    };
}

QJsonObject autoReceipt(
    const QString& mutationId,
    const QString& outcome,
    const QString& revision,
    const QJsonObject& rules,
    const QJsonValue& detail)
{
    return {
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("outcome"), outcome},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("rules"), rules},
        {QStringLiteral("detail"), detail},
    };
}

QJsonObject externalMcp(
    const QString& token,
    const bool canCopy = true,
    const bool canOpen = true,
    const bool canReveal = true)
{
    return {
        {QStringLiteral("selectionToken"), token},
        {QStringLiteral("source"), QStringLiteral("Claude Desktop")},
        {QStringLiteral("serverName"), QStringLiteral("filesystem")},
        {QStringLiteral("transport"), QJsonValue::Null},
        {QStringLiteral("canCopySourcePath"), canCopy},
        {QStringLiteral("canOpenSource"), canOpen},
        {QStringLiteral("canRevealSource"), canReveal},
    };
}

QJsonObject externalDiscovery(const QString& token)
{
    return {
        {QStringLiteral("mcpServers"), QJsonArray {externalMcp(token)}},
        {QStringLiteral("sessions"), QJsonArray {}},
    };
}

QJsonObject externalAction(
    const QString& action,
    const QJsonValue& sourcePath,
    const QJsonValue& handoffId,
    const QJsonValue& handoffPath)
{
    return {
        {QStringLiteral("action"), action},
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("handoffId"), handoffId},
        {QStringLiteral("handoffPath"), handoffPath},
        {QStringLiteral("displayName"), QStringLiteral("source.json")},
    };
}

QJsonObject localSession(
    const QString& id,
    const QString& incarnation)
{
    return {
        {QStringLiteral("kind"), QStringLiteral("local")},
        {QStringLiteral("id"), id},
        {QStringLiteral("incarnationId"), incarnation},
        {QStringLiteral("name"), id},
        {QStringLiteral("project"), QStringLiteral("/repo")},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("recovery"), QStringLiteral("live")},
        {QStringLiteral("scope"), QStringLiteral("justMe")},
        {QStringLiteral("access"), QStringLiteral("approve")},
    };
}

QByteArray sessionsEvent(const QJsonArray& sessions)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"), sessions},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject intelSnapshot()
{
    return {
        {QStringLiteral("identity"),
         QJsonObject {
             {QStringLiteral("agentType"), QStringLiteral("claude")},
             {QStringLiteral("version"), QJsonValue::Null},
             {QStringLiteral("model"), QJsonValue::Null},
             {QStringLiteral("title"), QJsonValue::Null},
             {QStringLiteral("cwd"), QStringLiteral("/repo")},
             {QStringLiteral("vendorSessionId"), QJsonValue::Null},
             {QStringLiteral("processId"), QJsonValue::Null},
         }},
        {QStringLiteral("lifecycle"), QStringLiteral("working")},
        {QStringLiteral("attention"), QJsonValue::Null},
        {QStringLiteral("pendingInteraction"), QJsonValue::Null},
        {QStringLiteral("currentActivity"), QJsonValue::Null},
        {QStringLiteral("workers"),
         QJsonObject {
             {QStringLiteral("active"), 0},
             {QStringLiteral("blocked"), 0},
             {QStringLiteral("failed"), 0},
             {QStringLiteral("completed"), 0},
         }},
        {QStringLiteral("outcome"), QJsonValue::Null},
        {QStringLiteral("exceptionalState"), QJsonValue::Null},
        {QStringLiteral("source"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("runtime")},
             {QStringLiteral("degraded"), false},
             {QStringLiteral("detail"), QJsonValue::Null},
         }},
    };
}

QByteArray liveSet(
    const QString& sessionId,
    const QString& incarnation)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("agent.intel.liveSet")},
        {QStringLiteral("requestId"), QJsonValue::Null},
        {QStringLiteral("authorityIncarnationId"), authorityIncarnation},
        {QStringLiteral("revision"), 1},
        {QStringLiteral("entries"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("sessionId"), sessionId},
                 {QStringLiteral("sessionIncarnationId"), incarnation},
                 {QStringLiteral("snapshot"), intelSnapshot()},
             },
         }},
    }).toJson(QJsonDocument::Compact);
}

void activateProject(kodosi::ProjectIntelligenceModel& model)
{
    model.ingestAuthEvent(auth());
    QVERIFY(model.refreshSources());
}

void loadProjectMemory(
    ProjectDispatcher& dispatcher,
    kodosi::ProjectIntelligenceModel& project,
    const QString& sourceToken,
    const QString& sourceReplacement,
    const QString& readToken,
    const QString& openToken,
    const QString& copyToken,
    const QString& destinationToken = {})
{
    QJsonArray sources {source(
        sourceToken,
        QStringLiteral("active"),
        QStringLiteral("Project"),
        QJsonArray {QStringLiteral("session-a")})};
    if (!destinationToken.isEmpty()) {
        sources.push_back(source(
            destinationToken,
            QStringLiteral("claudeArchive"),
            QStringLiteral("Destination")));
    }
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(sources)));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            sourceReplacement,
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {memorySummary(readToken, openToken, copyToken)})));
    QVERIFY(project.selectMemory(itemId(project.memories(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(readToken)));
}

} // namespace

class ProjectIntelligenceModelTest final : public QObject {
    Q_OBJECT

private slots:
    void syntheticProjectionIsTypedAndPathFree();
    void liveProtocolV37SnapshotShapeIsAccepted();
    void legacySnapshotSummaryFieldsAreRejected();
    void settingsTreeReplacementNotifiesAvailability();
    void detailCachesServeRepeatSelections();
    void memoryCopyDestinationsExcludeCopilotArchives();
    void newSelectionSupersedesFailedRefreshRecovery();
    void loadingMoreSourcesPreservesSelectedDetails();
    void refreshReconcilesSelectedDetails();
    void copyRefreshPreservesFailedDetailRecovery();
    void learnedDetailIdentitySurvivesRebind();
    void uncachedReselectReacquiresCapabilities();
    void repeatOpenReacquiresCapabilities();
    void autoModeSparseRulesBoundsDemandAndDirtyRefresh();
    void autoModeReceiptsAndStructuredReconciliation();
    void autoModeMutationDeliveryOutlivesSurfaceDemand();
    void malformedMutationOutcomeDetailCouplingIsRejected();
    void aggregateRefreshDoesNotTouchAutoModeDraft();
    void projectCustomizationProjectionContainsOnlyMcp();
    void duplicateSourcesAreDistinctAndRetryNeverGuesses();
    void detailIdentityMismatchReacquiresBeforeRetry();
    void projectSessionEntryRequiresExactIncarnation();
    void settingsIntegerAndNumberRemainDistinctAcrossScopes();
    void settingsTreesRejectDuplicateAndMismatchedNodes();
    void projectOpenHandoffsAlwaysRelease();
    void externalActionsValidateNullShapesRevealAndRelease();
    void projectMemoryCopyReceiptsAreTruthfulAndReconciled();
    void projectMemoryCopyDeliveryOutlivesSurfaceDemand();
    void acceptedMutationDeliveryCancelsOnAuthorityReset();
    void boundModelsClearOnRuntimeRestart();
};

void ProjectIntelligenceModelTest::syntheticProjectionIsTypedAndPathFree()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    kodosi::AgentAutoModeRulesModel rules(dispatcher);
    kodosi::ExternalDiscoveryModel external(dispatcher, *desktop.integration);

    project.installSyntheticFixture();
    rules.installSyntheticFixture();
    external.installSyntheticFixture();

    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Ready);
    QCOMPARE(project.sources()->rowCount(), 2);
    QCOMPARE(project.copyDestinations()->rowCount(), 2);
    QCOMPARE(project.sessions()->rowCount(), 2);
    QCOMPARE(project.memories()->rowCount(), 2);
    QCOMPARE(project.agents()->rowCount(), 1);
    QCOMPARE(project.customizations()->rowCount(), 1);
    QCOMPARE(project.settingsTree()->rowCount(), 3);
    QCOMPARE(rules.state(), kodosi::AgentAutoModeRulesModel::State::Ready);
    QCOMPARE(external.servers()->rowCount(), 1);
    QCOMPARE(external.sessions()->rowCount(), 1);
    QCOMPARE(
        project.sources()->data(
            project.sources()->index(0),
            kodosi::PresentationListModel::SubtitleRole).toString(),
        QStringLiteral("Project intelligence"));
    QCOMPARE(
        project.sources()->data(
            project.sources()->index(1),
            kodosi::PresentationListModel::SubtitleRole).toString(),
        QStringLiteral("Claude project archive"));
    QVERIFY(
        project.customizations()->data(
            project.customizations()->index(0),
            kodosi::PresentationListModel::KindRole)
            .toString()
            .contains(QStringLiteral("mcp"), Qt::CaseInsensitive));
    QCOMPARE(project.selectedAgentErrorCount(), 1);
    QCOMPARE(project.agentParseErrors().size(), 1);
    const auto externalServerId = itemId(external.servers(), 0);
    QVERIFY(external.canCopySourcePath(externalServerId));
    QVERIFY(external.canOpenSource(externalServerId));
    QVERIFY(external.canRevealSource(externalServerId));

    for (auto* model : {
             project.sources(),
             project.sessions(),
             project.memories(),
             project.agents(),
             project.customizations(),
             project.settingsTree(),
             external.servers(),
             external.sessions()}) {
        const auto roles = model->roleNames().values();
        QVERIFY(!roles.contains(QByteArrayLiteral("selectionToken")));
        QVERIFY(!roles.contains(QByteArrayLiteral("path")));
        QVERIFY(!roles.contains(QByteArrayLiteral("handoffId")));
        QVERIFY(!roles.contains(QByteArrayLiteral("canonicalCwd")));
        QVERIFY(!roles.contains(QByteArrayLiteral("projectSlug")));
        QVERIFY(!roles.contains(QByteArrayLiteral("requestId")));
        QVERIFY(!roles.contains(QByteArrayLiteral("incarnationId")));
    }
}

void ProjectIntelligenceModelTest::liveProtocolV37SnapshotShapeIsAccepted()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);

    const auto readToken = uuidV7();
    const auto memoryOpenToken = uuidV7();
    const auto copyToken = uuidV7();
    const auto detailToken = uuidV7();
    const auto agentOpenToken = uuidV7();
    const auto memory = memorySummary(
        readToken,
        memoryOpenToken,
        copyToken);
    const auto agent = agentSummary(detailToken, agentOpenToken);
    const auto memoryKeys = memory.keys();
    const auto agentKeys = agent.keys();
    QCOMPARE(
        QSet<QString>(memoryKeys.cbegin(), memoryKeys.cend()),
        QSet<QString>({
            QStringLiteral("readSelectionToken"),
            QStringLiteral("openSelectionToken"),
            QStringLiteral("copySelectionToken"),
            QStringLiteral("filename"),
            QStringLiteral("memoryType"),
        }));
    QCOMPARE(
        QSet<QString>(agentKeys.cbegin(), agentKeys.cend()),
        QSet<QString>({
            QStringLiteral("detailSelectionToken"),
            QStringLiteral("openSelectionToken"),
            QStringLiteral("target"),
            QStringLiteral("name"),
            QStringLiteral("description"),
            QStringLiteral("model"),
            QStringLiteral("tools"),
            QStringLiteral("errorCount"),
        }));

    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {memory},
            QJsonArray {agent})));

    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Ready);
    QCOMPARE(project.memories()->rowCount(), 1);
    QCOMPARE(project.agents()->rowCount(), 1);

    QVERIFY(project.selectMemory(itemId(project.memories(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(
            readToken,
            QStringLiteral("live memory"),
            QStringLiteral("/repo"),
            QStringLiteral("-repo"))));
    QCOMPARE(project.memoryContent(), QStringLiteral("live memory"));
    QVERIFY(project.memoryError().isEmpty());

    QVERIFY(project.selectAgent(itemId(project.agents(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        agentDetail(
            detailToken,
            QJsonArray {
                QStringLiteral("Bash"),
                QStringLiteral("Write"),
            })));
    QCOMPARE(project.agentPrompt(), QStringLiteral("Review exactly."));
    QVERIFY(project.agentDetailError().isEmpty());

    ProjectDispatcher archiveDispatcher;
    kodosi::SessionCatalogModel archiveSessions;
    kodosi::AgentSessionIntelModel archiveIntel(
        archiveDispatcher,
        archiveSessions);
    kodosi::AgentConversationModel archiveConversation(
        archiveDispatcher,
        archiveSessions,
        archiveIntel);
    DesktopHarness archiveDesktop(archiveSessions);
    kodosi::ProjectIntelligenceModel archive(
        archiveDispatcher,
        archiveConversation,
        *archiveDesktop.integration);
    activateProject(archive);
    archive.ingestAgentIntelEvent(reply(
        requestId(archiveDispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("claudeArchive"),
                QStringLiteral("Archive")),
        })));
    QVERIFY(archive.selectSource(itemId(archive.sources(), 0)));
    const auto archiveReadToken = uuidV7();
    archive.ingestAgentIntelEvent(reply(
        requestId(archiveDispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("claudeArchive"),
            QStringLiteral("Archive"),
            {},
            QJsonArray {
                memorySummary(
                    archiveReadToken,
                    uuidV7(),
                    uuidV7()),
            })));
    QVERIFY(archive.selectMemory(itemId(archive.memories(), 0)));
    archive.ingestAgentIntelEvent(reply(
        requestId(archiveDispatcher.last()),
        memoryDetail(
            archiveReadToken,
            QStringLiteral("archive memory"),
            QString {},
            QStringLiteral("-archive"))));
    QCOMPARE(archive.memoryContent(), QStringLiteral("archive memory"));
    QVERIFY(archive.memoryError().isEmpty());
}

void ProjectIntelligenceModelTest::
    legacySnapshotSummaryFieldsAreRejected()
{
    const auto rejectsSnapshot =
        [](const QJsonArray& memories, const QJsonArray& agents) {
            ProjectDispatcher dispatcher;
            kodosi::SessionCatalogModel sessions;
            kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
            kodosi::AgentConversationModel conversation(
                dispatcher,
                sessions,
                intel);
            DesktopHarness desktop(sessions);
            kodosi::ProjectIntelligenceModel project(
                dispatcher,
                conversation,
                *desktop.integration);
            project.ingestAuthEvent(auth());
            if (!project.refreshSources()) {
                return false;
            }
            project.ingestAgentIntelEvent(reply(
                requestId(dispatcher.last()),
                sourcePage(QJsonArray {
                    source(
                        uuidV7(),
                        QStringLiteral("active"),
                        QStringLiteral("Project"),
                        QJsonArray {QStringLiteral("session-a")}),
                })));
            const auto sourceId = itemId(project.sources(), 0);
            if (sourceId.isEmpty() || !project.selectSource(sourceId)) {
                return false;
            }
            project.ingestAgentIntelEvent(reply(
                requestId(dispatcher.last()),
                projectSnapshot(
                    uuidV7(),
                    QStringLiteral("active"),
                    QStringLiteral("Project"),
                    {},
                    memories,
                    agents)));
            return project.state()
                == kodosi::ProjectIntelligenceModel::State::Failed;
        };

    auto legacyMemory =
        memorySummary(uuidV7(), uuidV7(), uuidV7());
    legacyMemory.insert(
        QStringLiteral("canonicalCwd"),
        QStringLiteral("/repo"));
    legacyMemory.insert(
        QStringLiteral("projectSlug"),
        QStringLiteral("-repo"));
    QVERIFY(rejectsSnapshot(QJsonArray {legacyMemory}, {}));

    auto legacyAgent = agentSummary(uuidV7(), uuidV7());
    legacyAgent.insert(
        QStringLiteral("disallowedTools"),
        QJsonArray {QStringLiteral("Bash")});
    QVERIFY(rejectsSnapshot({}, QJsonArray {legacyAgent}));
}

void ProjectIntelligenceModelTest::
    settingsTreeReplacementNotifiesAvailability()
{
    ProjectDispatcher syntheticDispatcher;
    kodosi::SessionCatalogModel syntheticSessions;
    kodosi::AgentSessionIntelModel syntheticIntel(
        syntheticDispatcher,
        syntheticSessions);
    kodosi::AgentConversationModel syntheticConversation(
        syntheticDispatcher,
        syntheticSessions,
        syntheticIntel);
    DesktopHarness syntheticDesktop(syntheticSessions);
    kodosi::ProjectIntelligenceModel synthetic(
        syntheticDispatcher,
        syntheticConversation,
        *syntheticDesktop.integration);
    QSignalSpy syntheticNotifications(
        &synthetic,
        &kodosi::ProjectIntelligenceModel::settingsFilterChanged);
    synthetic.installSyntheticFixture();
    QCOMPARE(syntheticNotifications.size(), 1);
    QCOMPARE(synthetic.settingsTree()->rowCount(), 3);
    QVERIFY(synthetic.settingsAvailabilityMessage().isEmpty());

    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    QSignalSpy liveNotifications(
        &project,
        &kodosi::ProjectIntelligenceModel::settingsFilterChanged);
    QVERIFY(!project.settingsAvailabilityMessage().isEmpty());
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            {},
            {},
            QJsonArray {
                settingsBundle(
                    QStringLiteral("claude"),
                    QStringLiteral("user"),
                    QJsonArray {
                        settingsNode(
                            0,
                            QStringLiteral("defaultMode"),
                            QStringLiteral("plan")),
                    }),
            })));
    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Ready);
    QCOMPARE(project.settingsTree()->rowCount(), 1);
    QVERIFY(project.settingsAvailabilityMessage().isEmpty());
    QCOMPARE(liveNotifications.size(), 1);
}

void ProjectIntelligenceModelTest::detailCachesServeRepeatSelections()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    const auto sourceId = itemId(project.sources(), 0);
    QVERIFY(project.selectSource(sourceId));
    const auto firstMemoryRead = uuidV7();
    const auto secondMemoryRead = uuidV7();
    const auto firstAgentDetail = uuidV7();
    const auto secondAgentDetail = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(firstMemoryRead, uuidV7(), uuidV7()),
                memorySummary(secondMemoryRead, uuidV7(), uuidV7()),
            },
            QJsonArray {
                agentSummary(firstAgentDetail, uuidV7()),
                agentSummary(secondAgentDetail, uuidV7()),
            })));
    const auto firstMemoryId = itemId(project.memories(), 0);
    const auto secondMemoryId = itemId(project.memories(), 1);
    const auto firstAgentId = itemId(project.agents(), 0);
    const auto secondAgentId = itemId(project.agents(), 1);
    QVERIFY(firstMemoryId != secondMemoryId);
    QVERIFY(firstAgentId != secondAgentId);

    QVERIFY(project.selectMemory(firstMemoryId));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(firstMemoryRead, QStringLiteral("first memory"))));
    QVERIFY(project.selectMemory(secondMemoryId));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(secondMemoryRead, QStringLiteral("second memory"))));
    const auto beforeCachedMemory = dispatcher.commands.size();
    QVERIFY(project.selectMemory(firstMemoryId));
    QCOMPARE(dispatcher.commands.size(), beforeCachedMemory);
    QCOMPARE(project.memoryContent(), QStringLiteral("first memory"));
    QVERIFY(!project.memoryLoading());

    QVERIFY(project.selectAgent(firstAgentId));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        agentDetail(
            firstAgentDetail,
            QJsonArray {QStringLiteral("Bash")},
            QStringLiteral("reviewer"),
            QStringLiteral("Reviews changes"),
            QStringLiteral("sonnet"),
            QJsonArray {QStringLiteral("Read")},
            QStringLiteral("First prompt."))));
    QVERIFY(project.selectAgent(secondAgentId));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        agentDetail(
            secondAgentDetail,
            QJsonArray {QStringLiteral("Bash")},
            QStringLiteral("reviewer"),
            QStringLiteral("Reviews changes"),
            QStringLiteral("sonnet"),
            QJsonArray {QStringLiteral("Read")},
            QStringLiteral("Second prompt."))));
    const auto beforeCachedAgent = dispatcher.commands.size();
    QVERIFY(project.selectAgent(firstAgentId));
    QCOMPARE(dispatcher.commands.size(), beforeCachedAgent);
    QCOMPARE(project.agentPrompt(), QStringLiteral("First prompt."));
    QVERIFY(!project.agentDetailLoading());

    QVERIFY(project.refreshSources(true));
    QVERIFY(project.hasSourceSnapshot());
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project renamed"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QCOMPARE(itemId(project.sources(), 0), sourceId);
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.inspectProjectSourceBound"));
    QVERIFY(project.hasSourceSnapshot());
    QCOMPARE(project.memories()->rowCount(), 2);
    QCOMPARE(project.agents()->rowCount(), 2);
    QCOMPARE(project.memoryContent(), QStringLiteral("first memory"));
    QCOMPARE(project.agentPrompt(), QStringLiteral("First prompt."));
    const auto refreshedFirstMemoryRead = uuidV7();
    const auto refreshedSecondMemoryRead = uuidV7();
    const auto refreshedFirstAgentRead = uuidV7();
    const auto refreshedSecondAgentRead = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project renamed"),
            {},
            QJsonArray {
                memorySummary(
                    refreshedFirstMemoryRead,
                    uuidV7(),
                    uuidV7()),
                memorySummary(
                    refreshedSecondMemoryRead,
                    uuidV7(),
                    uuidV7()),
            },
            QJsonArray {
                agentSummary(refreshedFirstAgentRead, uuidV7()),
                agentSummary(refreshedSecondAgentRead, uuidV7()),
            })));
    QCOMPARE(itemId(project.memories(), 0), firstMemoryId);
    QCOMPARE(itemId(project.memories(), 1), secondMemoryId);
    QCOMPARE(itemId(project.agents(), 0), firstAgentId);
    QCOMPARE(itemId(project.agents(), 1), secondAgentId);
    QVERIFY(project.selectedMemoryId().isEmpty());
    QVERIFY(project.selectedAgentId().isEmpty());
    QVERIFY(project.memoryContent().isEmpty());
    QVERIFY(project.agentPrompt().isEmpty());
    const auto beforeReboundCache = dispatcher.commands.size();
    QVERIFY(project.selectMemory(firstMemoryId));
    QCOMPARE(dispatcher.commands.size(), beforeReboundCache + 1);
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        refreshedFirstMemoryRead);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(
            refreshedFirstMemoryRead,
            QStringLiteral("refreshed first memory"),
            QStringLiteral("/repo"),
            QStringLiteral("-repo"))));
    QCOMPARE(
        project.memoryContent(),
        QStringLiteral("refreshed first memory"));
    QVERIFY(project.selectAgent(firstAgentId));
    QCOMPARE(dispatcher.commands.size(), beforeReboundCache + 2);
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        refreshedFirstAgentRead);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        agentDetail(
            refreshedFirstAgentRead,
            QJsonArray {QStringLiteral("Write")},
            QStringLiteral("reviewer"),
            QStringLiteral("Reviews changes"),
            QStringLiteral("sonnet"),
            QJsonArray {QStringLiteral("Read")},
            QStringLiteral("Refreshed first prompt."))));
    QCOMPARE(
        project.agentPrompt(),
        QStringLiteral("Refreshed first prompt."));
    QVERIFY(project.agentDetailError().isEmpty());
}

void ProjectIntelligenceModelTest::
    memoryCopyDestinationsExcludeCopilotArchives()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
            source(
                uuidV7(),
                QStringLiteral("claudeArchive"),
                QStringLiteral("Claude destination")),
            source(
                uuidV7(),
                QStringLiteral("claudeArchive"),
                QStringLiteral("Claude destination")),
            source(
                uuidV7(),
                QStringLiteral("copilotArchive"),
                QStringLiteral("Copilot archive")),
        })));

    QCOMPARE(project.sources()->rowCount(), 4);
    QCOMPARE(project.copyDestinations()->rowCount(), 1);
    for (auto row = 0; row < project.copyDestinations()->rowCount(); ++row) {
        QVERIFY(
            project.copyDestinations()
                ->data(
                    project.copyDestinations()->index(row),
                    kodosi::PresentationListModel::KindRole)
                .toString()
            != QStringLiteral("copilotArchive"));
    }
}

void ProjectIntelligenceModelTest::
    newSelectionSupersedesFailedRefreshRecovery()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project A"),
                QJsonArray {QStringLiteral("session-a")}),
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project B"),
                QJsonArray {QStringLiteral("session-b")}),
        })));
    const auto sourceA = itemId(project.sources(), 0);
    const auto sourceB = itemId(project.sources(), 1);
    QVERIFY(project.selectSource(sourceA));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project A"))));

    QVERIFY(project.refreshSources(true));
    project.ingestAgentIntelEvent(runtimeError(requestId(dispatcher.last())));
    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Failed);

    QVERIFY(project.selectSource(sourceB));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project B"))));
    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Ready);
    QCOMPARE(project.selectedSourceId(), sourceB);

    const auto refreshedA = uuidV7();
    const auto refreshedB = uuidV7();
    QVERIFY(project.refreshSources(true));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                refreshedA,
                QStringLiteral("active"),
                QStringLiteral("Project A"),
                QJsonArray {QStringLiteral("session-a")}),
            source(
                refreshedB,
                QStringLiteral("active"),
                QStringLiteral("Project B"),
                QJsonArray {QStringLiteral("session-b")}),
        })));
    QCOMPARE(project.selectedSourceId(), sourceB);
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.inspectProjectSourceBound"));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        refreshedB);
}

void ProjectIntelligenceModelTest::
    loadingMoreSourcesPreservesSelectedDetails()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(
            QJsonArray {
                source(
                    uuidV7(),
                    QStringLiteral("active"),
                    QStringLiteral("Project A"),
                    QJsonArray {QStringLiteral("session-a")}),
            },
            QStringLiteral("cursor-2"),
            true)));
    const auto sourceA = itemId(project.sources(), 0);
    QVERIFY(!project.selectSourceForSession(QStringLiteral("session-a")));
    QVERIFY(project.selectSource(sourceA));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project A"),
            QJsonArray {
                projectSession(
                    QStringLiteral("session-a"),
                    firstIncarnation),
            })));
    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Ready);
    QCOMPARE(project.sessions()->rowCount(), 1);

    QVERIFY(project.loadMoreSources());
    QVERIFY(project.hasSourceSnapshot());
    QCOMPARE(project.sessions()->rowCount(), 1);
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("cursor")).toString(),
        QStringLiteral("cursor-2"));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("claudeArchive"),
                QStringLiteral("Archive B")),
        })));

    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Ready);
    QCOMPARE(project.selectedSourceId(), sourceA);
    QCOMPARE(project.sessions()->rowCount(), 1);
    QCOMPARE(project.sources()->rowCount(), 2);
    QVERIFY(project.selectSourceForSession(QStringLiteral("session-a")));
}

void ProjectIntelligenceModelTest::refreshReconcilesSelectedDetails()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    const auto initialMemoryRead = uuidV7();
    const auto initialAgentRead = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(initialMemoryRead, uuidV7(), uuidV7()),
            },
            QJsonArray {
                agentSummary(initialAgentRead, uuidV7()),
            })));

    QVERIFY(project.selectMemory(itemId(project.memories(), 0)));
    project.ingestAgentIntelEvent(runtimeError(requestId(dispatcher.last())));
    QVERIFY(!project.memoryError().isEmpty());
    QVERIFY(!project.memoryLoading());

    const auto refreshedMemoryRead = uuidV7();
    const auto refreshedAgentRead = uuidV7();
    QVERIFY(project.refreshSources(true));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(refreshedMemoryRead, uuidV7(), uuidV7()),
            },
            QJsonArray {
                agentSummary(refreshedAgentRead, uuidV7()),
            })));
    QVERIFY(project.selectedMemoryId().isEmpty());
    QVERIFY(project.memoryContent().isEmpty());
    QVERIFY(project.memoryError().isEmpty());
    QVERIFY(!project.memoryLoading());

    QVERIFY(project.selectAgent(itemId(project.agents(), 0)));
    QVERIFY(project.agentDetailLoading());
    const auto finalMemoryRead = uuidV7();
    const auto finalAgentRead = uuidV7();
    QVERIFY(project.refreshSources(true));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(finalMemoryRead, uuidV7(), uuidV7()),
            },
            QJsonArray {
                agentSummary(finalAgentRead, uuidV7()),
            })));
    QVERIFY(project.selectedAgentId().isEmpty());
    QVERIFY(project.agentPrompt().isEmpty());
    QVERIFY(project.agentDetailError().isEmpty());
    QVERIFY(!project.agentDetailLoading());
}

void ProjectIntelligenceModelTest::
    copyRefreshPreservesFailedDetailRecovery()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    const auto initialRead = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
            source(
                uuidV7(),
                QStringLiteral("claudeArchive"),
                QStringLiteral("Destination")),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(initialRead, uuidV7(), uuidV7()),
            })));
    const auto memoryId = itemId(project.memories(), 0);
    QVERIFY(project.selectMemory(memoryId));
    project.ingestAgentIntelEvent(runtimeError(requestId(dispatcher.last())));
    QVERIFY(!project.memoryError().isEmpty());

    QVERIFY(project.copySelectedMemory(itemId(project.sources(), 1)));
    const auto copy = dispatcher.last();
    project.ingestAgentIntelEvent(reply(
        requestId(copy),
        QJsonObject {
            {QStringLiteral("mutationId"),
             copy.value(QStringLiteral("mutationId")).toString()},
            {QStringLiteral("outcome"), QStringLiteral("copied")},
            {QStringLiteral("filename"), QStringLiteral("MEMORY.md")},
            {QStringLiteral("targetLabel"), QStringLiteral("Destination")},
            {QStringLiteral("detail"), QJsonValue::Null},
        }));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.listProjectSourcesBound"));

    const auto refreshedRead = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
            source(
                uuidV7(),
                QStringLiteral("claudeArchive"),
                QStringLiteral("Destination")),
        })));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(refreshedRead, uuidV7(), uuidV7()),
            })));
    QCOMPARE(project.selectedMemoryId(), memoryId);
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readClaudeMemoryBound"));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        refreshedRead);
    QVERIFY(project.memoryLoading());
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(
            refreshedRead,
            QStringLiteral("recovered after copy"))));
    QCOMPARE(
        project.memoryContent(),
        QStringLiteral("recovered after copy"));
    QVERIFY(project.memoryError().isEmpty());
}

void ProjectIntelligenceModelTest::learnedDetailIdentitySurvivesRebind()
{
    constexpr auto itemCount = 33;
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    QSignalSpy decodeErrors(
        &project,
        &kodosi::ProjectIntelligenceModel::decodeError);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    const auto sourceId = itemId(project.sources(), 0);
    QVERIFY(project.selectSource(sourceId));

    QJsonArray memorySummaries;
    QJsonArray agentSummaries;
    QVector<QString> memoryReadTokens;
    QVector<QString> agentDetailTokens;
    for (auto index = 0; index < itemCount; ++index) {
        const auto memoryToken = uuidV7();
        const auto agentToken = uuidV7();
        memoryReadTokens.push_back(memoryToken);
        agentDetailTokens.push_back(agentToken);
        memorySummaries.push_back(memorySummary(
            memoryToken,
            uuidV7(),
            uuidV7(),
            QStringLiteral("memory-%1.md").arg(index)));
        agentSummaries.push_back(agentSummary(
            agentToken,
            uuidV7(),
            QStringLiteral("reviewer-%1").arg(index),
            QStringLiteral("Reviews changes %1").arg(index)));
    }
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            memorySummaries,
            agentSummaries)));
    const auto firstMemoryId = itemId(project.memories(), 0);
    const auto firstAgentId = itemId(project.agents(), 0);

    for (auto index = 0; index < itemCount; ++index) {
        QVERIFY(project.selectMemory(itemId(project.memories(), index)));
        project.ingestAgentIntelEvent(reply(
            requestId(dispatcher.last()),
            memoryDetail(
                memoryReadTokens.at(index),
                QStringLiteral("memory %1").arg(index),
                QStringLiteral("/repo"),
                QStringLiteral("-repo"),
                QStringLiteral("memory-%1.md").arg(index))));
        QVERIFY(project.memoryError().isEmpty());
    }
    for (auto index = 0; index < itemCount; ++index) {
        const auto name = QStringLiteral("reviewer-%1").arg(index);
        const auto description =
            QStringLiteral("Reviews changes %1").arg(index);
        QVERIFY(project.selectAgent(itemId(project.agents(), index)));
        project.ingestAgentIntelEvent(reply(
            requestId(dispatcher.last()),
            agentDetail(
                agentDetailTokens.at(index),
                QJsonArray {QStringLiteral("Bash")},
                name,
                description)));
        QVERIFY(project.agentDetailError().isEmpty());
    }

    QVERIFY(project.refreshSources(true));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project rebound"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.inspectProjectSourceBound"));
    memorySummaries = {};
    agentSummaries = {};
    memoryReadTokens.clear();
    agentDetailTokens.clear();
    for (auto index = 0; index < itemCount; ++index) {
        const auto memoryToken = uuidV7();
        const auto agentToken = uuidV7();
        memoryReadTokens.push_back(memoryToken);
        agentDetailTokens.push_back(agentToken);
        memorySummaries.push_back(memorySummary(
            memoryToken,
            uuidV7(),
            uuidV7(),
            QStringLiteral("memory-%1.md").arg(index)));
        agentSummaries.push_back(agentSummary(
            agentToken,
            uuidV7(),
            QStringLiteral("reviewer-%1").arg(index),
            QStringLiteral("Reviews changes %1").arg(index)));
    }
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project rebound"),
            {},
            memorySummaries,
            agentSummaries)));
    QCOMPARE(itemId(project.memories(), 0), firstMemoryId);
    QCOMPARE(itemId(project.agents(), 0), firstAgentId);

    QVERIFY(project.selectMemory(firstMemoryId));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        memoryReadTokens.constFirst());
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(
            memoryReadTokens.constFirst(),
            QStringLiteral("wrong project"),
            QStringLiteral("/other"),
            QStringLiteral("-repo"),
            QStringLiteral("memory-0.md"))));
    QVERIFY(project.memoryContent().isEmpty());
    QVERIFY(project.memoryError().contains(QStringLiteral("different selection")));

    QVERIFY(project.selectAgent(firstAgentId));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        agentDetailTokens.constFirst());
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        agentDetail(
            agentDetailTokens.constFirst(),
            QJsonArray {QStringLiteral("Write")},
            QStringLiteral("reviewer-0"),
            QStringLiteral("Reviews changes 0"))));
    QCOMPARE(project.agentPrompt(), QStringLiteral("Review exactly."));
    QVERIFY(project.agentDetailError().isEmpty());
    QCOMPARE(decodeErrors.size(), 1);
}

void ProjectIntelligenceModelTest::
    uncachedReselectReacquiresCapabilities()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    QSignalSpy actions(
        &project,
        &kodosi::ProjectIntelligenceModel::actionMessage);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    const auto sourceId = itemId(project.sources(), 0);
    QVERIFY(project.selectSource(sourceId));
    const auto firstMemoryRead = uuidV7();
    const auto firstAgentDetail = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(
                    firstMemoryRead,
                    uuidV7(),
                    uuidV7()),
            },
            QJsonArray {
                agentSummary(firstAgentDetail, uuidV7()),
            })));
    const auto memoryId = itemId(project.memories(), 0);
    const auto agentId = itemId(project.agents(), 0);

    QVERIFY(project.selectMemory(memoryId));
    project.ingestAgentIntelEvent(runtimeError(
        requestId(dispatcher.last()),
        QStringLiteral("deterministic"),
        QJsonValue::Null,
        false,
        QStringLiteral("memory selection is stale")));
    QVERIFY(!project.memoryError().isEmpty());
    QVERIFY(project.selectMemory(memoryId));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.listProjectSourcesBound"));
    QVERIFY(actions.constLast().at(0).toString().contains(
        QStringLiteral("Refreshing project capabilities")));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project memory refreshed"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    const auto secondMemoryRead = uuidV7();
    const auto secondAgentDetail = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project memory refreshed"),
            {},
            QJsonArray {
                memorySummary(
                    secondMemoryRead,
                    uuidV7(),
                    uuidV7()),
            },
            QJsonArray {
                agentSummary(secondAgentDetail, uuidV7()),
            })));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readClaudeMemoryBound"));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        secondMemoryRead);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(
            secondMemoryRead,
            QStringLiteral("refreshed memory"))));
    QCOMPARE(project.memoryContent(), QStringLiteral("refreshed memory"));

    QVERIFY(project.selectAgent(agentId));
    project.ingestAgentIntelEvent(runtimeError(
        requestId(dispatcher.last()),
        QStringLiteral("deterministic"),
        QJsonValue::Null,
        false,
        QStringLiteral("agent selection is stale")));
    QVERIFY(!project.agentDetailError().isEmpty());
    QVERIFY(project.selectAgent(agentId));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.listProjectSourcesBound"));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project agent refreshed"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    const auto finalAgentDetail = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project agent refreshed"),
            {},
            QJsonArray {
                memorySummary(uuidV7(), uuidV7(), uuidV7()),
            },
            QJsonArray {
                agentSummary(finalAgentDetail, uuidV7()),
            })));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readCustomAgentBound"));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        finalAgentDetail);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        agentDetail(finalAgentDetail)));
    QCOMPARE(project.agentPrompt(), QStringLiteral("Review exactly."));
}

void ProjectIntelligenceModelTest::repeatOpenReacquiresCapabilities()
{
#if defined(Q_OS_LINUX)
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("repeat-open-XXXXXX")));
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("MEMORY.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("memory"), 6);
    file.close();
    const auto descriptor =
        ::open(path.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY(descriptor >= 0);
    const auto closeDescriptor =
        qScopeGuard([descriptor] { ::close(descriptor); });
    const auto handoffPath =
        createBoundArtifact(directory.path(), descriptor);
    QVERIFY(!handoffPath.isEmpty());

    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    QSignalSpy actions(
        &project,
        &kodosi::ProjectIntelligenceModel::actionMessage);
    activateProject(project);
    const auto initialSourceToken = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                initialSourceToken,
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    const auto sourceId = itemId(project.sources(), 0);
    QVERIFY(project.selectSource(sourceId));
    const auto initialMemoryRead = uuidV7();
    const auto initialMemoryOpen = uuidV7();
    const auto initialAgentDetail = uuidV7();
    const auto initialAgentOpen = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(
                    initialMemoryRead,
                    initialMemoryOpen,
                    uuidV7()),
            },
            QJsonArray {
                agentSummary(initialAgentDetail, initialAgentOpen),
            })));

    QVERIFY(project.selectMemory(itemId(project.memories(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(initialMemoryRead)));
    QCOMPARE(project.memoryContent(), QStringLiteral("body"));
    QVERIFY(project.openSelectedMemory());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        initialMemoryOpen);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        QJsonObject {
            {QStringLiteral("displayName"), QStringLiteral("MEMORY.md")},
            {QStringLiteral("handoffId"), uuidV7()},
            {QStringLiteral("handoffPath"), handoffPath},
        }));

    QVERIFY(project.openSelectedMemory());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.listProjectSourcesBound"));
    QVERIFY(!actions.isEmpty());
    QVERIFY(actions.constLast().at(0).toString().contains(
        QStringLiteral("Refreshing project capabilities")));
    QVERIFY(!actions.constLast().at(1).toBool());
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project refreshed"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.inspectProjectSourceBound"));
    const auto refreshedMemoryOpen = uuidV7();
    const auto refreshedAgentDetail = uuidV7();
    const auto refreshedAgentOpen = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project refreshed"),
            {},
            QJsonArray {
                memorySummary(
                    uuidV7(),
                    refreshedMemoryOpen,
                    uuidV7()),
            },
            QJsonArray {
                agentSummary(
                    refreshedAgentDetail,
                    refreshedAgentOpen),
            })));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.openProjectMemoryBound"));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        refreshedMemoryOpen);
    QCOMPARE(project.memoryContent(), QStringLiteral("body"));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        QJsonObject {
            {QStringLiteral("displayName"), QStringLiteral("MEMORY.md")},
            {QStringLiteral("handoffId"), uuidV7()},
            {QStringLiteral("handoffPath"), handoffPath},
        }));
    QCOMPARE(project.memoryContent(), QStringLiteral("body"));

    QVERIFY(project.selectAgent(itemId(project.agents(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        agentDetail(refreshedAgentDetail)));
    QCOMPARE(project.agentFrontmatter(), QStringLiteral("name: reviewer"));
    QCOMPARE(project.agentPrompt(), QStringLiteral("Review exactly."));
    QVERIFY(project.openSelectedAgent());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        refreshedAgentOpen);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        QJsonObject {
            {QStringLiteral("displayName"), QStringLiteral("reviewer.md")},
            {QStringLiteral("handoffId"), uuidV7()},
            {QStringLiteral("handoffPath"), handoffPath},
        }));

    QVERIFY(project.openSelectedAgent());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.listProjectSourcesBound"));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project refreshed again"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    const auto finalAgentOpen = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project refreshed again"),
            {},
            QJsonArray {
                memorySummary(uuidV7(), uuidV7(), uuidV7()),
            },
            QJsonArray {
                agentSummary(uuidV7(), finalAgentOpen),
            })));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.openCustomAgentBound"));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        finalAgentOpen);
    QCOMPARE(project.agentFrontmatter(), QStringLiteral("name: reviewer"));
    QCOMPARE(project.agentPrompt(), QStringLiteral("Review exactly."));
#endif
}

void ProjectIntelligenceModelTest::
    autoModeSparseRulesBoundsDemandAndDirtyRefresh()
{
    ProjectDispatcher dispatcher;
    kodosi::AgentAutoModeRulesModel rules(dispatcher);
    rules.ingestAuthEvent(auth());
    QVERIFY(rules.refresh());
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("0123456789abcdef"),
            QJsonObject {})));
    QCOMPARE(rules.state(), kodosi::AgentAutoModeRulesModel::State::Ready);
    QVERIFY(rules.environmentText().isEmpty());
    QVERIFY(rules.allowText().isEmpty());
    QVERIFY(rules.softDenyText().isEmpty());
    QVERIFY(rules.hardDenyText().isEmpty());

    const auto loadedCommandCount = dispatcher.commands.size();
    QVERIFY(rules.refresh());
    QCOMPARE(dispatcher.commands.size(), loadedCommandCount);

    rules.setAllowText(QStringLiteral("draft"));
    QVERIFY(rules.save());
    const auto write = dispatcher.last();
    const auto mutationId =
        write.value(QStringLiteral("mutationId")).toString();
    rules.ingestAgentIntelEvent(runtimeError(
        requestId(write),
        QStringLiteral("deterministic"),
        mutationId,
        false,
        QStringLiteral("revision changed")));
    QCOMPARE(
        dispatcher.count(
            QStringLiteral("agent.intel.reconcileClaudeAutoModeRulesWrite")),
        0);
    QCOMPARE(rules.allowText(), QStringLiteral("draft"));

    QVERIFY(rules.refresh());
    const auto refresh = dispatcher.last();
    QCOMPARE(
        refresh.value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readClaudeAutoModeRulesBound"));
    rules.ingestAgentIntelEvent(reply(
        requestId(refresh),
        autoRead(
            uuidV7(),
            QStringLiteral("fedcba9876543210"),
            QJsonObject {
                {QStringLiteral("allow"),
                 QJsonArray {QStringLiteral("authoritative")}},
            })));
    QCOMPARE(rules.allowText(), QStringLiteral("draft"));
    QVERIFY(rules.dirty());

    QVERIFY(rules.refresh(true));
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("0123456789abcdef"),
            QJsonObject {
                {QStringLiteral("environment"), QStringLiteral("wrong")},
            })));
    QCOMPARE(rules.state(), kodosi::AgentAutoModeRulesModel::State::Failed);

    QVERIFY(rules.refresh(true));
    QJsonArray oversized;
    for (auto index = 0; index < 1'025; ++index) {
        oversized.push_back(QStringLiteral("rule"));
    }
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("0123456789abcdef"),
            QJsonObject {{QStringLiteral("allow"), oversized}})));
    QCOMPARE(rules.state(), kodosi::AgentAutoModeRulesModel::State::Failed);

    rules.close();
    QCOMPARE(rules.state(), kodosi::AgentAutoModeRulesModel::State::Dormant);
    const auto beforeReopen = dispatcher.commands.size();
    QVERIFY(rules.refresh());
    QCOMPARE(dispatcher.commands.size(), beforeReopen + 1);
}

void ProjectIntelligenceModelTest::
    autoModeReceiptsAndStructuredReconciliation()
{
    ProjectDispatcher dispatcher;
    kodosi::AgentAutoModeRulesModel rules(dispatcher);
    rules.ingestAuthEvent(auth());
    QVERIFY(rules.refresh());
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("0123456789abcdef"),
            QJsonObject {})));
    rules.setAllowText(QStringLiteral("Read"));
    QVERIFY(rules.save());
    const auto write = dispatcher.last();
    const auto mutationId =
        write.value(QStringLiteral("mutationId")).toString();
    rules.ingestAgentIntelEvent(runtimeError(
        requestId(write),
        QStringLiteral("deliveryAmbiguous"),
        mutationId,
        true,
        QStringLiteral("delivery interrupted")));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.reconcileClaudeAutoModeRulesWrite"));
    const QJsonObject savedRules {
        {QStringLiteral("allow"), QJsonArray {QStringLiteral("Read")}},
    };
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoReceipt(
            mutationId,
            QStringLiteral("applied"),
            QStringLiteral("1111111111111111"),
            savedRules,
            QJsonValue::Null)));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readClaudeAutoModeRulesBound"));
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("1111111111111111"),
            savedRules)));
    QCOMPARE(rules.statusMessage(), QStringLiteral("Rules saved."));
    QVERIFY(!rules.dirty());

    ProjectDispatcher indeterminateDispatcher;
    kodosi::AgentAutoModeRulesModel indeterminate(indeterminateDispatcher);
    indeterminate.ingestAuthEvent(auth());
    QVERIFY(indeterminate.refresh());
    indeterminate.ingestAgentIntelEvent(reply(
        requestId(indeterminateDispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("2222222222222222"),
            QJsonObject {})));
    indeterminate.setEnvironmentText(QStringLiteral("draft"));
    QVERIFY(indeterminate.save());
    const auto mutation =
        indeterminateDispatcher.last()
            .value(QStringLiteral("mutationId")).toString();
    const QJsonObject candidateRules {
        {QStringLiteral("environment"),
         QJsonArray {QStringLiteral("draft")}},
    };
    indeterminate.ingestAgentIntelEvent(reply(
        requestId(indeterminateDispatcher.last()),
        autoReceipt(
            mutation,
            QStringLiteral("indeterminate"),
            QStringLiteral("3333333333333333"),
            candidateRules,
            QStringLiteral("durability could not be confirmed"))));
    QCOMPARE(
        indeterminateDispatcher.count(
            QStringLiteral("agent.intel.reconcileClaudeAutoModeRulesWrite")),
        0);
    indeterminate.ingestAgentIntelEvent(reply(
        requestId(indeterminateDispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("4444444444444444"),
            QJsonObject {
                {QStringLiteral("environment"),
                 QJsonArray {QStringLiteral("authoritative")}},
            })));
    QCOMPARE(
        indeterminate.environmentText(),
        QStringLiteral("authoritative"));
    QCOMPARE(
        indeterminate.statusMessage(),
        QStringLiteral("durability could not be confirmed"));
}

void ProjectIntelligenceModelTest::
    autoModeMutationDeliveryOutlivesSurfaceDemand()
{
    ProjectDispatcher dispatcher;
    kodosi::AgentAutoModeRulesModel rules(dispatcher);
    rules.ingestAuthEvent(auth());
    QVERIFY(rules.refresh());
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("0123456789abcdef"),
            QJsonObject {})));
    rules.setAllowText(QStringLiteral("Read"));
    QVERIFY(rules.save());
    const auto write = dispatcher.last();
    const auto mutationId =
        write.value(QStringLiteral("mutationId")).toString();

    rules.close();
    QCOMPARE(rules.state(), kodosi::AgentAutoModeRulesModel::State::Dormant);
    QVERIFY(rules.saving());
    rules.ingestAgentIntelEvent(runtimeError(
        requestId(write),
        QStringLiteral("deliveryAmbiguous"),
        mutationId,
        true,
        QStringLiteral("delivery interrupted")));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.reconcileClaudeAutoModeRulesWrite"));
    const auto reconcile = dispatcher.last();
    rules.close();
    rules.ingestAgentIntelEvent(reply(
        requestId(reconcile),
        autoReceipt(
            mutationId,
            QStringLiteral("applied"),
            QStringLiteral("1111111111111111"),
            QJsonObject {
                {QStringLiteral("allow"),
                 QJsonArray {QStringLiteral("Read")}},
            },
            QJsonValue::Null)));
    QVERIFY(!rules.saving());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.reconcileClaudeAutoModeRulesWrite"));

    QVERIFY(rules.refresh());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readClaudeAutoModeRulesBound"));
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("1111111111111111"),
            QJsonObject {
                {QStringLiteral("allow"),
                 QJsonArray {QStringLiteral("Read")}},
            })));
    QCOMPARE(rules.statusMessage(), QStringLiteral("Rules saved."));

    ProjectDispatcher boundedDispatcher;
    kodosi::AgentAutoModeRulesModel bounded(boundedDispatcher, 1);
    bounded.ingestAuthEvent(auth());
    QVERIFY(bounded.refresh());
    bounded.ingestAgentIntelEvent(reply(
        requestId(boundedDispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("2222222222222222"),
            QJsonObject {})));
    bounded.setAllowText(QStringLiteral("Write"));
    QVERIFY(bounded.save());
    bounded.close();
    QTRY_COMPARE_WITH_TIMEOUT(
        boundedDispatcher.count(
            QStringLiteral(
                "agent.intel.reconcileClaudeAutoModeRulesWrite")),
        3,
        200);
    QTRY_VERIFY_WITH_TIMEOUT(!bounded.saving(), 200);
}

void ProjectIntelligenceModelTest::
    malformedMutationOutcomeDetailCouplingIsRejected()
{
    const auto checkAuto = [](const QString& outcome, const QJsonValue& detail) {
        ProjectDispatcher dispatcher;
        kodosi::AgentAutoModeRulesModel rules(dispatcher);
        QSignalSpy decodeErrors(
            &rules,
            &kodosi::AgentAutoModeRulesModel::decodeError);
        rules.ingestAuthEvent(auth());
        QVERIFY(rules.refresh());
        rules.ingestAgentIntelEvent(reply(
            requestId(dispatcher.last()),
            autoRead(
                uuidV7(),
                QStringLiteral("0123456789abcdef"),
                QJsonObject {})));
        rules.setAllowText(QStringLiteral("Read"));
        QVERIFY(rules.save());
        const auto write = dispatcher.last();
        rules.ingestAgentIntelEvent(reply(
            requestId(write),
            autoReceipt(
                write.value(QStringLiteral("mutationId")).toString(),
                outcome,
                QStringLiteral("1111111111111111"),
                QJsonObject {
                    {QStringLiteral("allow"),
                     QJsonArray {QStringLiteral("Read")}},
                },
                detail)));
        QCOMPARE(
            rules.state(),
            kodosi::AgentAutoModeRulesModel::State::Failed);
        QVERIFY(rules.dirty());
        QCOMPARE(decodeErrors.size(), 1);
        QCOMPARE(dispatcher.commands.size(), 2);
    };
    checkAuto(
        QStringLiteral("applied"),
        QStringLiteral("unexpected"));
    checkAuto(QStringLiteral("indeterminate"), QJsonValue::Null);
    checkAuto(QStringLiteral("indeterminate"), QStringLiteral(""));

    const auto checkCopy = [](const QString& outcome, const QJsonValue& detail) {
        ProjectDispatcher dispatcher;
        kodosi::SessionCatalogModel sessions;
        kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
        kodosi::AgentConversationModel conversation(
            dispatcher,
            sessions,
            intel);
        DesktopHarness desktop(sessions);
        kodosi::ProjectIntelligenceModel project(
            dispatcher,
            conversation,
            *desktop.integration);
        QSignalSpy decodeErrors(
            &project,
            &kodosi::ProjectIntelligenceModel::decodeError);
        activateProject(project);
        loadProjectMemory(
            dispatcher,
            project,
            uuidV7(),
            uuidV7(),
            uuidV7(),
            uuidV7(),
            uuidV7(),
            uuidV7());
        QVERIFY(project.copySelectedMemory(
            itemId(project.sources(), 1)));
        const auto copy = dispatcher.last();
        const auto commandCount = dispatcher.commands.size();
        project.closeSource();
        project.ingestAgentIntelEvent(reply(
            requestId(copy),
            QJsonObject {
                {QStringLiteral("mutationId"),
                 copy.value(QStringLiteral("mutationId")).toString()},
                {QStringLiteral("outcome"), outcome},
                {QStringLiteral("filename"),
                 QStringLiteral("MEMORY.md")},
                {QStringLiteral("targetLabel"),
                 QStringLiteral("Destination")},
                {QStringLiteral("detail"), detail},
            }));
        QCOMPARE(decodeErrors.size(), 1);
        QCOMPARE(dispatcher.commands.size(), commandCount);
        QCOMPARE(
            project.state(),
            kodosi::ProjectIntelligenceModel::State::Dormant);
    };
    checkCopy(
        QStringLiteral("copied"),
        QStringLiteral("unexpected"));
    checkCopy(
        QStringLiteral("alreadyExists"),
        QStringLiteral("unexpected"));
    checkCopy(QStringLiteral("indeterminate"), QJsonValue::Null);
    checkCopy(QStringLiteral("indeterminate"), QStringLiteral(""));
}

void ProjectIntelligenceModelTest::
    aggregateRefreshDoesNotTouchAutoModeDraft()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    kodosi::AgentAutoModeRulesModel rules(dispatcher);
    rules.ingestAuthEvent(auth());
    QVERIFY(rules.refresh());
    rules.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("0123456789abcdef"),
            QJsonObject {})));
    rules.setAllowText(QStringLiteral("unsaved draft"));
    const auto autoReads = dispatcher.count(
        QStringLiteral("agent.intel.readClaudeAutoModeRulesBound"));

    project.ingestAuthEvent(auth());
    QVERIFY(project.refreshSources(true));

    QCOMPARE(rules.allowText(), QStringLiteral("unsaved draft"));
    QVERIFY(rules.dirty());
    QCOMPARE(
        dispatcher.count(
            QStringLiteral("agent.intel.readClaudeAutoModeRulesBound")),
        autoReads);
}

void ProjectIntelligenceModelTest::
    projectCustomizationProjectionContainsOnlyMcp()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);

    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session")}),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            {},
            {},
            {},
            QJsonArray {
                customization(
                    QStringLiteral("skill"),
                    QStringLiteral("validate-api")),
                customization(
                    QStringLiteral("MCP server"),
                    QStringLiteral("github")),
                customization(
                    QStringLiteral("plugin"),
                    QStringLiteral("review-tools")),
            })));

    QCOMPARE(project.customizations()->rowCount(), 1);
    QCOMPARE(
        project.customizations()->data(
            project.customizations()->index(0),
            kodosi::PresentationListModel::TitleRole).toString(),
        QStringLiteral("github"));
}

void ProjectIntelligenceModelTest::
    duplicateSourcesAreDistinctAndRetryNeverGuesses()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(uuidV7(), QStringLiteral("claudeArchive"), QStringLiteral("Duplicate")),
            source(uuidV7(), QStringLiteral("claudeArchive"), QStringLiteral("Duplicate")),
        })));
    QCOMPARE(project.sources()->rowCount(), 2);
    const auto firstId = itemId(project.sources(), 0);
    const auto secondId = itemId(project.sources(), 1);
    QVERIFY(!firstId.isEmpty());
    QVERIFY(firstId != secondId);

    QVERIFY(project.selectSource(firstId));
    project.ingestAgentIntelEvent(runtimeError(
        requestId(dispatcher.last())));
    QVERIFY(project.retry());
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(uuidV7(), QStringLiteral("claudeArchive"), QStringLiteral("Duplicate")),
        })));
    QCOMPARE(
        dispatcher.count(
            QStringLiteral("agent.intel.inspectProjectSourceBound")),
        1);
    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Failed);
    QVERIFY(project.error().contains(QStringLiteral("More than one")));

    ProjectDispatcher uniqueDispatcher;
    kodosi::SessionCatalogModel uniqueSessions;
    kodosi::AgentSessionIntelModel uniqueIntel(
        uniqueDispatcher,
        uniqueSessions);
    kodosi::AgentConversationModel uniqueConversation(
        uniqueDispatcher,
        uniqueSessions,
        uniqueIntel);
    DesktopHarness uniqueDesktop(uniqueSessions);
    kodosi::ProjectIntelligenceModel unique(
        uniqueDispatcher,
        uniqueConversation,
        *uniqueDesktop.integration);
    activateProject(unique);
    const auto initialToken = uuidV7();
    unique.ingestAgentIntelEvent(reply(
        requestId(uniqueDispatcher.last()),
        sourcePage(QJsonArray {
            source(
                initialToken,
                QStringLiteral("claudeArchive"),
                QStringLiteral("Only")),
        })));
    const auto stableId = itemId(unique.sources(), 0);
    QVERIFY(unique.selectSource(stableId));
    unique.ingestAgentIntelEvent(runtimeError(
        requestId(uniqueDispatcher.last())));
    QVERIFY(unique.retry());
    const auto replacementToken = uuidV7();
    unique.ingestAgentIntelEvent(reply(
        requestId(uniqueDispatcher.last()),
        sourcePage(QJsonArray {
            source(
                replacementToken,
                QStringLiteral("claudeArchive"),
                QStringLiteral("Only")),
        })));
    QCOMPARE(unique.selectedSourceId(), stableId);
    QCOMPARE(
        uniqueDispatcher.last()
            .value(QStringLiteral("selectionToken")).toString(),
        replacementToken);
    unique.ingestAgentIntelEvent(reply(
        requestId(uniqueDispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("claudeArchive"),
            QStringLiteral("Only"))));
    QCOMPARE(unique.state(), kodosi::ProjectIntelligenceModel::State::Ready);

    const auto loadedCount = uniqueDispatcher.commands.size();
    QVERIFY(unique.refreshSources());
    QCOMPARE(uniqueDispatcher.commands.size(), loadedCount);
    unique.closeSource();
    QVERIFY(unique.refreshSources());
    QCOMPARE(uniqueDispatcher.commands.size(), loadedCount + 1);
}

void ProjectIntelligenceModelTest::
    detailIdentityMismatchReacquiresBeforeRetry()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    QSignalSpy decodeErrors(
        &project,
        &kodosi::ProjectIntelligenceModel::decodeError);
    activateProject(project);
    const auto sourceToken = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                sourceToken,
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    const auto firstRead = uuidV7();
    const auto firstOpen = uuidV7();
    const auto firstCopy = uuidV7();
    const auto firstAgentDetail = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            QJsonArray {
                memorySummary(firstRead, firstOpen, firstCopy),
            },
            QJsonArray {
                agentSummary(firstAgentDetail, uuidV7()),
            })));

    QVERIFY(project.selectMemory(itemId(project.memories(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(
            firstRead,
            QStringLiteral("wrong"),
            QStringLiteral("/repo"),
            QStringLiteral("-repo"),
            QStringLiteral("OTHER.md"))));
    QVERIFY(project.memoryContent().isEmpty());
    QVERIFY(project.memoryError().contains(QStringLiteral("different selection")));
    QCOMPARE(decodeErrors.size(), 1);

    QVERIFY(project.retry());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.listProjectSourcesBound"));
    const auto reboundSource = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                reboundSource,
                QStringLiteral("active"),
                QStringLiteral("Renamed presentation"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.inspectProjectSourceBound"));
    const auto secondRead = uuidV7();
    const auto secondAgentDetail = uuidV7();
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Renamed presentation"),
            {},
            QJsonArray {
                memorySummary(secondRead, uuidV7(), uuidV7()),
            },
            QJsonArray {
                agentSummary(secondAgentDetail, uuidV7()),
            })));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readClaudeMemoryBound"));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("selectionToken")).toString(),
        secondRead);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        memoryDetail(secondRead, QStringLiteral("authoritative"))));
    QCOMPARE(project.memoryContent(), QStringLiteral("authoritative"));

    QVERIFY(project.selectAgent(itemId(project.agents(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        agentDetail(
            secondAgentDetail,
            QJsonArray {QStringLiteral("Bash")},
            QStringLiteral("different-agent"))));
    QVERIFY(project.agentPrompt().isEmpty());
    QVERIFY(project.agentDetailError().contains(QStringLiteral("different")));
    QCOMPARE(decodeErrors.size(), 2);
}

void ProjectIntelligenceModelTest::
    projectSessionEntryRequiresExactIncarnation()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);

    sessions.ingestAuthEvent(auth());
    sessions.ingestSessionEvent(sessionsEvent(QJsonArray {
        localSession(QStringLiteral("session"), secondIncarnation),
    }));
    intel.ingestAuthEvent(auth());
    intel.ingestAgentIntelEvent(liveSet(
        QStringLiteral("session"),
        secondIncarnation));
    conversation.ingestAuthEvent(auth());

    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session")}),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            QJsonArray {
                projectSession(
                    QStringLiteral("session"),
                    firstIncarnation),
            })));
    const auto commandCount = dispatcher.commands.size();
    QVERIFY(!project.openSession(itemId(project.sessions(), 0)));
    QCOMPARE(dispatcher.commands.size(), commandCount);
    QVERIFY(conversation.error().contains(QStringLiteral("incarnation")));
}

void ProjectIntelligenceModelTest::
    settingsIntegerAndNumberRemainDistinctAcrossScopes()
{
    const QJsonObject rootNode {
        {QStringLiteral("nodeId"), 0},
        {QStringLiteral("parentId"), QJsonValue::Null},
        {QStringLiteral("depth"), 0},
        {QStringLiteral("key"), QStringLiteral("Value")},
        {QStringLiteral("kind"), QStringLiteral("object")},
        {QStringLiteral("childCount"), 2},
        {QStringLiteral("stringValue"), QJsonValue::Null},
        {QStringLiteral("integerValue"), QJsonValue::Null},
        {QStringLiteral("numberValue"), QJsonValue::Null},
        {QStringLiteral("booleanValue"), QJsonValue::Null},
    };
    const QJsonObject integerNode {
        {QStringLiteral("nodeId"), 1},
        {QStringLiteral("parentId"), 0},
        {QStringLiteral("depth"), 1},
        {QStringLiteral("key"), QStringLiteral("integer")},
        {QStringLiteral("kind"), QStringLiteral("integer")},
        {QStringLiteral("childCount"), 0},
        {QStringLiteral("stringValue"), QJsonValue::Null},
        {QStringLiteral("integerValue"), -7},
        {QStringLiteral("numberValue"), QJsonValue::Null},
        {QStringLiteral("booleanValue"), QJsonValue::Null},
    };
    const QJsonObject numberNode {
        {QStringLiteral("nodeId"), 2},
        {QStringLiteral("parentId"), 0},
        {QStringLiteral("depth"), 1},
        {QStringLiteral("key"), QStringLiteral("number")},
        {QStringLiteral("kind"), QStringLiteral("number")},
        {QStringLiteral("childCount"), 0},
        {QStringLiteral("stringValue"), QJsonValue::Null},
        {QStringLiteral("integerValue"), QJsonValue::Null},
        {QStringLiteral("numberValue"), 1.5},
        {QStringLiteral("booleanValue"), QJsonValue::Null},
    };
    const QJsonObject tree {
        {QStringLiteral("nodes"),
         QJsonArray {rootNode, integerNode, numberNode}},
    };
    const QJsonArray settings {
        QJsonObject {
            {QStringLiteral("agent"), QStringLiteral("claude")},
            {QStringLiteral("settings"),
             QJsonObject {
                 {QStringLiteral("managed"), tree},
                 {QStringLiteral("user"), tree},
                 {QStringLiteral("project"), tree},
                 {QStringLiteral("local"), tree},
             }},
        },
    };

    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            {},
            {},
            settings)));
    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Ready);

    for (const auto& scope : {
             QStringLiteral("managed"),
             QStringLiteral("user"),
             QStringLiteral("project"),
             QStringLiteral("local"),
         }) {
        project.setSettingsScope(scope);
        QCOMPARE(project.settingsTree()->rowCount(), 3);
        QCOMPARE(
            project.settingsTree()
                ->data(
                    project.settingsTree()->index(1),
                    kodosi::PresentationListModel::KindRole)
                .toString(),
            QStringLiteral("integer"));
        QCOMPARE(
            project.settingsTree()
                ->data(
                    project.settingsTree()->index(1),
                    kodosi::PresentationListModel::SubtitleRole)
                .toString(),
            QStringLiteral("-7"));
        QCOMPARE(
            project.settingsTree()
                ->data(
                    project.settingsTree()->index(2),
                    kodosi::PresentationListModel::KindRole)
                .toString(),
            QStringLiteral("number"));
        QCOMPARE(
            project.settingsTree()
                ->data(
                    project.settingsTree()->index(2),
                    kodosi::PresentationListModel::SubtitleRole)
                .toString(),
            QStringLiteral("1.5"));
    }
}

void ProjectIntelligenceModelTest::
    settingsTreesRejectDuplicateAndMismatchedNodes()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    QSignalSpy decodeErrors(
        &project,
        &kodosi::ProjectIntelligenceModel::decodeError);
    activateProject(project);
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        sourcePage(QJsonArray {
            source(
                uuidV7(),
                QStringLiteral("active"),
                QStringLiteral("Project"),
                QJsonArray {QStringLiteral("session-a")}),
        })));
    QVERIFY(project.selectSource(itemId(project.sources(), 0)));
    const QJsonObject duplicateNode {
        {QStringLiteral("nodeId"), 0},
        {QStringLiteral("parentId"), QJsonValue::Null},
        {QStringLiteral("depth"), 0},
        {QStringLiteral("key"), QStringLiteral("root")},
        {QStringLiteral("kind"), QStringLiteral("string")},
        {QStringLiteral("childCount"), 0},
        {QStringLiteral("stringValue"), QStringLiteral("value")},
        {QStringLiteral("integerValue"), QJsonValue::Null},
        {QStringLiteral("numberValue"), QJsonValue::Null},
        {QStringLiteral("booleanValue"), QJsonValue::Null},
    };
    const QJsonArray settings {
        QJsonObject {
            {QStringLiteral("agent"), QStringLiteral("claude")},
            {QStringLiteral("settings"),
             QJsonObject {
                 {QStringLiteral("managed"), QJsonValue::Null},
                 {QStringLiteral("user"),
                  QJsonObject {
                      {QStringLiteral("nodes"),
                       QJsonArray {duplicateNode, duplicateNode}},
                  }},
                 {QStringLiteral("project"), QJsonValue::Null},
                 {QStringLiteral("local"), QJsonValue::Null},
             }},
        },
    };
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        projectSnapshot(
            uuidV7(),
            QStringLiteral("active"),
            QStringLiteral("Project"),
            {},
            {},
            {},
            settings)));
    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Failed);
    QVERIFY(!decodeErrors.isEmpty());
}

void ProjectIntelligenceModelTest::projectOpenHandoffsAlwaysRelease()
{
#if defined(Q_OS_LINUX)
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("project-open-XXXXXX")));
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("MEMORY.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("memory"), 6);
    file.close();
    const auto descriptor =
        ::open(path.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY(descriptor >= 0);
    const auto closeDescriptor =
        qScopeGuard([descriptor] { ::close(descriptor); });
    const auto handoffPath =
        createBoundArtifact(directory.path(), descriptor);
    QVERIFY(!handoffPath.isEmpty());

    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    loadProjectMemory(
        dispatcher,
        project,
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7());
    QVERIFY(project.openSelectedMemory());
    const auto openRequest = requestId(dispatcher.last());
    const auto handoffId = uuidV7();
    project.ingestAgentIntelEvent(reply(
        openRequest,
        QJsonObject {
            {QStringLiteral("displayName"), QStringLiteral("MEMORY.md")},
            {QStringLiteral("handoffId"), handoffId},
            {QStringLiteral("handoffPath"), handoffPath},
        }));
    QCOMPARE(desktop.openedUrls.size(), 1);
    const auto openedHandoff =
        desktop.openedUrls.constFirst().toLocalFile();
    QVERIFY(openedHandoff != handoffPath);
    QVERIFY(openedHandoff.startsWith(
        QStringLiteral("/proc/%1/fd/")
            .arg(QCoreApplication::applicationPid())));
    QFile openedFile(openedHandoff);
    QVERIFY(openedFile.open(QIODevice::ReadOnly));
    QCOMPARE(openedFile.readAll(), QByteArrayLiteral("memory"));
    QCOMPARE(
        dispatcher.count(QStringLiteral("agent.intel.releaseOpenHandoff")),
        1);
    const auto release = dispatcher.lastOfType(
        QStringLiteral("agent.intel.releaseOpenHandoff"));
    QCOMPARE(
        release.value(QStringLiteral("handoffId")).toString(),
        handoffId);
    project.ingestAgentIntelEvent(reply(
        requestId(release),
        QJsonValue::Null));
    const auto releasesBeforeClose =
        dispatcher.count(QStringLiteral("agent.intel.releaseOpenHandoff"));
    project.closeSource();
    QCOMPARE(
        dispatcher.count(QStringLiteral("agent.intel.releaseOpenHandoff")),
        releasesBeforeClose);

    ProjectDispatcher failedDispatcher;
    kodosi::SessionCatalogModel failedSessions;
    kodosi::AgentSessionIntelModel failedIntel(
        failedDispatcher,
        failedSessions);
    kodosi::AgentConversationModel failedConversation(
        failedDispatcher,
        failedSessions,
        failedIntel);
    DesktopHarness failedDesktop(failedSessions);
    failedDesktop.openerResult = false;
    kodosi::ProjectIntelligenceModel failed(
        failedDispatcher,
        failedConversation,
        *failedDesktop.integration);
    activateProject(failed);
    loadProjectMemory(
        failedDispatcher,
        failed,
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7());
    QVERIFY(failed.openSelectedMemory());
    const auto failedHandoffId = uuidV7();
    failed.ingestAgentIntelEvent(reply(
        requestId(failedDispatcher.last()),
        QJsonObject {
            {QStringLiteral("displayName"), QStringLiteral("MEMORY.md")},
            {QStringLiteral("handoffId"), failedHandoffId},
            {QStringLiteral("handoffPath"), handoffPath},
        }));
    QCOMPARE(
        failedDispatcher.count(
            QStringLiteral("agent.intel.releaseOpenHandoff")),
        1);
    failed.closeSource();
    QCOMPARE(
        failedDispatcher.count(
            QStringLiteral("agent.intel.releaseOpenHandoff")),
        2);

    ProjectDispatcher cancelledDispatcher;
    kodosi::SessionCatalogModel cancelledSessions;
    kodosi::AgentSessionIntelModel cancelledIntel(
        cancelledDispatcher,
        cancelledSessions);
    kodosi::AgentConversationModel cancelledConversation(
        cancelledDispatcher,
        cancelledSessions,
        cancelledIntel);
    DesktopHarness cancelledDesktop(cancelledSessions);
    kodosi::ProjectIntelligenceModel cancelled(
        cancelledDispatcher,
        cancelledConversation,
        *cancelledDesktop.integration);
    activateProject(cancelled);
    loadProjectMemory(
        cancelledDispatcher,
        cancelled,
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7());
    QVERIFY(cancelled.openSelectedMemory());
    const auto cancelledRequest = requestId(cancelledDispatcher.last());
    cancelled.closeSource();
    const auto cancelledHandoffId = uuidV7();
    cancelled.ingestAgentIntelEvent(reply(
        cancelledRequest,
        QJsonObject {
            {QStringLiteral("displayName"), QStringLiteral("MEMORY.md")},
            {QStringLiteral("handoffId"), cancelledHandoffId},
            {QStringLiteral("handoffPath"), handoffPath},
        }));
    QVERIFY(cancelledDesktop.openedUrls.isEmpty());
    QCOMPARE(
        cancelledDispatcher.lastOfType(
            QStringLiteral("agent.intel.releaseOpenHandoff"))
            .value(QStringLiteral("handoffId")).toString(),
        cancelledHandoffId);
#endif
}

void ProjectIntelligenceModelTest::
    externalActionsValidateNullShapesRevealAndRelease()
{
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("external-action-XXXXXX")));
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("source.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("{}"), 2);
    file.close();

    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    DesktopHarness desktop(sessions);
    kodosi::ExternalDiscoveryModel external(
        dispatcher,
        *desktop.integration);
    QSignalSpy messages(
        &external,
        &kodosi::ExternalDiscoveryModel::actionMessage);
    external.ingestAuthEvent(auth());
    QVERIFY(external.refresh());
    auto token = uuidV7();
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        externalDiscovery(token)));
    const auto sourceId = itemId(external.servers(), 0);
    QVERIFY(external.canCopySourcePath(sourceId));
    QVERIFY(external.canOpenSource(sourceId));
    QVERIFY(external.canRevealSource(sourceId));
    const auto capabilityRevision = external.capabilityRevision();
    QVERIFY(external.revealSource(sourceId));
    QVERIFY(external.capabilityRevision() > capabilityRevision);
    QVERIFY(!external.canCopySourcePath(sourceId));
    QVERIFY(!external.canOpenSource(sourceId));
    QVERIFY(!external.canRevealSource(sourceId));
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        externalAction(
            QStringLiteral("reveal"),
            path,
            QJsonValue::Null,
            QJsonValue::Null)));
    QCOMPARE(
        desktop.openedUrls.constLast(),
        QUrl::fromLocalFile(directory.path()));
    QVERIFY(!messages.constLast().at(1).toBool());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.discoverExternalBound"));

    token = uuidV7();
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        externalDiscovery(token)));
    QGuiApplication::clipboard()->setText(QStringLiteral("unchanged"));
    QVERIFY(external.copySourcePath(itemId(external.servers(), 0)));
    auto malformed = externalAction(
        QStringLiteral("copyPath"),
        path,
        QJsonValue::Null,
        QJsonValue::Null);
    malformed.remove(QStringLiteral("handoffPath"));
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        malformed));
    QCOMPARE(
        QGuiApplication::clipboard()->text(),
        QStringLiteral("unchanged"));
    QVERIFY(messages.constLast().at(1).toBool());

    token = uuidV7();
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        externalDiscovery(token)));
    QVERIFY(external.copySourcePath(itemId(external.servers(), 0)));
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        externalAction(
            QStringLiteral("copyPath"),
            path,
            QJsonValue::Null,
            QJsonValue::Null)));
    QCOMPARE(QGuiApplication::clipboard()->text(), path);

#if defined(Q_OS_LINUX)
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        externalDiscovery(uuidV7())));
    const auto descriptor =
        ::open(path.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY(descriptor >= 0);
    const auto closeDescriptor =
        qScopeGuard([descriptor] { ::close(descriptor); });
    const auto handoffPath =
        createBoundArtifact(directory.path(), descriptor);
    QVERIFY(!handoffPath.isEmpty());
    QVERIFY(external.openSource(itemId(external.servers(), 0)));
    const auto handoffId = uuidV7();
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        externalAction(
            QStringLiteral("open"),
            QJsonValue::Null,
            handoffId,
            handoffPath)));
    QCOMPARE(
        dispatcher.count(QStringLiteral("agent.intel.releaseOpenHandoff")),
        1);
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.discoverExternalBound"));
    const auto release = dispatcher.lastOfType(
        QStringLiteral("agent.intel.releaseOpenHandoff"));
    external.ingestAgentIntelEvent(reply(
        requestId(release),
        QJsonValue::Null));
    external.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        externalDiscovery(uuidV7())));
#endif

    const auto loadedCount = dispatcher.commands.size();
    QVERIFY(external.refresh());
    QCOMPARE(dispatcher.commands.size(), loadedCount);
    external.close();
    QVERIFY(external.refresh());
    QCOMPARE(dispatcher.commands.size(), loadedCount + 1);
}

void ProjectIntelligenceModelTest::
    projectMemoryCopyReceiptsAreTruthfulAndReconciled()
{
    const auto runOutcome = [](
                                const QString& outcome,
                                const QJsonValue& detail,
                                const QString& expectedText,
                                const bool expectedError) {
        ProjectDispatcher dispatcher;
        kodosi::SessionCatalogModel sessions;
        kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
        kodosi::AgentConversationModel conversation(
            dispatcher,
            sessions,
            intel);
        DesktopHarness desktop(sessions);
        kodosi::ProjectIntelligenceModel project(
            dispatcher,
            conversation,
            *desktop.integration);
        QSignalSpy messages(
            &project,
            &kodosi::ProjectIntelligenceModel::actionMessage);
        activateProject(project);
        loadProjectMemory(
            dispatcher,
            project,
            uuidV7(),
            uuidV7(),
            uuidV7(),
            uuidV7(),
            uuidV7(),
            uuidV7());
        QVERIFY(project.copySelectedMemory(itemId(project.sources(), 1)));
        const auto copy = dispatcher.last();
        const auto mutationId =
            copy.value(QStringLiteral("mutationId")).toString();
        project.ingestAgentIntelEvent(reply(
            requestId(copy),
            QJsonObject {
                {QStringLiteral("mutationId"), mutationId},
                {QStringLiteral("outcome"), outcome},
                {QStringLiteral("filename"), QStringLiteral("MEMORY.md")},
                {QStringLiteral("targetLabel"), QStringLiteral("Destination")},
                {QStringLiteral("detail"), detail},
            }));
        QVERIFY(!messages.isEmpty());
        QVERIFY(messages.constLast().at(0).toString().contains(expectedText));
        QCOMPARE(messages.constLast().at(1).toBool(), expectedError);
        QCOMPARE(
            dispatcher.last().value(QStringLiteral("type")).toString(),
            QStringLiteral("agent.intel.listProjectSourcesBound"));
    };

    runOutcome(
        QStringLiteral("copied"),
        QJsonValue::Null,
        QStringLiteral("Copied"),
        false);
    runOutcome(
        QStringLiteral("alreadyExists"),
        QJsonValue::Null,
        QStringLiteral("already exists"),
        false);
    runOutcome(
        QStringLiteral("indeterminate"),
        QStringLiteral("copy durability is unknown"),
        QStringLiteral("durability"),
        true);

    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    QSignalSpy messages(
        &project,
        &kodosi::ProjectIntelligenceModel::actionMessage);
    activateProject(project);
    loadProjectMemory(
        dispatcher,
        project,
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7());
    QVERIFY(project.copySelectedMemory(itemId(project.sources(), 1)));
    const auto copy = dispatcher.last();
    const auto mutationId =
        copy.value(QStringLiteral("mutationId")).toString();
    project.ingestAgentIntelEvent(runtimeError(
        requestId(copy),
        QStringLiteral("deliveryAmbiguous"),
        mutationId,
        true,
        QStringLiteral("delivery interrupted")));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.reconcileProjectMemoryCopy"));
    project.ingestAgentIntelEvent(reply(
        requestId(dispatcher.last()),
        QJsonObject {
            {QStringLiteral("mutationId"), mutationId},
            {QStringLiteral("outcome"), QStringLiteral("copied")},
            {QStringLiteral("filename"), QStringLiteral("MEMORY.md")},
            {QStringLiteral("targetLabel"), QStringLiteral("Destination")},
            {QStringLiteral("detail"), QJsonValue::Null},
        }));
    QVERIFY(messages.constLast().at(0).toString().contains(QStringLiteral("Copied")));
}

void ProjectIntelligenceModelTest::
    projectMemoryCopyDeliveryOutlivesSurfaceDemand()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    loadProjectMemory(
        dispatcher,
        project,
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7());
    QVERIFY(project.copySelectedMemory(itemId(project.sources(), 1)));
    const auto copy = dispatcher.last();
    const auto mutationId =
        copy.value(QStringLiteral("mutationId")).toString();

    project.closeSource();
    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Dormant);
    project.ingestAgentIntelEvent(runtimeError(
        requestId(copy),
        QStringLiteral("deliveryAmbiguous"),
        mutationId,
        true,
        QStringLiteral("delivery interrupted")));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.reconcileProjectMemoryCopy"));
    const auto reconcile = dispatcher.last();
    project.closeSource();
    project.ingestAgentIntelEvent(reply(
        requestId(reconcile),
        QJsonObject {
            {QStringLiteral("mutationId"), mutationId},
            {QStringLiteral("outcome"), QStringLiteral("copied")},
            {QStringLiteral("filename"), QStringLiteral("MEMORY.md")},
            {QStringLiteral("targetLabel"), QStringLiteral("Destination")},
            {QStringLiteral("detail"), QJsonValue::Null},
        }));
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.reconcileProjectMemoryCopy"));

    QVERIFY(project.refreshSources());
    QCOMPARE(
        dispatcher.last().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.listProjectSourcesBound"));
}

void ProjectIntelligenceModelTest::
    acceptedMutationDeliveryCancelsOnAuthorityReset()
{
    ProjectDispatcher autoDispatcher;
    kodosi::AgentAutoModeRulesModel rules(autoDispatcher);
    rules.ingestAuthEvent(auth());
    QVERIFY(rules.refresh());
    rules.ingestAgentIntelEvent(reply(
        requestId(autoDispatcher.last()),
        autoRead(
            uuidV7(),
            QStringLiteral("0123456789abcdef"),
            QJsonObject {})));
    rules.setAllowText(QStringLiteral("Read"));
    QVERIFY(rules.save());
    const auto write = autoDispatcher.last();
    const auto autoCommands = autoDispatcher.commands.size();
    rules.resetRuntimeAuthority();
    QVERIFY(!rules.saving());
    rules.ingestAgentIntelEvent(runtimeError(
        requestId(write),
        QStringLiteral("deliveryAmbiguous"),
        write.value(QStringLiteral("mutationId")).toString(),
        true,
        QStringLiteral("late old-runtime delivery")));
    QCOMPARE(autoDispatcher.commands.size(), autoCommands);

    ProjectDispatcher copyDispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(copyDispatcher, sessions);
    kodosi::AgentConversationModel conversation(
        copyDispatcher,
        sessions,
        intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        copyDispatcher,
        conversation,
        *desktop.integration);
    activateProject(project);
    loadProjectMemory(
        copyDispatcher,
        project,
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7(),
        uuidV7());
    QVERIFY(project.copySelectedMemory(itemId(project.sources(), 1)));
    const auto copy = copyDispatcher.last();
    const auto copyCommands = copyDispatcher.commands.size();
    project.resetRuntimeAuthority();
    project.ingestAgentIntelEvent(runtimeError(
        requestId(copy),
        QStringLiteral("deliveryAmbiguous"),
        copy.value(QStringLiteral("mutationId")).toString(),
        true,
        QStringLiteral("late old-runtime delivery")));
    QCOMPARE(copyDispatcher.commands.size(), copyCommands);
}

void ProjectIntelligenceModelTest::boundModelsClearOnRuntimeRestart()
{
    ProjectDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::AgentConversationModel conversation(dispatcher, sessions, intel);
    DesktopHarness desktop(sessions);
    kodosi::ProjectIntelligenceModel project(
        dispatcher,
        conversation,
        *desktop.integration);
    kodosi::AgentAutoModeRulesModel rules(dispatcher);
    kodosi::ExternalDiscoveryModel external(dispatcher, *desktop.integration);
    project.installSyntheticFixture();
    rules.installSyntheticFixture();
    external.installSyntheticFixture();

    project.resetRuntimeAuthority();
    rules.resetRuntimeAuthority();
    external.resetRuntimeAuthority();

    QCOMPARE(project.state(), kodosi::ProjectIntelligenceModel::State::Dormant);
    QCOMPARE(project.sources()->rowCount(), 0);
    QCOMPARE(project.settingsTree()->rowCount(), 0);
    QCOMPARE(rules.state(), kodosi::AgentAutoModeRulesModel::State::Dormant);
    QCOMPARE(external.state(), kodosi::ExternalDiscoveryModel::State::Dormant);
    QCOMPARE(external.sessions()->rowCount(), 0);
}

QTEST_MAIN(ProjectIntelligenceModelTest)

#include "tst_project_intelligence_model.moc"
