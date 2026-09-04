#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace kodosi {

class AgentConversationModel;
class DesktopFileIntegration;

class PresentationListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        ItemIdRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        KindRole,
        AgentRole,
        StatusRole,
        ModeRole,
        MetadataRole,
        AvailableRole,
        NumberARole,
        NumberBRole,
    };
    Q_ENUM(Role)

    explicit PresentationListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] QVariantMap presentation(const QString& itemId) const;

signals:
    void countChanged();

private:
    friend class ProjectIntelligenceModel;
    friend class ExternalDiscoveryModel;

    struct Row {
        QString itemId {};
        QString title {};
        QString subtitle {};
        QString kind {};
        QString agent {};
        QString status {};
        QString mode {};
        QString metadata {};
        bool available = false;
        qint64 numberA = 0;
        qint64 numberB = 0;
    };

    QVector<Row> m_rows;
    void replace(QVector<Row> rows);
    [[nodiscard]] qsizetype indexOf(const QString& itemId) const;
};

class ProjectIntelligenceModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(kodosi::PresentationListModel* sources READ sources CONSTANT)
    Q_PROPERTY(kodosi::PresentationListModel* copyDestinations READ copyDestinations CONSTANT)
    Q_PROPERTY(kodosi::PresentationListModel* sessions READ sessions CONSTANT)
    Q_PROPERTY(kodosi::PresentationListModel* memories READ memories CONSTANT)
    Q_PROPERTY(kodosi::PresentationListModel* agents READ agents CONSTANT)
    Q_PROPERTY(kodosi::PresentationListModel* customizations READ customizations CONSTANT)
    Q_PROPERTY(kodosi::PresentationListModel* settingsTree READ settingsTree CONSTANT)
    Q_PROPERTY(QString selectedSourceId READ selectedSourceId NOTIFY sourceChanged)
    Q_PROPERTY(QString selectedSourceLabel READ selectedSourceLabel NOTIFY sourceChanged)
    Q_PROPERTY(QString selectedSourceKind READ selectedSourceKind NOTIFY sourceChanged)
    Q_PROPERTY(bool hasSourceSnapshot READ hasSourceSnapshot NOTIFY sourceChanged)
    Q_PROPERTY(bool hasMoreSources READ hasMoreSources NOTIFY stateChanged)
    Q_PROPERTY(QString selectedMemoryId READ selectedMemoryId NOTIFY memoryChanged)
    Q_PROPERTY(QString selectedMemoryTitle READ selectedMemoryTitle NOTIFY memoryChanged)
    Q_PROPERTY(QString memoryContent READ memoryContent NOTIFY memoryChanged)
    Q_PROPERTY(bool memoryLoading READ memoryLoading NOTIFY memoryChanged)
    Q_PROPERTY(QString memoryError READ memoryError NOTIFY memoryChanged)
    Q_PROPERTY(QString selectedAgentId READ selectedAgentId NOTIFY agentChanged)
    Q_PROPERTY(QString selectedAgentName READ selectedAgentName NOTIFY agentChanged)
    Q_PROPERTY(QString selectedAgentDescription READ selectedAgentDescription NOTIFY agentChanged)
    Q_PROPERTY(QString selectedAgentModel READ selectedAgentModel NOTIFY agentChanged)
    Q_PROPERTY(QStringList selectedAgentTools READ selectedAgentTools NOTIFY agentChanged)
    Q_PROPERTY(int selectedAgentErrorCount READ selectedAgentErrorCount NOTIFY agentChanged)
    Q_PROPERTY(QStringList agentParseErrors READ agentParseErrors NOTIFY agentChanged)
    Q_PROPERTY(QString agentFrontmatter READ agentFrontmatter NOTIFY agentChanged)
    Q_PROPERTY(QString agentPrompt READ agentPrompt NOTIFY agentChanged)
    Q_PROPERTY(QString agentDetailError READ agentDetailError NOTIFY agentChanged)
    Q_PROPERTY(bool agentDetailLoading READ agentDetailLoading NOTIFY agentChanged)
    Q_PROPERTY(QString settingsAgent READ settingsAgent WRITE setSettingsAgent NOTIFY settingsFilterChanged)
    Q_PROPERTY(QString settingsScope READ settingsScope WRITE setSettingsScope NOTIFY settingsFilterChanged)
    Q_PROPERTY(QString settingsAvailabilityMessage READ settingsAvailabilityMessage NOTIFY settingsFilterChanged)

public:
    enum class State {
        Dormant,
        LoadingSources,
        SourcesReady,
        LoadingSource,
        Ready,
        Failed,
    };
    Q_ENUM(State)

    ProjectIntelligenceModel(
        CommandDispatcher& dispatcher,
        AgentConversationModel& conversation,
        DesktopFileIntegration& desktopFiles,
        qint64 replyTimeoutMs = 15'000,
        QObject* parent = nullptr);
    ~ProjectIntelligenceModel() override;

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] PresentationListModel* sources() noexcept;
    [[nodiscard]] PresentationListModel* copyDestinations() noexcept;
    [[nodiscard]] PresentationListModel* sessions() noexcept;
    [[nodiscard]] PresentationListModel* memories() noexcept;
    [[nodiscard]] PresentationListModel* agents() noexcept;
    [[nodiscard]] PresentationListModel* customizations() noexcept;
    [[nodiscard]] PresentationListModel* settingsTree() noexcept;
    [[nodiscard]] QString selectedSourceId() const;
    [[nodiscard]] QString selectedSourceLabel() const;
    [[nodiscard]] QString selectedSourceKind() const;
    [[nodiscard]] bool hasSourceSnapshot() const noexcept;
    [[nodiscard]] bool hasMoreSources() const noexcept;
    [[nodiscard]] QString selectedMemoryId() const;
    [[nodiscard]] QString selectedMemoryTitle() const;
    [[nodiscard]] QString memoryContent() const;
    [[nodiscard]] bool memoryLoading() const noexcept;
    [[nodiscard]] QString memoryError() const;
    [[nodiscard]] QString selectedAgentId() const;
    [[nodiscard]] QString selectedAgentName() const;
    [[nodiscard]] QString selectedAgentDescription() const;
    [[nodiscard]] QString selectedAgentModel() const;
    [[nodiscard]] QStringList selectedAgentTools() const;
    [[nodiscard]] int selectedAgentErrorCount() const noexcept;
    [[nodiscard]] QStringList agentParseErrors() const;
    [[nodiscard]] QString agentFrontmatter() const;
    [[nodiscard]] QString agentPrompt() const;
    [[nodiscard]] QString agentDetailError() const;
    [[nodiscard]] bool agentDetailLoading() const noexcept;
    [[nodiscard]] QString settingsAgent() const;
    void setSettingsAgent(const QString& agent);
    [[nodiscard]] QString settingsScope() const;
    void setSettingsScope(const QString& scope);
    [[nodiscard]] QString settingsAvailabilityMessage() const;

    Q_INVOKABLE [[nodiscard]] bool refreshSources(bool force = false);
    Q_INVOKABLE [[nodiscard]] bool loadMoreSources();
    Q_INVOKABLE [[nodiscard]] bool selectSource(const QString& sourceId);
    Q_INVOKABLE [[nodiscard]] bool selectSourceForSession(const QString& sessionId);
    Q_INVOKABLE void closeSource();
    Q_INVOKABLE [[nodiscard]] bool retry();
    Q_INVOKABLE [[nodiscard]] bool openSession(const QString& itemId);
    Q_INVOKABLE [[nodiscard]] bool selectMemory(const QString& itemId);
    Q_INVOKABLE [[nodiscard]] bool openSelectedMemory();
    Q_INVOKABLE [[nodiscard]] bool canCopySelectedMemoryTo(
        const QString& destinationSourceId);
    Q_INVOKABLE [[nodiscard]] bool copySelectedMemory(const QString& destinationSourceId);
    Q_INVOKABLE [[nodiscard]] bool selectAgent(const QString& itemId);
    Q_INVOKABLE [[nodiscard]] bool openSelectedAgent();
    void installSyntheticFixture();

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void sourceChanged();
    void memoryChanged();
    void agentChanged();
    void settingsFilterChanged();
    void actionMessage(QString message, bool error);
    void decodeError(QString message);

private:
    enum class Operation {
        ListSources,
        InspectSource,
        ReadMemory,
        ReadAgent,
        OpenMemory,
        OpenAgent,
        CopyMemory,
        ReconcileCopy,
    };

    struct Source {
        QString id {};
        QString selectionToken {};
        QString kind {};
        QString agent {};
        QString label {};
        quint64 sessionCount = 0;
        quint32 memoryCount = 0;
        QStringList activeSessionIds {};
        quint32 occurrence = 0;
        bool ambiguousAuthorityIdentity = false;
    };
    struct Session {
        QString id {};
        QString runtimeSessionId {};
        QString runtimeIncarnationId {};
        QString title {};
        QString agent {};
        QString status {};
        QString mode {};
        QString metadata {};
        bool transcriptAvailable = false;
    };
    struct Memory {
        QString id {};
        QString canonicalCwd {};
        QString projectSlug {};
        QString filename {};
        QString kind {};
        bool canonicalIdentityKnown = false;
        bool memoryTypeIsNull = true;
        QString readToken {};
        QString openToken {};
        QString copyToken {};
    };
    struct Agent {
        QString id {};
        QString target {};
        QString name {};
        QString description {};
        QString model {};
        bool modelIsNull = true;
        QStringList tools {};
        QStringList disallowedTools {};
        bool disallowedToolsKnown = false;
        quint32 errorCount = 0;
        QString detailToken {};
        QString openToken {};
    };
    struct MemoryDetailCache {
        QString content {};
        QString canonicalCwd {};
        QString projectSlug {};
        QString filename {};
        QString memoryType {};
        bool memoryTypeIsNull = true;
        qsizetype byteSize = 0;
    };
    struct AgentDetailCache {
        QString target {};
        QString name {};
        QString description {};
        QString model {};
        bool modelIsNull = true;
        QStringList tools {};
        QStringList disallowedTools {};
        QStringList parseErrors {};
        QString frontmatter {};
        QString prompt {};
        qsizetype byteSize = 0;
    };
    struct LearnedMemoryIdentity {
        QString canonicalCwd {};
        QString projectSlug {};
        QString filename {};
        QString memoryType {};
        bool memoryTypeIsNull = true;
    };
    struct LearnedAgentIdentity {
        QString target {};
        QString name {};
        QString description {};
        QString model {};
        bool modelIsNull = true;
        QStringList tools {};
        QStringList disallowedTools {};
    };
    struct SettingsNode {
        QString agent {};
        QString scope {};
        quint32 nodeId = 0;
        std::optional<quint32> parentId {};
        quint16 depth = 0;
        QString key {};
        QString kind {};
        quint32 childCount = 0;
        QString value {};
    };
    struct Pending {
        QString requestId {};
        Operation operation = Operation::ListSources;
        quint64 runtimeGeneration = 0;
        quint64 demandGeneration = 0;
        quint8 reconcileAttempts = 0;
        QString itemId {};
        QString sourceId {};
        QString mutationId {};
        QString consumedToken {};
        QString sourceKind {};
        QString sourceAgent {};
        QString sourceLabel {};
        QStringList sourceActiveSessionIds {};
        quint32 sourceOccurrence = 0;
        bool sourceIdentityAmbiguous = false;
        QString expectedCanonicalCwd {};
        QString expectedProjectSlug {};
        bool expectedCanonicalIdentityKnown = false;
        QString expectedFilename {};
        QString expectedMemoryType {};
        bool expectedMemoryTypeIsNull = true;
        QString expectedTarget {};
        QString expectedName {};
        QString expectedDescription {};
        QString expectedModel {};
        bool expectedModelIsNull = true;
        QStringList expectedTools {};
        QStringList expectedDisallowedTools {};
        bool expectedDisallowedToolsKnown = false;
        QString expectedTargetLabel {};
        bool append = false;
        bool reacquired = false;
    };

    static constexpr qsizetype maximumSources = 1'024;
    static constexpr qsizetype maximumSessions = 500;
    static constexpr qsizetype maximumMemories = 512;
    static constexpr qsizetype maximumAgents = 128;
    static constexpr qsizetype maximumCustomizations = 512;
    static constexpr qsizetype maximumSettingsNodes = 8'192;
    static constexpr qsizetype maximumContentBytes = 4 * 1024 * 1024;
    static constexpr qsizetype maximumDetailCacheEntries = 32;
    static constexpr qsizetype maximumDetailCacheBytes = 16 * 1024 * 1024;
    static constexpr qsizetype maximumLearnedItemIdentities = 2'048;

    CommandDispatcher& m_dispatcher;
    AgentConversationModel& m_conversation;
    DesktopFileIntegration& m_desktopFiles;
    AccountContextFence m_accountFence {256};
    PresentationListModel m_sourcesModel;
    PresentationListModel m_copyDestinationsModel;
    PresentationListModel m_sessionsModel;
    PresentationListModel m_memoriesModel;
    PresentationListModel m_agentsModel;
    PresentationListModel m_customizationsModel;
    PresentationListModel m_settingsTreeModel;
    QVector<Source> m_sources;
    QVector<Session> m_sessions;
    QVector<Memory> m_memories;
    QVector<Agent> m_agents;
    QVector<SettingsNode> m_settingsNodes;
    std::optional<Pending> m_pending;
    std::optional<Pending> m_mutationDelivery;
    std::optional<Pending> m_failedPending;
    std::optional<Pending> m_reacquireAfterList;
    std::optional<Pending> m_resumeAfterSnapshot;
    QVector<Source> m_rebindSources;
    QSet<QString> m_rebindUsedSourceIds;
    QSet<QString> m_sourceSelectionTokens;
    QSet<QString> m_seenSourceCursors;
    QHash<QString, QString> m_stableIds;
    QHash<QString, MemoryDetailCache> m_memoryDetailCache;
    QHash<QString, AgentDetailCache> m_agentDetailCache;
    QHash<QString, LearnedMemoryIdentity> m_learnedMemoryIdentities;
    QHash<QString, LearnedAgentIdentity> m_learnedAgentIdentities;
    QStringList m_memoryDetailCacheOrder;
    QStringList m_agentDetailCacheOrder;
    QStringList m_learnedMemoryIdentityOrder;
    QStringList m_learnedAgentIdentityOrder;
    qsizetype m_memoryDetailCacheBytes = 0;
    qsizetype m_agentDetailCacheBytes = 0;
    QHash<QString, QString> m_releaseRequests;
    QSet<QString> m_ownedHandoffs;
    QHash<QString, Operation> m_cancelledOpenRequests;
    QString m_nextCursor;
    QString m_selectedSourceId;
    QString m_selectedMemoryId;
    QString m_selectedAgentId;
    QString m_memoryContent;
    QString m_memoryError;
    QString m_agentDescription;
    QString m_agentModel;
    QStringList m_agentTools;
    QStringList m_agentParseErrors;
    QString m_agentFrontmatter;
    QString m_agentPrompt;
    QString m_agentDetailError;
    QString m_settingsAgent = QStringLiteral("claude");
    QString m_settingsScope = QStringLiteral("user");
    QString m_error;
    QTimer m_replyTimer;
    QTimer m_mutationTimer;
    qint64 m_replyTimeoutMs;
    quint64 m_runtimeGeneration = 1;
    quint64 m_demandGeneration = 0;
    State m_state = State::Dormant;
    bool m_hasAccountContext = false;
    bool m_memoryLoading = false;
    bool m_agentDetailLoading = false;
    bool m_hasMoreSources = false;
    bool m_demanded = false;
    bool m_sourcesDirtyAfterMutation = false;
    bool m_hasSourceSnapshot = false;
    bool m_invalidateDetailsAfterRefresh = false;

    [[nodiscard]] bool beginListSources(bool append);
    [[nodiscard]] bool beginInspect(
        const QString& sourceId,
        bool preserveDetails = false);
    [[nodiscard]] bool beginItemOperation(
        Operation operation,
        const QString& itemId,
        const QString& token,
        QJsonObject extra,
        Pending pending);
    [[nodiscard]] bool send(QJsonObject command, Pending pending);
    [[nodiscard]] bool sendMutation(QJsonObject command, Pending pending);
    [[nodiscard]] bool dispatchCopyReconcile(
        Pending mutation,
        QString statusMessage);
    void settleCopyDelivery(QString message, bool malformed = false);
    void refreshDirtySourcesIfIdle();
    void cancelPending();
    void rememberFailure(const Pending& pending);
    void recoverConsumedOperation(const Pending& pending);
    void populatePendingIdentity(Pending& pending);
    [[nodiscard]] bool reacquireItemOperation(Pending pending);
    [[nodiscard]] bool resumeAfterSnapshot();
    [[nodiscard]] std::optional<qsizetype> uniqueReacquiredSource(
        const Pending& pending,
        bool& ambiguous) const;
    void activateAccount(QString userId, quint64 epoch);
    void applyAgentIntelEvent(const QByteArray& json);
    void applyReply(const QJsonObject& payload, const Pending& pending);
    void applySourcePage(const QJsonObject& payload, bool append);
    void applySnapshot(const QJsonObject& payload, const Pending& pending);
    void applyMemoryDetail(const QJsonObject& payload, const Pending& pending);
    void applyAgentDetail(const QJsonObject& payload, const Pending& pending);
    void applyOpen(const QJsonObject& payload, const Pending& pending);
    void applyCopy(const QJsonObject& payload, const Pending& pending);
    void applyError(const QJsonObject& object, const Pending& pending);
    [[nodiscard]] bool handleReleaseEvent(const QJsonObject& object);
    [[nodiscard]] bool handleCancelledOpenEvent(const QJsonObject& object);
    void dispatchRelease(const QString& handoffId);
    void releaseOwnedHandoffsBestEffort();
    void rebuildSourceRows();
    void rebuildSettingsTree();
    [[nodiscard]] std::optional<MemoryDetailCache> cachedMemoryDetail(
        Memory& memory,
        bool allowCachedContent = true);
    [[nodiscard]] std::optional<AgentDetailCache> cachedAgentDetail(
        Agent& agent,
        bool allowCachedContent = true);
    void storeMemoryDetail(
        Memory& memory,
        QString content,
        QString canonicalCwd,
        QString projectSlug);
    void storeAgentDetail(
        Agent& agent,
        QStringList disallowedTools,
        QStringList parseErrors,
        QString frontmatter,
        QString prompt);
    void invalidateRefreshedDetailState();
    void clearDetailCaches();
    void clearSourceDetails();
    void clearAll();
    void setState(State state, QString error = {});
    [[nodiscard]] bool pendingCurrent(const Pending& pending) const;
    [[nodiscard]] bool mutationCurrent(const Pending& pending) const;
    [[nodiscard]] QString stableId(const QString& key);
    [[nodiscard]] Source* sourceForId(const QString& id);
    [[nodiscard]] const Source* sourceForId(const QString& id) const;
    [[nodiscard]] Memory* memoryForId(const QString& id);
    [[nodiscard]] Agent* agentForId(const QString& id);
};

} // namespace kodosi
