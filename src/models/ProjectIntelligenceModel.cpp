#include "models/ProjectIntelligenceModel.hpp"

#include "models/AgentConversationModel.hpp"
#include "platform/DesktopFileIntegration.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype identifierLimit = 4'096;
constexpr qsizetype labelLimit = 2'048;
constexpr qsizetype metadataLimit = 8'192;
constexpr qsizetype accountLimit = 1'024;
constexpr qsizetype maximumReleaseRequests = 32;
constexpr qsizetype maximumSettingsTreeNodes = 2'048;
constexpr qsizetype maximumSettingsStringBytes = 64 * 1024;
constexpr qsizetype maximumSettingsTreeBytes = 512 * 1024;
constexpr quint64 maximumSettingsTreeDepth = 12;
constexpr qsizetype maximumCustomAgentDetailBytes = 7 * 1024 * 1024;
constexpr qsizetype maximumCustomAgentSourceBytes = 1024 * 1024;
constexpr qsizetype maximumCustomAgentFrontmatterBytes = 64 * 1024;
constexpr qsizetype maximumCustomAgentDetailTextBytes = 64 * 1024;
constexpr quint64 maximumExactJsonInteger = 9'007'199'254'740'991ULL;
constexpr quint8 maximumCopyReconcileAttempts = 3;

bool bounded(const QStringView value, const qsizetype maximum, const bool empty = false)
{
    return (empty || !value.isEmpty()) && !value.contains(QChar::Null)
        && value.toUtf8().size() <= maximum;
}

bool exactFields(
    const QJsonObject& object,
    const std::initializer_list<QString> fields)
{
    const auto keys = object.keys();
    return QSet<QString>(keys.cbegin(), keys.cend())
        == QSet<QString>(fields.begin(), fields.end());
}

bool exactAccountEnvelope(
    const QJsonObject& object,
    const std::initializer_list<QString> fields)
{
    QSet<QString> expected {
        QStringLiteral("authority"),
        QStringLiteral("accountEpoch"),
        QStringLiteral("type"),
        QStringLiteral("requestId"),
    };
    if (object.contains(QStringLiteral("accountUserId"))) {
        expected.insert(QStringLiteral("accountUserId"));
    }
    for (const auto& field : fields) {
        expected.insert(field);
    }
    const auto keys = object.keys();
    return QSet<QString>(keys.cbegin(), keys.cend()) == expected;
}

std::optional<QString> stringField(
    const QJsonObject& object,
    const QString& key,
    const qsizetype maximum = identifierLimit,
    const bool empty = false)
{
    const auto value = object.value(key);
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto text = value.toString();
    return bounded(text, maximum, empty)
        ? std::optional<QString>(text)
        : std::nullopt;
}

bool nullableString(
    const QJsonObject& object,
    const QString& key,
    QString& output,
    const qsizetype maximum = identifierLimit)
{
    const auto value = object.value(key);
    if (value.isNull()) {
        output.clear();
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    output = value.toString();
    return bounded(output, maximum, true);
}

bool nullableAccount(
    const QJsonObject& object,
    const QString& key,
    QString& output)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        output.clear();
        return true;
    }
    return value.isString()
        && bounded(value.toString(), accountLimit, false)
        && ((output = value.toString()), true);
}

bool canonicalUuidV7(const QString& value)
{
    const QUuid id(value);
    return !id.isNull() && id.version() == QUuid::UnixEpoch
        && id.toString(QUuid::WithoutBraces) == value;
}

bool validCanonicalWorkingDirectory(
    const QString& path,
    const QString& sourceKind)
{
    if (sourceKind == QStringLiteral("claudeArchive")) {
        return path.isEmpty();
    }
    if (sourceKind != QStringLiteral("active") || path.isEmpty()) {
        return false;
    }
    const auto portable = QDir::fromNativeSeparators(path);
    return QDir::isAbsolutePath(portable)
        && QDir::cleanPath(portable) == portable
        && QDir::toNativeSeparators(portable) == path;
}

bool validProjectSlug(const QString& slug)
{
    return bounded(slug, identifierLimit, false)
        && slug != QStringLiteral(".")
        && slug != QStringLiteral("..")
        && !slug.contains(QLatin1Char('/'))
        && !slug.contains(QLatin1Char('\\'));
}

qsizetype encodedBytes(const QString& value)
{
    return value.toUtf8().size();
}

qsizetype encodedBytes(const QStringList& values)
{
    qsizetype total = 0;
    for (const auto& value : values) {
        total += encodedBytes(value);
    }
    return total;
}

std::optional<quint64> unsignedValue(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0
        || number > static_cast<double>(maximumExactJsonInteger)
        || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<quint64>(number);
}

std::optional<qint64> signedValue(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number)
        || number < -static_cast<double>(maximumExactJsonInteger)
        || number > static_cast<double>(maximumExactJsonInteger)
        || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<qint64>(number);
}

bool overlaps(
    const QStringList& left,
    const QStringList& right)
{
    return std::ranges::any_of(left, [&right](const QString& value) {
        return right.contains(value);
    });
}

QString trText(const char* value)
{
    return QCoreApplication::translate("ProjectIntelligenceModel", value);
}

QString joinedMetadata(
    const QString& first,
    const QString& second,
    const QString& third = {})
{
    QStringList values;
    for (const auto& value : {first, second, third}) {
        if (!value.isEmpty()) {
            values.push_back(value);
        }
    }
    return values.join(QStringLiteral(" · "));
}

} // namespace

PresentationListModel::PresentationListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PresentationListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant PresentationListModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const auto& row = m_rows.at(index.row());
    switch (role) {
    case ItemIdRole: return row.itemId;
    case TitleRole: return row.title;
    case SubtitleRole: return row.subtitle;
    case KindRole: return row.kind;
    case AgentRole: return row.agent;
    case StatusRole: return row.status;
    case ModeRole: return row.mode;
    case MetadataRole: return row.metadata;
    case AvailableRole: return row.available;
    case NumberARole: return row.numberA;
    case NumberBRole: return row.numberB;
    default: return {};
    }
}

QHash<int, QByteArray> PresentationListModel::roleNames() const
{
    return {
        {ItemIdRole, QByteArrayLiteral("itemId")},
        {TitleRole, QByteArrayLiteral("title")},
        {SubtitleRole, QByteArrayLiteral("subtitle")},
        {KindRole, QByteArrayLiteral("kind")},
        {AgentRole, QByteArrayLiteral("agent")},
        {StatusRole, QByteArrayLiteral("status")},
        {ModeRole, QByteArrayLiteral("mode")},
        {MetadataRole, QByteArrayLiteral("metadata")},
        {AvailableRole, QByteArrayLiteral("available")},
        {NumberARole, QByteArrayLiteral("numberA")},
        {NumberBRole, QByteArrayLiteral("numberB")},
    };
}

QVariantMap PresentationListModel::presentation(const QString& itemId) const
{
    const auto index = indexOf(itemId);
    if (index < 0) {
        return {};
    }
    const auto& row = m_rows.at(index);
    return {
        {QStringLiteral("itemId"), row.itemId},
        {QStringLiteral("title"), row.title},
        {QStringLiteral("subtitle"), row.subtitle},
        {QStringLiteral("kind"), row.kind},
        {QStringLiteral("agent"), row.agent},
        {QStringLiteral("status"), row.status},
        {QStringLiteral("mode"), row.mode},
        {QStringLiteral("metadata"), row.metadata},
        {QStringLiteral("available"), row.available},
        {QStringLiteral("numberA"), row.numberA},
        {QStringLiteral("numberB"), row.numberB},
    };
}

void PresentationListModel::replace(QVector<Row> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

qsizetype PresentationListModel::indexOf(const QString& itemId) const
{
    const auto found = std::find_if(
        m_rows.cbegin(),
        m_rows.cend(),
        [&itemId](const Row& row) { return row.itemId == itemId; });
    return found == m_rows.cend() ? -1 : std::distance(m_rows.cbegin(), found);
}

ProjectIntelligenceModel::ProjectIntelligenceModel(
    CommandDispatcher& dispatcher,
    AgentConversationModel& conversation,
    DesktopFileIntegration& desktopFiles,
    const qint64 replyTimeoutMs,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_conversation(conversation)
    , m_desktopFiles(desktopFiles)
    , m_sourcesModel(this)
    , m_copyDestinationsModel(this)
    , m_sessionsModel(this)
    , m_memoriesModel(this)
    , m_agentsModel(this)
    , m_customizationsModel(this)
    , m_settingsTreeModel(this)
    , m_replyTimeoutMs(replyTimeoutMs)
{
    Q_ASSERT(replyTimeoutMs >= 0);
    Q_ASSERT(replyTimeoutMs <= std::numeric_limits<int>::max());
    m_replyTimer.setSingleShot(true);
    connect(&m_replyTimer, &QTimer::timeout, this, [this] {
        if (!m_pending || !pendingCurrent(*m_pending)) {
            return;
        }
        const auto pending = *m_pending;
        m_pending.reset();
        if (pending.operation == Operation::OpenMemory
            || pending.operation == Operation::OpenAgent) {
            if (m_cancelledOpenRequests.size() >= maximumReleaseRequests) {
                m_cancelledOpenRequests.erase(
                    m_cancelledOpenRequests.begin());
            }
            m_cancelledOpenRequests.insert(
                pending.requestId,
                pending.operation);
        }
        rememberFailure(pending);
        if (pending.operation == Operation::ReadMemory) {
            m_memoryLoading = false;
            m_memoryError = trText("The runtime did not reply in time.");
            emit memoryChanged();
        } else if (pending.operation == Operation::ReadAgent) {
            m_agentDetailLoading = false;
            m_agentDetailError = trText("The runtime did not reply in time.");
            emit agentChanged();
        } else if (pending.operation == Operation::CopyMemory
            || pending.operation == Operation::ReconcileCopy
            || pending.operation == Operation::OpenMemory
            || pending.operation == Operation::OpenAgent) {
            emit actionMessage(trText("The runtime did not reply in time."), true);
            recoverConsumedOperation(pending);
        } else {
            setState(State::Failed, trText("The runtime did not reply in time."));
        }
    });
    m_mutationTimer.setSingleShot(true);
    connect(&m_mutationTimer, &QTimer::timeout, this, [this] {
        if (!m_mutationDelivery
            || !mutationCurrent(*m_mutationDelivery)) {
            return;
        }
        auto mutation = *m_mutationDelivery;
        m_mutationDelivery.reset();
        if (!dispatchCopyReconcile(
                std::move(mutation),
                trText(
                    "The copy result was delayed. Kodosi is reconciling it."))
            && m_mutationDelivery == std::nullopt) {
            refreshDirtySourcesIfIdle();
        }
    });
}

ProjectIntelligenceModel::~ProjectIntelligenceModel()
{
    releaseOwnedHandoffsBestEffort();
}

ProjectIntelligenceModel::State ProjectIntelligenceModel::state() const noexcept
{
    return m_state;
}

QString ProjectIntelligenceModel::error() const { return m_error; }
bool ProjectIntelligenceModel::loading() const noexcept
{
    return m_state == State::LoadingSources || m_state == State::LoadingSource;
}
PresentationListModel* ProjectIntelligenceModel::sources() noexcept { return &m_sourcesModel; }
PresentationListModel* ProjectIntelligenceModel::copyDestinations() noexcept
{
    return &m_copyDestinationsModel;
}
PresentationListModel* ProjectIntelligenceModel::sessions() noexcept { return &m_sessionsModel; }
PresentationListModel* ProjectIntelligenceModel::memories() noexcept { return &m_memoriesModel; }
PresentationListModel* ProjectIntelligenceModel::agents() noexcept { return &m_agentsModel; }
PresentationListModel* ProjectIntelligenceModel::customizations() noexcept
{
    return &m_customizationsModel;
}
PresentationListModel* ProjectIntelligenceModel::settingsTree() noexcept
{
    return &m_settingsTreeModel;
}
QString ProjectIntelligenceModel::selectedSourceId() const { return m_selectedSourceId; }
QString ProjectIntelligenceModel::selectedSourceLabel() const
{
    const auto* source = sourceForId(m_selectedSourceId);
    return source ? source->label : QString {};
}
QString ProjectIntelligenceModel::selectedSourceKind() const
{
    const auto* source = sourceForId(m_selectedSourceId);
    return source ? source->kind : QString {};
}
bool ProjectIntelligenceModel::hasSourceSnapshot() const noexcept
{
    return m_hasSourceSnapshot;
}
bool ProjectIntelligenceModel::hasMoreSources() const noexcept { return m_hasMoreSources; }
QString ProjectIntelligenceModel::selectedMemoryId() const { return m_selectedMemoryId; }
QString ProjectIntelligenceModel::selectedMemoryTitle() const
{
    auto* self = const_cast<ProjectIntelligenceModel*>(this);
    const auto* memory = self->memoryForId(m_selectedMemoryId);
    return memory ? memory->filename : QString {};
}
QString ProjectIntelligenceModel::memoryContent() const { return m_memoryContent; }
bool ProjectIntelligenceModel::memoryLoading() const noexcept { return m_memoryLoading; }
QString ProjectIntelligenceModel::memoryError() const { return m_memoryError; }
QString ProjectIntelligenceModel::selectedAgentId() const { return m_selectedAgentId; }
QString ProjectIntelligenceModel::selectedAgentName() const
{
    auto* self = const_cast<ProjectIntelligenceModel*>(this);
    const auto* agent = self->agentForId(m_selectedAgentId);
    return agent
        ? (agent->name.isEmpty() ? trText("Unnamed agent") : agent->name)
        : QString {};
}
QString ProjectIntelligenceModel::selectedAgentDescription() const { return m_agentDescription; }
QString ProjectIntelligenceModel::selectedAgentModel() const { return m_agentModel; }
QStringList ProjectIntelligenceModel::selectedAgentTools() const { return m_agentTools; }
int ProjectIntelligenceModel::selectedAgentErrorCount() const noexcept
{
    auto* self = const_cast<ProjectIntelligenceModel*>(this);
    const auto* agent = self->agentForId(m_selectedAgentId);
    return agent ? static_cast<int>(agent->errorCount) : 0;
}
QStringList ProjectIntelligenceModel::agentParseErrors() const
{
    return m_agentParseErrors;
}
QString ProjectIntelligenceModel::agentFrontmatter() const { return m_agentFrontmatter; }
QString ProjectIntelligenceModel::agentPrompt() const { return m_agentPrompt; }
QString ProjectIntelligenceModel::agentDetailError() const { return m_agentDetailError; }
bool ProjectIntelligenceModel::agentDetailLoading() const noexcept
{
    return m_agentDetailLoading;
}
QString ProjectIntelligenceModel::settingsAgent() const { return m_settingsAgent; }
QString ProjectIntelligenceModel::settingsScope() const { return m_settingsScope; }

void ProjectIntelligenceModel::setSettingsAgent(const QString& agent)
{
    if ((agent != QStringLiteral("claude") && agent != QStringLiteral("copilot"))
        || m_settingsAgent == agent) {
        return;
    }
    m_settingsAgent = agent;
    rebuildSettingsTree();
    emit settingsFilterChanged();
}

void ProjectIntelligenceModel::setSettingsScope(const QString& scope)
{
    static const QSet<QString> scopes {
        QStringLiteral("managed"),
        QStringLiteral("user"),
        QStringLiteral("project"),
        QStringLiteral("local"),
    };
    if (!scopes.contains(scope) || m_settingsScope == scope) {
        return;
    }
    m_settingsScope = scope;
    rebuildSettingsTree();
    emit settingsFilterChanged();
}

QString ProjectIntelligenceModel::settingsAvailabilityMessage() const
{
    if (m_selectedSourceId.isEmpty()
        || selectedSourceKind() != QStringLiteral("active")) {
        return trText(
            "Select an active local session to inspect its bound settings snapshot.");
    }
    if (m_settingsTreeModel.rowCount() == 0) {
        return trText("No values for the %1 scope.").arg(m_settingsScope);
    }
    return {};
}

bool ProjectIntelligenceModel::refreshSources(const bool force)
{
    const auto wasDemanded = m_demanded;
    m_demanded = true;
    if (!m_hasAccountContext) {
        return false;
    }
    if (force) {
        m_invalidateDetailsAfterRefresh = true;
        if (m_resumeAfterSnapshot
            && (m_resumeAfterSnapshot->operation == Operation::ReadMemory
                || m_resumeAfterSnapshot->operation == Operation::OpenMemory
                || m_resumeAfterSnapshot->operation == Operation::ReadAgent
                || m_resumeAfterSnapshot->operation == Operation::OpenAgent)) {
            m_resumeAfterSnapshot.reset();
        }
    }
    if (m_pending) {
        if (!force) {
            return true;
        }
        ++m_demandGeneration;
        cancelPending();
    }
    if (!force && !m_sourcesDirtyAfterMutation && wasDemanded
        && (m_state == State::SourcesReady || m_state == State::Ready)) {
        return true;
    }
    m_nextCursor.clear();
    m_hasMoreSources = false;
    const auto started = beginListSources(false);
    if (started) {
        m_sourcesDirtyAfterMutation = false;
    }
    return started;
}

bool ProjectIntelligenceModel::loadMoreSources()
{
    return m_hasMoreSources && !m_nextCursor.isEmpty() && !m_pending
        && beginListSources(true);
}

bool ProjectIntelligenceModel::selectSource(const QString& sourceId)
{
    if (sourceId == m_selectedSourceId && m_state == State::Ready) {
        return true;
    }
    if (sourceForId(sourceId) == nullptr || m_pending) {
        return false;
    }
    if (sourceId != m_selectedSourceId) {
        m_reacquireAfterList.reset();
        m_resumeAfterSnapshot.reset();
        m_failedPending.reset();
    }
    return beginInspect(sourceId);
}

bool ProjectIntelligenceModel::selectSourceForSession(const QString& sessionId)
{
    if (m_hasMoreSources) {
        emit actionMessage(
            trText("Load the complete project catalog before matching this session."),
            false);
        return false;
    }
    QVector<QString> matches;
    for (const auto& source : m_sources) {
        if (source.kind == QStringLiteral("active")
            && source.activeSessionIds.contains(sessionId)) {
            matches.push_back(source.id);
        }
    }
    if (matches.size() != 1) {
        if (matches.size() > 1) {
            emit actionMessage(
                trText("More than one active project contains this session."),
                true);
        }
        return false;
    }
    return selectSource(matches.constFirst());
}

void ProjectIntelligenceModel::closeSource()
{
    releaseOwnedHandoffsBestEffort();
    m_demanded = false;
    ++m_demandGeneration;
    clearAll();
}

bool ProjectIntelligenceModel::retry()
{
    if (m_pending) {
        return false;
    }
    if (m_reacquireAfterList) {
        if (m_reacquireAfterList->operation == Operation::ReadMemory
            || m_reacquireAfterList->operation == Operation::OpenMemory
            || m_reacquireAfterList->operation == Operation::ReadAgent
            || m_reacquireAfterList->operation == Operation::OpenAgent) {
            auto pending =
                std::exchange(m_reacquireAfterList, std::nullopt);
            return reacquireItemOperation(std::move(*pending));
        }
        return beginListSources(false);
    }
    if (!m_failedPending && m_resumeAfterSnapshot) {
        m_failedPending = m_resumeAfterSnapshot;
        m_resumeAfterSnapshot.reset();
    }
    if (m_failedPending) {
        auto pending = std::exchange(m_failedPending, std::nullopt);
        if (pending->operation == Operation::ReadMemory
            || pending->operation == Operation::OpenMemory
            || pending->operation == Operation::ReadAgent
            || pending->operation == Operation::OpenAgent) {
            return reacquireItemOperation(std::move(*pending));
        }
        m_reacquireAfterList = std::move(*pending);
        m_nextCursor.clear();
        m_hasMoreSources = false;
        return beginListSources(false);
    }
    if (!m_selectedSourceId.isEmpty()) {
        const auto* source = sourceForId(m_selectedSourceId);
        if (source == nullptr) {
            return false;
        }
        Pending retry {
            .operation = Operation::InspectSource,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .itemId = source->id,
            .sourceId = source->id,
            .sourceKind = source->kind,
            .sourceAgent = source->agent,
            .sourceLabel = source->label,
            .sourceActiveSessionIds = source->activeSessionIds,
            .sourceOccurrence = source->occurrence,
            .sourceIdentityAmbiguous =
                source->ambiguousAuthorityIdentity,
        };
        m_reacquireAfterList = std::move(retry);
        return beginListSources(false);
    }
    return refreshSources(true);
}

bool ProjectIntelligenceModel::openSession(const QString& itemId)
{
    const auto found = std::find_if(
        m_sessions.cbegin(),
        m_sessions.cend(),
        [&itemId](const Session& session) { return session.id == itemId; });
    if (found == m_sessions.cend() || !found->transcriptAvailable
        || found->runtimeSessionId.isEmpty()) {
        emit actionMessage(
            trText("A current matching session incarnation is required."),
            true);
        return false;
    }
    if (!m_conversation.inspect(
            found->runtimeSessionId,
            found->runtimeIncarnationId)) {
        emit actionMessage(
            trText("A current matching session incarnation is required."),
            true);
        return false;
    }
    return true;
}

bool ProjectIntelligenceModel::selectMemory(const QString& itemId)
{
    auto* memory = memoryForId(itemId);
    if (memory == nullptr) {
        emit actionMessage(
            trText("The selected Project Memory is no longer available."),
            true);
        return false;
    }
    if (m_pending) {
        emit actionMessage(
            trText("Wait for the current Project Intelligence request to finish."),
            true);
        return false;
    }
    if (m_failedPending
        && (m_failedPending->operation == Operation::ReadMemory
            || m_failedPending->operation == Operation::ReadAgent)) {
        m_failedPending.reset();
    }
    if (m_resumeAfterSnapshot
        && (m_resumeAfterSnapshot->operation == Operation::ReadMemory
            || m_resumeAfterSnapshot->operation == Operation::ReadAgent)) {
        m_resumeAfterSnapshot.reset();
    }
    m_selectedMemoryId = itemId;
    if (const auto cached = cachedMemoryDetail(*memory)) {
        m_memoryContent = cached->content;
        m_memoryError.clear();
        m_memoryLoading = false;
        emit memoryChanged();
        return true;
    }
    m_memoryContent.clear();
    m_memoryError.clear();
    m_memoryLoading = true;
    emit memoryChanged();
    if (memory->readToken.isEmpty()) {
        return reacquireItemOperation(Pending {
            .operation = Operation::ReadMemory,
            .itemId = itemId,
        });
    }
    const auto token = std::exchange(memory->readToken, {});
    return beginItemOperation(
        Operation::ReadMemory,
        itemId,
        token,
        {},
        {});
}

bool ProjectIntelligenceModel::openSelectedMemory()
{
    auto* memory = memoryForId(m_selectedMemoryId);
    if (memory == nullptr) {
        emit actionMessage(
            trText("Select an available Project Memory before opening it."),
            true);
        return false;
    }
    if (m_pending) {
        emit actionMessage(
            trText("Wait for the current Project Intelligence request to finish."),
            true);
        return false;
    }
    if (memory->openToken.isEmpty()) {
        return reacquireItemOperation(Pending {
            .operation = Operation::OpenMemory,
            .itemId = memory->id,
        });
    }
    const auto token = std::exchange(memory->openToken, {});
    return beginItemOperation(
        Operation::OpenMemory,
        memory->id,
        token,
        {},
        {});
}

bool ProjectIntelligenceModel::canCopySelectedMemoryTo(
    const QString& destinationSourceId)
{
    const auto* memory = memoryForId(m_selectedMemoryId);
    const auto* destination = sourceForId(destinationSourceId);
    return memory != nullptr && destination != nullptr && !m_pending
        && !m_mutationDelivery && sourceForId(m_selectedSourceId) != nullptr
        && !memory->copyToken.isEmpty()
        && !destination->selectionToken.isEmpty()
        && destination->id != m_selectedSourceId
        && destination->kind != QStringLiteral("copilotArchive")
        && !destination->ambiguousAuthorityIdentity;
}

bool ProjectIntelligenceModel::copySelectedMemory(const QString& destinationSourceId)
{
    auto* memory = memoryForId(m_selectedMemoryId);
    auto* destination = sourceForId(destinationSourceId);
    const auto* source = sourceForId(m_selectedSourceId);
    if (memory == nullptr) {
        emit actionMessage(
            trText("Select an available Project Memory before copying it."),
            true);
        return false;
    }
    if (m_pending || m_mutationDelivery) {
        emit actionMessage(
            trText("Wait for the current Project Intelligence request to finish."),
            true);
        return false;
    }
    if (source == nullptr) {
        emit actionMessage(
            trText("The selected project source is no longer available."),
            true);
        return false;
    }
    if (destination == nullptr
        || destination->id == m_selectedSourceId
        || destination->kind == QStringLiteral("copilotArchive")
        || destination->ambiguousAuthorityIdentity) {
        emit actionMessage(
            trText(
                "Choose an unambiguous active project or Claude project archive."),
            true);
        return false;
    }
    if (memory->copyToken.isEmpty()
        || destination->selectionToken.isEmpty()) {
        emit actionMessage(
            trText("Refresh Project Intelligence before copying this memory."),
            true);
        return false;
    }
    const auto mutationId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("agent.intel.copyProjectMemoryBound")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sourceSelectionToken"),
         std::exchange(memory->copyToken, {})},
        {QStringLiteral("destinationSelectionToken"),
         std::exchange(destination->selectionToken, {})},
        {QStringLiteral("mutationId"), mutationId},
    };
    return sendMutation(
        std::move(command),
        Pending {
            .requestId = requestId,
            .operation = Operation::CopyMemory,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .itemId = memory->id,
            .sourceId = source->id,
            .mutationId = mutationId,
            .sourceKind = source->kind,
            .sourceAgent = source->agent,
            .sourceLabel = source->label,
            .sourceActiveSessionIds = source->activeSessionIds,
            .sourceOccurrence = source->occurrence,
            .sourceIdentityAmbiguous =
                source->ambiguousAuthorityIdentity,
            .expectedFilename = memory->filename,
            .expectedTargetLabel = destination->label,
        });
}

bool ProjectIntelligenceModel::selectAgent(const QString& itemId)
{
    auto* agent = agentForId(itemId);
    if (agent == nullptr) {
        emit actionMessage(
            trText("The selected Custom Agent is no longer available."),
            true);
        return false;
    }
    if (m_pending) {
        emit actionMessage(
            trText("Wait for the current Project Intelligence request to finish."),
            true);
        return false;
    }
    if (m_failedPending
        && (m_failedPending->operation == Operation::ReadMemory
            || m_failedPending->operation == Operation::ReadAgent)) {
        m_failedPending.reset();
    }
    if (m_resumeAfterSnapshot
        && (m_resumeAfterSnapshot->operation == Operation::ReadMemory
            || m_resumeAfterSnapshot->operation == Operation::ReadAgent)) {
        m_resumeAfterSnapshot.reset();
    }
    m_selectedAgentId = itemId;
    if (const auto cached = cachedAgentDetail(*agent)) {
        m_agentDescription = cached->description;
        m_agentModel = cached->model;
        m_agentTools = cached->tools;
        m_agentParseErrors = cached->parseErrors;
        m_agentFrontmatter = cached->frontmatter;
        m_agentPrompt = cached->prompt;
        m_agentDetailError.clear();
        m_agentDetailLoading = false;
        emit agentChanged();
        return true;
    }
    m_agentDescription = agent->description;
    m_agentModel = agent->model;
    m_agentTools = agent->tools;
    m_agentParseErrors.clear();
    m_agentFrontmatter.clear();
    m_agentPrompt.clear();
    m_agentDetailError.clear();
    m_agentDetailLoading = true;
    emit agentChanged();
    if (agent->detailToken.isEmpty()) {
        return reacquireItemOperation(Pending {
            .operation = Operation::ReadAgent,
            .itemId = itemId,
        });
    }
    const auto token = std::exchange(agent->detailToken, {});
    return beginItemOperation(
        Operation::ReadAgent,
        itemId,
        token,
        {},
        {});
}

bool ProjectIntelligenceModel::openSelectedAgent()
{
    auto* agent = agentForId(m_selectedAgentId);
    if (agent == nullptr) {
        emit actionMessage(
            trText("Select an available Custom Agent before opening it."),
            true);
        return false;
    }
    if (m_pending) {
        emit actionMessage(
            trText("Wait for the current Project Intelligence request to finish."),
            true);
        return false;
    }
    if (agent->openToken.isEmpty()) {
        return reacquireItemOperation(Pending {
            .operation = Operation::OpenAgent,
            .itemId = agent->id,
        });
    }
    const auto token = std::exchange(agent->openToken, {});
    return beginItemOperation(
        Operation::OpenAgent,
        agent->id,
        token,
        {},
        {});
}

void ProjectIntelligenceModel::installSyntheticFixture()
{
    ++m_demandGeneration;
    m_pending.reset();
    m_mutationDelivery.reset();
    m_replyTimer.stop();
    m_mutationTimer.stop();
    const auto activeId = stableId(QStringLiteral("synthetic|active"));
    const auto archiveId = stableId(QStringLiteral("synthetic|archive"));
    m_sources = {
        {
            .id = activeId,
            .kind = QStringLiteral("active"),
            .label = QStringLiteral("Kodosi"),
            .sessionCount = 2,
            .memoryCount = 2,
            .activeSessionIds = {QStringLiteral("project-intel-synthetic")},
        },
        {
            .id = archiveId,
            .kind = QStringLiteral("claudeArchive"),
            .agent = QStringLiteral("claude"),
            .label = QStringLiteral("Archive · release-work"),
            .sessionCount = 7,
            .memoryCount = 1,
        },
    };
    QVector<PresentationListModel::Row> sourceRows;
    QVector<PresentationListModel::Row> copyDestinationRows;
    for (const auto& source : m_sources) {
        PresentationListModel::Row row {
            .itemId = source.id,
            .title = source.label,
            .subtitle = source.kind == QStringLiteral("active")
                ? trText("Project intelligence")
                : trText("Claude project archive"),
            .kind = source.kind,
            .agent = source.agent,
            .available = true,
            .numberA = static_cast<qint64>(source.sessionCount),
            .numberB = source.memoryCount,
        };
        sourceRows.push_back(row);
        copyDestinationRows.push_back(std::move(row));
    }
    m_sourcesModel.replace(std::move(sourceRows));
    m_copyDestinationsModel.replace(std::move(copyDestinationRows));
    m_selectedSourceId = activeId;
    m_sessions = {
        {
            .id = stableId(QStringLiteral("synthetic|session|1")),
            .title = QStringLiteral("Linux parity checkpoint"),
            .agent = QStringLiteral("Claude"),
            .status = QStringLiteral("working"),
            .mode = QStringLiteral("autopilot"),
            .metadata = QStringLiteral("Updated moments ago"),
            .transcriptAvailable = true,
        },
        {
            .id = stableId(QStringLiteral("synthetic|session|2")),
            .title = QStringLiteral("Protocol security review"),
            .agent = QStringLiteral("Copilot"),
            .status = QStringLiteral("archived"),
            .metadata = QStringLiteral("2026-09-01 · 184 KiB"),
        },
    };
    m_memories = {
        {
            .id = stableId(QStringLiteral("synthetic|memory|main")),
            .filename = QStringLiteral("MEMORY.md"),
            .kind = QStringLiteral("project"),
        },
        {
            .id = stableId(QStringLiteral("synthetic|memory|testing")),
            .filename = QStringLiteral("testing.md"),
            .kind = QStringLiteral("reference"),
        },
    };
    m_agents = {
        {
            .id = stableId(QStringLiteral("synthetic|agent|reviewer")),
            .name = QStringLiteral("security-reviewer"),
            .description = QStringLiteral("Reviews filesystem and protocol boundaries."),
            .model = QStringLiteral("sonnet"),
            .tools = {QStringLiteral("Read"), QStringLiteral("Grep")},
            .errorCount = 1,
        },
    };
    m_settingsNodes = {
        {
            .agent = QStringLiteral("claude"),
            .scope = QStringLiteral("user"),
            .nodeId = 0,
            .depth = 0,
            .key = QStringLiteral("permissions"),
            .kind = QStringLiteral("object"),
            .childCount = 2,
        },
        {
            .agent = QStringLiteral("claude"),
            .scope = QStringLiteral("user"),
            .nodeId = 1,
            .parentId = 0,
            .depth = 1,
            .key = QStringLiteral("defaultMode"),
            .kind = QStringLiteral("string"),
            .value = QStringLiteral("plan"),
        },
        {
            .agent = QStringLiteral("claude"),
            .scope = QStringLiteral("user"),
            .nodeId = 2,
            .parentId = 0,
            .depth = 1,
            .key = QStringLiteral("enableAllProjectMcpServers"),
            .kind = QStringLiteral("boolean"),
            .value = trText("On"),
        },
    };
    QVector<PresentationListModel::Row> sessionRows;
    for (const auto& session : m_sessions) {
        sessionRows.push_back({
            .itemId = session.id,
            .title = session.title,
            .subtitle = session.metadata,
            .kind = session.transcriptAvailable
                ? QStringLiteral("active") : QStringLiteral("archived"),
            .agent = session.agent,
            .status = session.status,
            .mode = session.mode,
            .available = session.transcriptAvailable,
        });
    }
    QVector<PresentationListModel::Row> memoryRows;
    for (const auto& memory : m_memories) {
        memoryRows.push_back({
            .itemId = memory.id,
            .title = memory.filename,
            .subtitle = memory.kind,
            .kind = QStringLiteral("memory"),
            .available = true,
        });
    }
    QVector<PresentationListModel::Row> agentRows;
    for (const auto& agent : m_agents) {
        agentRows.push_back({
            .itemId = agent.id,
            .title = agent.name,
            .subtitle = agent.description,
            .kind = QStringLiteral("agent"),
            .agent = agent.model,
            .metadata = agent.tools.join(QStringLiteral(", ")),
            .available = true,
            .numberA = agent.errorCount,
        });
    }
    m_sessionsModel.replace(std::move(sessionRows));
    m_memoriesModel.replace(std::move(memoryRows));
    m_agentsModel.replace(std::move(agentRows));
    m_customizationsModel.replace({
        {
            .itemId = stableId(QStringLiteral("synthetic|mcp|github")),
            .title = QStringLiteral("github"),
            .subtitle = QStringLiteral("Repository tools and pull request context"),
            .kind = QStringLiteral("mcpserver"),
            .status = QStringLiteral("loaded"),
            .metadata = QStringLiteral("project"),
            .available = true,
        },
    });
    m_selectedMemoryId = m_memories.constFirst().id;
    m_memoryContent = QStringLiteral(
        "# Project memory\n\nRust owns filesystem identity, mutation, and security authority.");
    m_selectedAgentId = m_agents.constFirst().id;
    m_agentDescription = m_agents.constFirst().description;
    m_agentModel = m_agents.constFirst().model;
    m_agentTools = m_agents.constFirst().tools;
    m_agentParseErrors = {
        QStringLiteral("Line 3: tools must be a list."),
    };
    m_agentFrontmatter = QStringLiteral(
        "name: security-reviewer\nmodel: sonnet\ntools: Read, Grep");
    m_agentPrompt = QStringLiteral(
        "Review the selected project for filesystem and protocol boundary violations.");
    rebuildSettingsTree();
    emit settingsFilterChanged();
    m_hasSourceSnapshot = true;
    setState(State::Ready);
    emit sourceChanged();
    emit memoryChanged();
    emit agentChanged();
}

void ProjectIntelligenceModel::installSyntheticEmptyFixture(
    const bool archive)
{
    ++m_demandGeneration;
    m_pending.reset();
    m_replyTimer.stop();
    const auto sourceId = stableId(
        archive
            ? QStringLiteral("synthetic|empty|archive")
            : QStringLiteral("synthetic|empty|active"));
    m_sources = {
        {
            .id = sourceId,
            .kind = archive
                ? QStringLiteral("claudeArchive")
                : QStringLiteral("active"),
            .agent = archive
                ? QStringLiteral("claude")
                : QString {},
            .label = archive
                ? QStringLiteral("Archive · empty-project")
                : QStringLiteral("Empty project"),
        },
    };
    const PresentationListModel::Row sourceRow {
        .itemId = sourceId,
        .title = m_sources.constFirst().label,
        .subtitle = archive
            ? trText("Claude project archive")
            : trText("Project intelligence"),
        .kind = m_sources.constFirst().kind,
        .agent = m_sources.constFirst().agent,
        .available = true,
    };
    m_sourcesModel.replace({sourceRow});
    m_copyDestinationsModel.replace({sourceRow});
    m_selectedSourceId = sourceId;
    clearSourceDetails();
    m_hasSourceSnapshot = true;
    m_hasMoreSources = false;
    m_nextCursor.clear();
    setState(State::Ready);
    emit sourceChanged();
}

void ProjectIntelligenceModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("auth.ready")
        && type != QStringLiteral("auth.required")) {
        return;
    }
    const auto epoch = exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    QString userId;
    if (!epoch || (type == QStringLiteral("auth.ready")
            && !nullableAccount(object, QStringLiteral("userId"), userId))) {
        emit decodeError(trText("Authentication context is malformed."));
        return;
    }
    activateAccount(std::move(userId), *epoch);
}

void ProjectIntelligenceModel::ingestAgentIntelEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        return;
    }
    const auto epoch = exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    QString userId;
    if (!epoch
        || !nullableAccount(object, QStringLiteral("accountUserId"), userId)) {
        emit decodeError(trText("Agent-intelligence account authority is malformed."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {.userId = std::move(userId), .epoch = *epoch},
        json);
    if (admission == AccountEventAdmission::Current) {
        applyAgentIntelEvent(json);
    } else if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(trText("A future Project Intelligence reply exceeded its bound."));
    }
}

void ProjectIntelligenceModel::resetRuntimeAuthority()
{
    releaseOwnedHandoffsBestEffort();
    ++m_runtimeGeneration;
    ++m_demandGeneration;
    m_accountFence.reset();
    m_hasAccountContext = false;
    m_mutationDelivery.reset();
    m_mutationTimer.stop();
    m_sourcesDirtyAfterMutation = false;
    clearAll();
    m_releaseRequests.clear();
    m_ownedHandoffs.clear();
    m_cancelledOpenRequests.clear();
}

bool ProjectIntelligenceModel::beginListSources(const bool append)
{
    if (m_pending || (append && m_nextCursor.isEmpty())) {
        return false;
    }
    if (!append) {
        if (!m_invalidateDetailsAfterRefresh
            && !m_resumeAfterSnapshot && m_failedPending
            && (m_failedPending->operation == Operation::ReadMemory
                || m_failedPending->operation == Operation::ReadAgent)) {
            m_resumeAfterSnapshot = m_failedPending;
        }
        if (!m_reacquireAfterList && !m_selectedSourceId.isEmpty()) {
            Pending recovery {
                .operation = Operation::InspectSource,
                .itemId = m_selectedSourceId,
            };
            populatePendingIdentity(recovery);
            if (!recovery.sourceKind.isEmpty()) {
                m_reacquireAfterList = std::move(recovery);
            }
        }
        m_rebindSources = m_sources;
        m_rebindUsedSourceIds.clear();
        m_sourceSelectionTokens.clear();
        m_seenSourceCursors.clear();
    }
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("agent.intel.listProjectSourcesBound")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("limit"), 100},
        {QStringLiteral("maxBytes"), 262'144},
    };
    if (append) {
        command.insert(QStringLiteral("cursor"), m_nextCursor);
    }
    setState(State::LoadingSources);
    return send(
        std::move(command),
        Pending {
            .requestId = requestId,
            .operation = Operation::ListSources,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .append = append,
        });
}

bool ProjectIntelligenceModel::beginInspect(
    const QString& sourceId,
    const bool preserveDetails)
{
    auto* source = sourceForId(sourceId);
    if (source == nullptr || source->selectionToken.isEmpty() || m_pending) {
        return false;
    }
    m_selectedSourceId = sourceId;
    if (!preserveDetails) {
        clearSourceDetails();
    }
    emit sourceChanged();
    setState(State::LoadingSource);
    const auto token = std::exchange(source->selectionToken, {});
    return beginItemOperation(
        Operation::InspectSource,
        sourceId,
        token,
        {},
        Pending {
            .operation = Operation::InspectSource,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .itemId = sourceId,
            .sourceId = sourceId,
            .consumedToken = token,
            .sourceKind = source->kind,
            .sourceAgent = source->agent,
            .sourceLabel = source->label,
            .sourceActiveSessionIds = source->activeSessionIds,
            .sourceOccurrence = source->occurrence,
            .sourceIdentityAmbiguous =
                source->ambiguousAuthorityIdentity,
        });
}

bool ProjectIntelligenceModel::beginItemOperation(
    const Operation operation,
    const QString& itemId,
    const QString& token,
    QJsonObject extra,
    Pending pending)
{
    if (!canonicalUuidV7(token) || m_pending) {
        return false;
    }
    QString type;
    switch (operation) {
    case Operation::InspectSource:
        type = QStringLiteral("agent.intel.inspectProjectSourceBound");
        break;
    case Operation::ReadMemory:
        type = QStringLiteral("agent.intel.readClaudeMemoryBound");
        break;
    case Operation::ReadAgent:
        type = QStringLiteral("agent.intel.readCustomAgentBound");
        break;
    case Operation::OpenMemory:
        type = QStringLiteral("agent.intel.openProjectMemoryBound");
        break;
    case Operation::OpenAgent:
        type = QStringLiteral("agent.intel.openCustomAgentBound");
        break;
    default:
        return false;
    }
    extra.insert(QStringLiteral("type"), type);
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    extra.insert(
        QStringLiteral("requestId"),
        requestId);
    extra.insert(QStringLiteral("selectionToken"), token);
    pending.requestId = requestId;
    pending.operation = operation;
    pending.runtimeGeneration = m_runtimeGeneration;
    pending.demandGeneration = m_demandGeneration;
    pending.itemId = itemId;
    pending.consumedToken = token;
    populatePendingIdentity(pending);
    return send(
        std::move(extra),
        std::move(pending));
}

void ProjectIntelligenceModel::populatePendingIdentity(Pending& pending)
{
    if (const auto* source = sourceForId(m_selectedSourceId)) {
        pending.sourceId = source->id;
        pending.sourceKind = source->kind;
        pending.sourceAgent = source->agent;
        pending.sourceLabel = source->label;
        pending.sourceActiveSessionIds = source->activeSessionIds;
        pending.sourceOccurrence = source->occurrence;
        pending.sourceIdentityAmbiguous =
            source->ambiguousAuthorityIdentity;
    }
    if (pending.operation == Operation::ReadMemory
        || pending.operation == Operation::OpenMemory) {
        if (const auto* memory = memoryForId(pending.itemId)) {
            pending.expectedCanonicalCwd = memory->canonicalCwd;
            pending.expectedProjectSlug = memory->projectSlug;
            pending.expectedCanonicalIdentityKnown =
                memory->canonicalIdentityKnown;
            pending.expectedFilename = memory->filename;
            pending.expectedMemoryType = memory->kind;
            pending.expectedMemoryTypeIsNull =
                memory->memoryTypeIsNull;
        }
    } else if (pending.operation == Operation::ReadAgent
        || pending.operation == Operation::OpenAgent) {
        if (const auto* agent = agentForId(pending.itemId)) {
            pending.expectedTarget = agent->target;
            pending.expectedName = agent->name;
            pending.expectedDescription = agent->description;
            pending.expectedModel = agent->model;
            pending.expectedModelIsNull = agent->modelIsNull;
            pending.expectedTools = agent->tools;
            pending.expectedDisallowedTools = agent->disallowedTools;
            pending.expectedDisallowedToolsKnown =
                agent->disallowedToolsKnown;
        }
    }
}

bool ProjectIntelligenceModel::reacquireItemOperation(Pending pending)
{
    if (m_pending) {
        emit actionMessage(
            trText("Wait for the current Project Intelligence request to finish."),
            true);
        return false;
    }
    if (pending.operation != Operation::ReadMemory
        && pending.operation != Operation::OpenMemory
        && pending.operation != Operation::ReadAgent
        && pending.operation != Operation::OpenAgent) {
        emit actionMessage(
            trText("The requested project action cannot be refreshed."),
            true);
        return false;
    }
    if (sourceForId(m_selectedSourceId) == nullptr) {
        emit actionMessage(
            trText("The selected project source is no longer available."),
            true);
        return false;
    }
    if (pending.operation == Operation::ReadMemory) {
        m_selectedMemoryId = pending.itemId;
        m_memoryContent.clear();
        m_memoryError.clear();
        m_memoryLoading = true;
        emit memoryChanged();
    } else if (pending.operation == Operation::ReadAgent) {
        m_selectedAgentId = pending.itemId;
        m_agentParseErrors.clear();
        m_agentFrontmatter.clear();
        m_agentPrompt.clear();
        m_agentDetailError.clear();
        m_agentDetailLoading = true;
        emit agentChanged();
    }
    pending.runtimeGeneration = m_runtimeGeneration;
    pending.demandGeneration = m_demandGeneration;
    populatePendingIdentity(pending);
    m_failedPending.reset();
    m_resumeAfterSnapshot.reset();
    m_reacquireAfterList = std::move(pending);
    m_nextCursor.clear();
    m_hasMoreSources = false;
    emit actionMessage(
        trText("Refreshing project capabilities…"),
        false);
    if (beginListSources(false)) {
        return true;
    }
    const auto operation = m_reacquireAfterList
        ? m_reacquireAfterList->operation
        : Operation::InspectSource;
    if (operation == Operation::ReadMemory) {
        m_memoryLoading = false;
        m_memoryError = trText("Project capabilities could not be refreshed.");
        emit memoryChanged();
    } else if (operation == Operation::ReadAgent) {
        m_agentDetailLoading = false;
        m_agentDetailError =
            trText("Project capabilities could not be refreshed.");
        emit agentChanged();
    }
    emit actionMessage(
        trText("Project capabilities could not be refreshed."),
        true);
    return false;
}

bool ProjectIntelligenceModel::send(QJsonObject command, Pending pending)
{
    if (m_pending) {
        return false;
    }
    const auto bytes = QJsonDocument(command).toJson(QJsonDocument::Compact);
    m_pending = pending;
    const auto result = m_dispatcher.send(CommandLane::AgentIntel, bytes);
    if (!result) {
        m_pending.reset();
        rememberFailure(pending);
        if (pending.operation == Operation::ReadMemory) {
            m_memoryLoading = false;
            m_memoryError = result.error().message;
            emit memoryChanged();
        } else if (pending.operation == Operation::ReadAgent) {
            m_agentDetailLoading = false;
            m_agentDetailError = result.error().message;
            emit agentChanged();
        } else if (pending.operation == Operation::OpenMemory
            || pending.operation == Operation::OpenAgent
            || pending.operation == Operation::CopyMemory
            || pending.operation == Operation::ReconcileCopy) {
            emit actionMessage(result.error().message, true);
            recoverConsumedOperation(pending);
        } else {
            setState(State::Failed, result.error().message);
        }
        return false;
    }
    m_replyTimer.start(static_cast<int>(m_replyTimeoutMs));
    return true;
}

bool ProjectIntelligenceModel::sendMutation(
    QJsonObject command,
    Pending pending)
{
    if (m_mutationDelivery) {
        return false;
    }
    const auto result = m_dispatcher.send(
        CommandLane::AgentIntel,
        QJsonDocument(command).toJson(QJsonDocument::Compact));
    if (!result) {
        emit actionMessage(result.error().message, true);
        recoverConsumedOperation(pending);
        return false;
    }
    m_mutationDelivery = std::move(pending);
    m_mutationTimer.start(static_cast<int>(m_replyTimeoutMs));
    return true;
}

bool ProjectIntelligenceModel::dispatchCopyReconcile(
    Pending mutation,
    QString statusMessage)
{
    if (mutation.reconcileAttempts >= maximumCopyReconcileAttempts) {
        settleCopyDelivery(
            trText("The copy result could not be reconciled."));
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.reconcileProjectMemoryCopy")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("mutationId"), mutation.mutationId},
    };
    mutation.requestId = requestId;
    mutation.operation = Operation::ReconcileCopy;
    mutation.runtimeGeneration = m_runtimeGeneration;
    ++mutation.reconcileAttempts;
    if (!sendMutation(std::move(command), std::move(mutation))) {
        return false;
    }
    emit actionMessage(std::move(statusMessage), false);
    return true;
}

void ProjectIntelligenceModel::settleCopyDelivery(
    QString message,
    const bool malformed)
{
    m_mutationDelivery.reset();
    m_mutationTimer.stop();
    emit actionMessage(message, true);
    if (malformed) {
        emit decodeError(message);
    }
    m_sourcesDirtyAfterMutation = true;
}

void ProjectIntelligenceModel::refreshDirtySourcesIfIdle()
{
    if (!m_sourcesDirtyAfterMutation || !m_demanded || m_pending
        || !m_hasAccountContext) {
        return;
    }
    m_nextCursor.clear();
    m_hasMoreSources = false;
    if (beginListSources(false)) {
        m_sourcesDirtyAfterMutation = false;
    }
}

void ProjectIntelligenceModel::cancelPending()
{
    if (m_pending
        && (m_pending->operation == Operation::OpenMemory
            || m_pending->operation == Operation::OpenAgent)) {
        if (m_cancelledOpenRequests.size() >= maximumReleaseRequests) {
            m_cancelledOpenRequests.erase(m_cancelledOpenRequests.begin());
        }
        m_cancelledOpenRequests.insert(
            m_pending->requestId,
            m_pending->operation);
    }
    m_pending.reset();
    m_replyTimer.stop();
}

void ProjectIntelligenceModel::rememberFailure(const Pending& pending)
{
    if (pending.operation == Operation::ListSources) {
        return;
    }
    m_failedPending = m_resumeAfterSnapshot
        ? m_resumeAfterSnapshot
        : std::optional<Pending>(pending);
}

void ProjectIntelligenceModel::recoverConsumedOperation(
    const Pending& pending)
{
    if (pending.operation != Operation::CopyMemory
        && pending.operation != Operation::ReconcileCopy) {
        if (!m_selectedSourceId.isEmpty()) {
            setState(State::Ready);
        }
        return;
    }
    m_sourcesDirtyAfterMutation = true;
    refreshDirtySourcesIfIdle();
}

std::optional<qsizetype>
ProjectIntelligenceModel::uniqueReacquiredSource(
    const Pending& pending,
    bool& ambiguous) const
{
    if (pending.sourceIdentityAmbiguous) {
        ambiguous = true;
        return std::nullopt;
    }
    QVector<qsizetype> matches;
    for (qsizetype index = 0; index < m_sources.size(); ++index) {
        const auto& source = m_sources.at(index);
        if (source.kind != pending.sourceKind
            || source.agent != pending.sourceAgent) {
            continue;
        }
        const auto matchesIdentity =
            source.kind == QStringLiteral("active")
            ? (!pending.sourceActiveSessionIds.isEmpty()
                && overlaps(
                    source.activeSessionIds,
                    pending.sourceActiveSessionIds))
            : source.label == pending.sourceLabel;
        if (matchesIdentity) {
            matches.push_back(index);
        }
    }
    ambiguous = matches.size() > 1;
    return matches.size() == 1
        ? std::optional<qsizetype>(matches.constFirst())
        : std::nullopt;
}

bool ProjectIntelligenceModel::resumeAfterSnapshot()
{
    if (!m_resumeAfterSnapshot || m_pending) {
        return false;
    }
    auto pending = std::exchange(m_resumeAfterSnapshot, std::nullopt);
    if (pending->operation == Operation::ReadMemory
        || pending->operation == Operation::OpenMemory) {
        auto* memory = memoryForId(pending->itemId);
        const auto summaryMatches = memory != nullptr
            && memory->filename == pending->expectedFilename
            && memory->kind == pending->expectedMemoryType
            && memory->memoryTypeIsNull
                == pending->expectedMemoryTypeIsNull;
        const auto learnedIdentityMatches =
            !pending->expectedCanonicalIdentityKnown
            || (memory != nullptr
                && (!memory->canonicalIdentityKnown
                    || (memory->canonicalCwd
                            == pending->expectedCanonicalCwd
                        && memory->projectSlug
                            == pending->expectedProjectSlug)));
        if (!summaryMatches || !learnedIdentityMatches) {
            m_failedPending = pending;
            m_selectedMemoryId.clear();
            m_memoryContent.clear();
            m_memoryLoading = false;
            m_memoryError =
                trText("The selected Project Memory disappeared or changed.");
            emit memoryChanged();
            emit actionMessage(m_memoryError, true);
            return false;
        }
        if (pending->expectedCanonicalIdentityKnown
            && !memory->canonicalIdentityKnown) {
            memory->canonicalCwd = pending->expectedCanonicalCwd;
            memory->projectSlug = pending->expectedProjectSlug;
            memory->canonicalIdentityKnown = true;
        }
        m_selectedMemoryId = memory->id;
        m_memoryError.clear();
        m_memoryLoading = pending->operation == Operation::ReadMemory;
        if (pending->operation == Operation::OpenMemory) {
            if (const auto cached = cachedMemoryDetail(*memory)) {
                m_memoryContent = cached->content;
            }
        } else {
            m_memoryContent.clear();
        }
        emit memoryChanged();
        auto& token = pending->operation == Operation::ReadMemory
            ? memory->readToken : memory->openToken;
        pending->reacquired = true;
        return beginItemOperation(
            pending->operation,
            memory->id,
            std::exchange(token, {}),
            {},
            *pending);
    }
    if (pending->operation == Operation::ReadAgent
        || pending->operation == Operation::OpenAgent) {
        auto* agent = agentForId(pending->itemId);
        const auto summaryMatches = agent != nullptr
            && agent->target == pending->expectedTarget
            && agent->name == pending->expectedName
            && agent->description == pending->expectedDescription
            && agent->model == pending->expectedModel
            && agent->modelIsNull == pending->expectedModelIsNull
            && agent->tools == pending->expectedTools;
        const auto learnedDisallowedToolsMatch =
            !pending->expectedDisallowedToolsKnown
            || (agent != nullptr
                && (!agent->disallowedToolsKnown
                    || agent->disallowedTools
                        == pending->expectedDisallowedTools));
        if (!summaryMatches || !learnedDisallowedToolsMatch) {
            m_failedPending = pending;
            m_selectedAgentId.clear();
            m_agentDetailLoading = false;
            m_agentDetailError =
                trText("The selected Custom Agent disappeared or changed.");
            emit agentChanged();
            emit actionMessage(m_agentDetailError, true);
            return false;
        }
        if (pending->expectedDisallowedToolsKnown
            && !agent->disallowedToolsKnown) {
            agent->disallowedTools =
                pending->expectedDisallowedTools;
            agent->disallowedToolsKnown = true;
        }
        m_selectedAgentId = agent->id;
        m_agentDescription = agent->description;
        m_agentModel = agent->model;
        m_agentTools = agent->tools;
        m_agentDetailError.clear();
        m_agentDetailLoading = pending->operation == Operation::ReadAgent;
        if (pending->operation == Operation::OpenAgent) {
            if (const auto cached = cachedAgentDetail(*agent)) {
                m_agentDescription = cached->description;
                m_agentModel = cached->model;
                m_agentTools = cached->tools;
                m_agentParseErrors = cached->parseErrors;
                m_agentFrontmatter = cached->frontmatter;
                m_agentPrompt = cached->prompt;
            }
        } else {
            m_agentParseErrors.clear();
            m_agentFrontmatter.clear();
            m_agentPrompt.clear();
        }
        emit agentChanged();
        auto& token = pending->operation == Operation::ReadAgent
            ? agent->detailToken : agent->openToken;
        pending->reacquired = true;
        return beginItemOperation(
            pending->operation,
            agent->id,
            std::exchange(token, {}),
            {},
            *pending);
    }
    return false;
}

void ProjectIntelligenceModel::activateAccount(QString userId, const quint64 epoch)
{
    const auto hadContext = m_hasAccountContext;
    auto activation = m_accountFence.activate({
        .userId = std::move(userId),
        .epoch = epoch,
    });
    if (!activation.accepted) {
        return;
    }
    m_hasAccountContext = true;
    if (activation.changed && hadContext) {
        releaseOwnedHandoffsBestEffort();
        ++m_runtimeGeneration;
        ++m_demandGeneration;
        m_mutationDelivery.reset();
        m_mutationTimer.stop();
        m_sourcesDirtyAfterMutation = false;
        clearAll();
        m_releaseRequests.clear();
        m_ownedHandoffs.clear();
        m_cancelledOpenRequests.clear();
    }
    for (auto& event : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(event));
    }
    if (m_demanded && !m_pending
        && m_state != State::SourcesReady && m_state != State::Ready) {
        (void)beginListSources(false);
    }
}

void ProjectIntelligenceModel::applyAgentIntelEvent(const QByteArray& json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto requestId = stringField(object, QStringLiteral("requestId"));
    if (!requestId || !canonicalUuidV7(*requestId)) {
        return;
    }
    if (handleReleaseEvent(object) || handleCancelledOpenEvent(object)) {
        return;
    }
    Pending pending;
    const auto mutationReply =
        m_mutationDelivery
        && *requestId == m_mutationDelivery->requestId
        && mutationCurrent(*m_mutationDelivery);
    if (mutationReply) {
        pending = *m_mutationDelivery;
        m_mutationDelivery.reset();
        m_mutationTimer.stop();
    } else if (m_pending && *requestId == m_pending->requestId
        && pendingCurrent(*m_pending)) {
        pending = *m_pending;
        m_pending.reset();
        m_replyTimer.stop();
    } else {
        return;
    }
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("agent.intel.error")) {
        applyError(object, pending);
        refreshDirtySourcesIfIdle();
        return;
    }
    if (type != QStringLiteral("agent.intel.reply")
        || !exactAccountEnvelope(object, {QStringLiteral("payload")})
        || !object.value(QStringLiteral("payload")).isObject()) {
        rememberFailure(pending);
        const auto message = trText("Project Intelligence reply was malformed.");
        emit decodeError(message);
        if (pending.operation == Operation::ReadMemory) {
            m_memoryLoading = false;
            m_memoryContent.clear();
            m_memoryError = message;
            emit memoryChanged();
        } else if (pending.operation == Operation::ReadAgent) {
            m_agentDetailLoading = false;
            m_agentFrontmatter.clear();
            m_agentPrompt.clear();
            m_agentDetailError = message;
            emit agentChanged();
        } else if (pending.operation == Operation::CopyMemory
            || pending.operation == Operation::ReconcileCopy) {
            settleCopyDelivery(message, true);
            refreshDirtySourcesIfIdle();
        } else if (pending.operation == Operation::OpenMemory
            || pending.operation == Operation::OpenAgent) {
            emit actionMessage(message, true);
            setState(State::Ready);
        } else {
            setState(State::Failed, message);
        }
        return;
    }
    applyReply(object.value(QStringLiteral("payload")).toObject(), pending);
    refreshDirtySourcesIfIdle();
}

void ProjectIntelligenceModel::applyReply(
    const QJsonObject& payload,
    const Pending& pending)
{
    switch (pending.operation) {
    case Operation::ListSources:
        applySourcePage(payload, pending.append);
        break;
    case Operation::InspectSource: applySnapshot(payload, pending); break;
    case Operation::ReadMemory: applyMemoryDetail(payload, pending); break;
    case Operation::ReadAgent: applyAgentDetail(payload, pending); break;
    case Operation::OpenMemory:
    case Operation::OpenAgent: applyOpen(payload, pending); break;
    case Operation::CopyMemory:
    case Operation::ReconcileCopy: applyCopy(payload, pending); break;
    }
}

void ProjectIntelligenceModel::applySourcePage(
    const QJsonObject& payload,
    const bool append)
{
    if (!exactFields(
            payload,
            {QStringLiteral("items"),
             QStringLiteral("nextCursor"),
             QStringLiteral("hasMore"),
             QStringLiteral("responseBytes")})) {
        setState(State::Failed, trText("Project source catalog exceeded its contract."));
        emit decodeError(m_error);
        return;
    }
    const auto itemsValue = payload.value(QStringLiteral("items"));
    const auto hasMoreValue = payload.value(QStringLiteral("hasMore"));
    const auto nextCursorValue = payload.value(QStringLiteral("nextCursor"));
    const auto responseBytes =
        unsignedValue(payload.value(QStringLiteral("responseBytes")));
    if (!itemsValue.isArray() || !hasMoreValue.isBool()
        || (!nextCursorValue.isNull() && !nextCursorValue.isString())
        || itemsValue.toArray().size() > 256 || !responseBytes
        || *responseBytes > 262'144
        || hasMoreValue.toBool() != nextCursorValue.isString()
        || (nextCursorValue.isString()
            && (!bounded(nextCursorValue.toString(), 256, false)
                || m_seenSourceCursors.contains(nextCursorValue.toString())
                || (append && nextCursorValue.toString() == m_nextCursor)))) {
        setState(State::Failed, trText("Project source catalog exceeded its contract."));
        emit decodeError(m_error);
        return;
    }

    QVector<Source> page;
    QSet<QString> pageTokens;
    QHash<QString, quint32> occurrences;
    if (append) {
        for (const auto& source : m_sources) {
            const auto signature = source.kind + QLatin1Char('|')
                + source.agent + QLatin1Char('|') + source.label;
            occurrences[signature] = std::max(
                occurrences.value(signature),
                source.occurrence + 1);
        }
    }
    for (const auto& value : itemsValue.toArray()) {
        if (!value.isObject()) {
            setState(State::Failed, trText("Project source catalog was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("selectionToken"),
                 QStringLiteral("sourceKind"),
                 QStringLiteral("agent"),
                 QStringLiteral("label"),
                 QStringLiteral("sessionCount"),
                 QStringLiteral("memoryCount"),
                 QStringLiteral("activeSessionIds")})) {
            setState(State::Failed, trText("Project source catalog was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto token = stringField(object, QStringLiteral("selectionToken"));
        const auto kind = stringField(object, QStringLiteral("sourceKind"), 64);
        const auto label = stringField(object, QStringLiteral("label"), 512);
        const auto agentValue = object.value(QStringLiteral("agent"));
        QString agent;
        const auto sessions = unsignedValue(object.value(QStringLiteral("sessionCount")));
        const auto memories = unsignedValue(object.value(QStringLiteral("memoryCount")));
        const auto activeSessionValues =
            object.value(QStringLiteral("activeSessionIds"));
        if (!token || !canonicalUuidV7(*token) || !kind || !label
            || !nullableString(object, QStringLiteral("agent"), agent, 64)
            || !sessions || !memories || !activeSessionValues.isArray()
            || *sessions > static_cast<quint64>(std::numeric_limits<qint64>::max())
            || *memories > std::numeric_limits<quint32>::max()
            || (*kind != QStringLiteral("active")
                && *kind != QStringLiteral("claudeArchive")
                && *kind != QStringLiteral("copilotArchive"))
            || (*kind == QStringLiteral("active")
                ? !agentValue.isNull()
                : (!agentValue.isString()
                    || (*kind == QStringLiteral("claudeArchive")
                            ? agent != QStringLiteral("claude")
                            : agent != QStringLiteral("copilot"))))
            || pageTokens.contains(*token)
            || m_sourceSelectionTokens.contains(*token)) {
            setState(State::Failed, trText("Project source catalog was malformed."));
            emit decodeError(m_error);
            return;
        }
        QStringList activeSessionIds;
        QSet<QString> uniqueSessionIds;
        for (const auto& sessionId : activeSessionValues.toArray()) {
            if (!sessionId.isString()
                || !bounded(sessionId.toString(), identifierLimit, false)
                || activeSessionIds.size() >= 512
                || uniqueSessionIds.contains(sessionId.toString())) {
                setState(State::Failed, trText("Project source catalog was malformed."));
                emit decodeError(m_error);
                return;
            }
            activeSessionIds.push_back(sessionId.toString());
            uniqueSessionIds.insert(sessionId.toString());
        }
        if (*kind != QStringLiteral("active") && !activeSessionIds.isEmpty()) {
            setState(State::Failed, trText("Project source catalog was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto signature =
            *kind + QLatin1Char('|') + agent + QLatin1Char('|') + *label;
        const auto occurrence = occurrences[signature]++;
        page.push_back({
            .selectionToken = *token,
            .kind = *kind,
            .agent = agent,
            .label = *label,
            .sessionCount = *sessions,
            .memoryCount = static_cast<quint32>(
                *memories),
            .activeSessionIds = std::move(activeSessionIds),
            .occurrence = occurrence,
        });
        pageTokens.insert(*token);
    }

    for (auto& source : page) {
        QVector<const Source*> candidates;
        for (const auto& previous : m_rebindSources) {
            if (m_rebindUsedSourceIds.contains(previous.id)
                || previous.kind != source.kind
                || previous.agent != source.agent) {
                continue;
            }
            if (source.kind == QStringLiteral("active")) {
                if (overlaps(
                        previous.activeSessionIds,
                        source.activeSessionIds)) {
                    candidates.push_back(&previous);
                }
            } else if (previous.label == source.label
                && previous.occurrence == source.occurrence) {
                candidates.push_back(&previous);
            }
        }
        if (candidates.size() == 1) {
            auto candidateUseCount = 0;
            if (source.kind == QStringLiteral("active")) {
                for (const auto& other : page) {
                    if (other.kind == QStringLiteral("active")
                        && overlaps(
                            candidates.constFirst()->activeSessionIds,
                            other.activeSessionIds)) {
                        ++candidateUseCount;
                    }
                }
            } else {
                candidateUseCount = 1;
            }
            if (candidateUseCount == 1) {
                source.id = candidates.constFirst()->id;
                m_rebindUsedSourceIds.insert(source.id);
            }
        }
        if (source.id.isEmpty()) {
            source.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
    }

    QVector<Source> decoded = append ? m_sources : QVector<Source> {};
    decoded += page;
    if (decoded.size() > maximumSources) {
        setState(State::Failed, trText("Project source catalog is too large."));
        emit decodeError(m_error);
        return;
    }
    QHash<QString, qsizetype> archiveIdentityCounts;
    for (const auto& source : decoded) {
        if (source.kind != QStringLiteral("active")) {
            ++archiveIdentityCounts[
                source.kind + QLatin1Char('|') + source.agent
                + QLatin1Char('|') + source.label];
        }
    }
    for (auto& source : decoded) {
        if (source.kind != QStringLiteral("active")) {
            source.ambiguousAuthorityIdentity =
                hasMoreValue.toBool()
                || archiveIdentityCounts.value(
                    source.kind + QLatin1Char('|') + source.agent
                    + QLatin1Char('|') + source.label)
                    > 1;
            continue;
        }
        source.ambiguousAuthorityIdentity =
            hasMoreValue.toBool() || source.activeSessionIds.isEmpty()
            || std::ranges::any_of(decoded, [&source](const Source& other) {
                   return &other != &source
                       && other.kind == QStringLiteral("active")
                       && overlaps(
                           source.activeSessionIds,
                           other.activeSessionIds);
               });
    }
    m_sources = std::move(decoded);
    for (const auto& token : pageTokens) {
        m_sourceSelectionTokens.insert(token);
    }
    rebuildSourceRows();
    m_hasMoreSources = hasMoreValue.toBool();
    m_nextCursor = nextCursorValue.toString();
    if (!m_nextCursor.isEmpty()) {
        m_seenSourceCursors.insert(m_nextCursor);
    }
    setState(State::SourcesReady);
    if (!m_hasMoreSources) {
        m_rebindSources.clear();
        m_rebindUsedSourceIds.clear();
    }

    if (m_hasMoreSources && m_reacquireAfterList) {
        (void)beginListSources(true);
        return;
    }
    if (m_hasMoreSources || !m_reacquireAfterList) {
        if (!m_hasMoreSources && !m_selectedSourceId.isEmpty()
            && sourceForId(m_selectedSourceId) == nullptr) {
            m_selectedSourceId.clear();
            clearSourceDetails();
            emit sourceChanged();
        } else if (append && !m_selectedSourceId.isEmpty()) {
            setState(State::Ready);
        }
        return;
    }

    auto recovery = std::exchange(m_reacquireAfterList, std::nullopt);
    bool ambiguous = false;
    const auto index = uniqueReacquiredSource(*recovery, ambiguous);
    if (!index) {
        m_failedPending = recovery;
        m_selectedSourceId.clear();
        clearSourceDetails();
        setState(
            State::Failed,
            ambiguous
                ? trText("More than one project source matches the previous selection.")
                : trText("The previously selected project source disappeared."));
        emit sourceChanged();
        return;
    }
    auto& source = m_sources[*index];
    if (!recovery->sourceId.isEmpty()
        && source.id != recovery->sourceId) {
        const auto duplicate = std::ranges::find(
            m_sources,
            recovery->sourceId,
            &Source::id);
        if (duplicate != m_sources.end()) {
            duplicate->id =
                QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        source.id = recovery->sourceId;
        rebuildSourceRows();
    }
    m_selectedSourceId = source.id;
    if (recovery->operation != Operation::InspectSource) {
        m_resumeAfterSnapshot = recovery;
    }
    if (!beginInspect(source.id, true)) {
        m_failedPending = recovery;
        setState(
            State::Failed,
            trText("The refreshed project source could not be selected."));
    }
}

void ProjectIntelligenceModel::applySnapshot(
    const QJsonObject& payload,
    const Pending& pending)
{
    rememberFailure(pending);
    auto* source = sourceForId(pending.itemId);
    if (!exactFields(
            payload,
            {QStringLiteral("sourceSelectionToken"),
             QStringLiteral("sourceKind"),
             QStringLiteral("agent"),
             QStringLiteral("label"),
             QStringLiteral("sessions"),
             QStringLiteral("memories"),
             QStringLiteral("customAgents"),
             QStringLiteral("customizations"),
             QStringLiteral("settings")})
        || !payload.value(QStringLiteral("sessions")).isArray()
        || !payload.value(QStringLiteral("memories")).isArray()
        || !payload.value(QStringLiteral("customAgents")).isArray()
        || !payload.value(QStringLiteral("customizations")).isArray()
        || !payload.value(QStringLiteral("settings")).isArray()) {
        rememberFailure(pending);
        setState(State::Failed, trText("The project source snapshot was malformed."));
        emit decodeError(m_error);
        return;
    }
    const auto sourceToken =
        stringField(payload, QStringLiteral("sourceSelectionToken"));
    const auto sourceKind = stringField(payload, QStringLiteral("sourceKind"), 64);
    const auto label = stringField(payload, QStringLiteral("label"), labelLimit);
    const auto sourceAgentValue = payload.value(QStringLiteral("agent"));
    QString sourceAgent;
    if (source == nullptr || !sourceToken || !canonicalUuidV7(*sourceToken)
        || *sourceToken == pending.consumedToken
        || !sourceKind || !label || *sourceKind != source->kind
        || *sourceKind != pending.sourceKind
        || *label != source->label || *label != pending.sourceLabel
        || !nullableString(
            payload,
            QStringLiteral("agent"),
            sourceAgent,
            64)
        || sourceAgent != source->agent
        || sourceAgent != pending.sourceAgent
        || (source->kind == QStringLiteral("active")
                ? !sourceAgentValue.isNull()
                : !sourceAgentValue.isString())) {
        rememberFailure(pending);
        setState(State::Failed, trText("The selected project source is no longer current."));
        emit decodeError(m_error);
        return;
    }

    QVector<Session> sessions;
    const auto sessionValues = payload.value(QStringLiteral("sessions")).toArray();
    if (sessionValues.size() > maximumSessions) {
        setState(State::Failed, trText("Project session list exceeded its bound."));
        return;
    }
    QSet<QString> sessionIdentities;
    for (const auto& value : sessionValues) {
        if (!value.isObject()) {
            setState(State::Failed, trText("Project session list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("sessionId"),
                 QStringLiteral("runtimeIncarnationId"),
                 QStringLiteral("title"),
                 QStringLiteral("agent"),
                 QStringLiteral("status"),
                 QStringLiteral("mode"),
                 QStringLiteral("startedAt"),
                 QStringLiteral("updatedAt"),
                 QStringLiteral("sizeBytes"),
                 QStringLiteral("hostType"),
                 QStringLiteral("transcriptAvailable")})) {
            setState(State::Failed, trText("Project session list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto sessionId = stringField(object, QStringLiteral("sessionId"));
        const auto title = stringField(object, QStringLiteral("title"), labelLimit);
        const auto agent = stringField(object, QStringLiteral("agent"), 128);
        const auto status = stringField(object, QStringLiteral("status"), 128);
        QString incarnation;
        QString mode;
        QString started;
        QString updated;
        QString host;
        const auto sizeValue = object.value(QStringLiteral("sizeBytes"));
        std::optional<quint64> size;
        if (!sizeValue.isNull()) {
            size = unsignedValue(sizeValue);
        }
        if (!sessionId || !title || !agent || !status
            || !nullableString(object, QStringLiteral("runtimeIncarnationId"), incarnation)
            || !nullableString(object, QStringLiteral("mode"), mode, 128)
            || !nullableString(object, QStringLiteral("startedAt"), started, 256)
            || !nullableString(object, QStringLiteral("updatedAt"), updated, 256)
            || !nullableString(object, QStringLiteral("hostType"), host, 128)
            || (!sizeValue.isNull() && !size)
            || !object.value(QStringLiteral("transcriptAvailable")).isBool()
            || (!incarnation.isEmpty() && !canonicalUuidV7(incarnation))
            || (object.value(QStringLiteral("transcriptAvailable")).toBool()
                && incarnation.isEmpty())) {
            setState(State::Failed, trText("Project session list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto identity =
            *sessionId + QLatin1Char('|') + incarnation;
        if (sessionIdentities.contains(identity)) {
            setState(State::Failed, trText("Project session list was malformed."));
            emit decodeError(m_error);
            return;
        }
        sessionIdentities.insert(identity);
        auto metadata = joinedMetadata(updated, started, host);
        if (size) {
            metadata = joinedMetadata(
                metadata,
                trText("%1 bytes").arg(*size));
        }
        sessions.push_back({
            .id = stableId(
                QStringLiteral("session|") + source->id + QLatin1Char('|')
                + identity),
            .runtimeSessionId = incarnation.isEmpty() ? QString {} : *sessionId,
            .runtimeIncarnationId = incarnation,
            .title = *title,
            .agent = *agent,
            .status = *status,
            .mode = mode,
            .metadata = std::move(metadata),
            .transcriptAvailable =
                object.value(QStringLiteral("transcriptAvailable")).toBool(),
        });
    }

    QVector<Memory> memories;
    const auto memoryValues = payload.value(QStringLiteral("memories")).toArray();
    if (memoryValues.size() > maximumMemories) {
        setState(State::Failed, trText("Project memory list exceeded its bound."));
        return;
    }
    QSet<QString> capabilityTokens;
    QHash<QString, quint32> memoryOccurrences;
    for (const auto& value : memoryValues) {
        if (!value.isObject()) {
            setState(State::Failed, trText("Project memory list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("readSelectionToken"),
                 QStringLiteral("openSelectionToken"),
                 QStringLiteral("copySelectionToken"),
                 QStringLiteral("filename"),
                 QStringLiteral("memoryType")})) {
            setState(State::Failed, trText("Project memory list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto readToken = stringField(object, QStringLiteral("readSelectionToken"));
        const auto openToken = stringField(object, QStringLiteral("openSelectionToken"));
        const auto copyToken = stringField(object, QStringLiteral("copySelectionToken"));
        const auto filename = stringField(object, QStringLiteral("filename"), 255);
        const auto memoryTypeValue =
            object.value(QStringLiteral("memoryType"));
        QString kind;
        if (!readToken || !openToken || !copyToken || !filename
            || !canonicalUuidV7(*readToken) || !canonicalUuidV7(*openToken)
            || !canonicalUuidV7(*copyToken)
            || !nullableString(object, QStringLiteral("memoryType"), kind, 128)
            || capabilityTokens.contains(*readToken)
            || capabilityTokens.contains(*openToken)
            || capabilityTokens.contains(*copyToken)) {
            setState(State::Failed, trText("Project memory list was malformed."));
            emit decodeError(m_error);
            return;
        }
        capabilityTokens.insert(*readToken);
        capabilityTokens.insert(*openToken);
        capabilityTokens.insert(*copyToken);
        const auto identity = *filename + QLatin1Char('|')
            + (memoryTypeValue.isNull()
                   ? QStringLiteral("<null>")
                   : QStringLiteral("<string>") + kind);
        const auto occurrence = memoryOccurrences[identity]++;
        Memory memory {
            .id = stableId(
                QStringLiteral("memory|") + source->id + QLatin1Char('|')
                + identity + QLatin1Char('|') + QString::number(occurrence)),
            .filename = *filename,
            .kind = kind,
            .memoryTypeIsNull = memoryTypeValue.isNull(),
            .readToken = *readToken,
            .openToken = *openToken,
            .copyToken = *copyToken,
        };
        (void)cachedMemoryDetail(
            memory,
            !m_invalidateDetailsAfterRefresh);
        memories.push_back(std::move(memory));
    }

    QVector<Agent> agents;
    const auto agentValues = payload.value(QStringLiteral("customAgents")).toArray();
    if (agentValues.size() > maximumAgents) {
        setState(State::Failed, trText("Custom Agent list exceeded its bound."));
        return;
    }
    QHash<QString, quint32> agentOccurrences;
    for (const auto& value : agentValues) {
        if (!value.isObject()) {
            setState(State::Failed, trText("Custom Agent list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("detailSelectionToken"),
                 QStringLiteral("openSelectionToken"),
                 QStringLiteral("target"),
                 QStringLiteral("name"),
                 QStringLiteral("description"),
                 QStringLiteral("model"),
                 QStringLiteral("tools"),
                 QStringLiteral("errorCount")})) {
            setState(State::Failed, trText("Custom Agent list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto detailToken =
            stringField(object, QStringLiteral("detailSelectionToken"));
        const auto openToken =
            stringField(object, QStringLiteral("openSelectionToken"));
        const auto target = stringField(object, QStringLiteral("target"), 64);
        const auto name = stringField(object, QStringLiteral("name"), 256, true);
        const auto description =
            stringField(object, QStringLiteral("description"), labelLimit, true);
        const auto errorCount =
            unsignedValue(object.value(QStringLiteral("errorCount")));
        const auto modelValue = object.value(QStringLiteral("model"));
        QString model;
        if (!detailToken || !openToken || !target || !name || !description
            || !canonicalUuidV7(*detailToken) || !canonicalUuidV7(*openToken)
            || !nullableString(object, QStringLiteral("model"), model, 256)
            || !object.value(QStringLiteral("tools")).isArray()
            || !errorCount || *errorCount > 65'535
            || capabilityTokens.contains(*detailToken)
            || capabilityTokens.contains(*openToken)) {
            setState(State::Failed, trText("Custom Agent list was malformed."));
            emit decodeError(m_error);
            return;
        }
        QStringList tools;
        for (const auto& tool : object.value(QStringLiteral("tools")).toArray()) {
            if (!tool.isString() || !bounded(tool.toString(), 128, false)
                || tools.size() >= 64) {
                setState(State::Failed, trText("Custom Agent tools exceeded their bound."));
                emit decodeError(m_error);
                return;
            }
            tools.push_back(tool.toString());
        }
        capabilityTokens.insert(*detailToken);
        capabilityTokens.insert(*openToken);
        const auto separator = QString(QChar(0x1f));
        const auto identity = *target + QLatin1Char('|') + *name
            + QLatin1Char('|') + *description + QLatin1Char('|')
            + (modelValue.isNull()
                   ? QStringLiteral("<null>")
                   : QStringLiteral("<string>") + model)
            + QLatin1Char('|') + tools.join(separator);
        const auto occurrence = agentOccurrences[identity]++;
        Agent agent {
            .id = stableId(
                QStringLiteral("agent|") + source->id + QLatin1Char('|')
                + identity + QLatin1Char('|') + QString::number(occurrence)),
            .target = *target,
            .name = *name,
            .description = *description,
            .model = model,
            .modelIsNull = modelValue.isNull(),
            .tools = tools,
            .errorCount = static_cast<quint32>(*errorCount),
            .detailToken = *detailToken,
            .openToken = *openToken,
        };
        (void)cachedAgentDetail(
            agent,
            !m_invalidateDetailsAfterRefresh);
        agents.push_back(std::move(agent));
    }

    QVector<PresentationListModel::Row> customizationRows;
    const auto customizationValues =
        payload.value(QStringLiteral("customizations")).toArray();
    if (customizationValues.size() > maximumCustomizations) {
        setState(State::Failed, trText("Project customization list exceeded its bound."));
        return;
    }
    QHash<QString, quint32> customizationOccurrences;
    for (const auto& value : customizationValues) {
        if (!value.isObject()) {
            setState(State::Failed, trText("Project customization list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("kind"),
                 QStringLiteral("name"),
                 QStringLiteral("scope"),
                 QStringLiteral("enabled"),
                 QStringLiteral("status"),
                 QStringLiteral("description"),
                 QStringLiteral("statusMessage")})) {
            setState(State::Failed, trText("Project customization list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto kind = stringField(object, QStringLiteral("kind"), 128);
        const auto name = stringField(object, QStringLiteral("name"), 256);
        const auto scope = stringField(object, QStringLiteral("scope"), 256);
        const auto status = stringField(object, QStringLiteral("status"), 128);
        QString description;
        QString statusMessage;
        if (!kind || !name || !scope || !status
            || !object.value(QStringLiteral("enabled")).isBool()
            || !nullableString(
                object,
                QStringLiteral("description"),
                description,
                labelLimit)
            || !nullableString(
                object,
                QStringLiteral("statusMessage"),
                statusMessage,
                labelLimit)) {
            setState(State::Failed, trText("Project customization list was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto identity = *kind + QLatin1Char('|') + *scope
            + QLatin1Char('|') + *name;
        const auto occurrence = customizationOccurrences[identity]++;
        if (!kind->contains(QStringLiteral("mcp"), Qt::CaseInsensitive)) {
            continue;
        }
        customizationRows.push_back({
            .itemId = stableId(
                QStringLiteral("customization|") + source->id
                + QLatin1Char('|') + identity + QLatin1Char('|')
                + QString::number(occurrence)),
            .title = *name,
            .subtitle = description,
            .kind = *kind,
            .status = *status,
            .metadata = joinedMetadata(*scope, statusMessage),
            .available = object.value(QStringLiteral("enabled")).toBool(),
        });
    }

    QVector<SettingsNode> settingsNodes;
    const auto settingsValues = payload.value(QStringLiteral("settings")).toArray();
    if (settingsValues.size() > 16) {
        setState(State::Failed, trText("Agent settings projection exceeded its bound."));
        emit decodeError(m_error);
        return;
    }
    QSet<QString> settingsAgents;
    for (const auto& value : settingsValues) {
        if (!value.isObject()) {
            setState(State::Failed, trText("Agent settings projection was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("agent"),
                 QStringLiteral("settings")})) {
            setState(State::Failed, trText("Agent settings projection was malformed."));
            emit decodeError(m_error);
            return;
        }
        const auto settingsAgent =
            stringField(object, QStringLiteral("agent"), 64);
        if (!settingsAgent
            || (*settingsAgent != QStringLiteral("claude")
                && *settingsAgent != QStringLiteral("copilot"))
            || settingsAgents.contains(*settingsAgent)
            || !object.value(QStringLiteral("settings")).isObject()) {
            setState(State::Failed, trText("Agent settings projection was malformed."));
            emit decodeError(m_error);
            return;
        }
        settingsAgents.insert(*settingsAgent);
        const auto scopes = object.value(QStringLiteral("settings")).toObject();
        if (!exactFields(
                scopes,
                {QStringLiteral("managed"),
                 QStringLiteral("user"),
                 QStringLiteral("project"),
                 QStringLiteral("local")})) {
            setState(State::Failed, trText("Agent settings projection was malformed."));
            emit decodeError(m_error);
            return;
        }
        for (const auto& scope : {
                 QStringLiteral("managed"),
                 QStringLiteral("user"),
                 QStringLiteral("project"),
                 QStringLiteral("local")}) {
            const auto treeValue = scopes.value(scope);
            if (treeValue.isNull()) {
                continue;
            }
            if (!treeValue.isObject()
                || !exactFields(
                    treeValue.toObject(),
                    {QStringLiteral("nodes")})
                || !treeValue.toObject().value(QStringLiteral("nodes")).isArray()) {
                setState(State::Failed, trText("Agent settings tree was malformed."));
                emit decodeError(m_error);
                return;
            }
            const auto nodeValues =
                treeValue.toObject().value(QStringLiteral("nodes")).toArray();
            if (nodeValues.size() > maximumSettingsTreeNodes
                || QJsonDocument(treeValue.toObject())
                        .toJson(QJsonDocument::Compact)
                        .size()
                    > maximumSettingsTreeBytes
                || settingsNodes.size() + nodeValues.size()
                    > maximumSettingsNodes) {
                setState(State::Failed, trText("Agent settings tree exceeded its bound."));
                emit decodeError(m_error);
                return;
            }
            QVector<SettingsNode> treeNodes;
            QHash<quint32, qsizetype> nodeIndices;
            for (const auto& nodeValue : nodeValues) {
                if (!nodeValue.isObject()) {
                    setState(State::Failed, trText("Agent settings tree was malformed."));
                    emit decodeError(m_error);
                    return;
                }
                const auto node = nodeValue.toObject();
                if (!exactFields(
                        node,
                        {QStringLiteral("nodeId"),
                         QStringLiteral("parentId"),
                         QStringLiteral("depth"),
                         QStringLiteral("key"),
                         QStringLiteral("kind"),
                         QStringLiteral("childCount"),
                         QStringLiteral("stringValue"),
                         QStringLiteral("integerValue"),
                         QStringLiteral("numberValue"),
                         QStringLiteral("booleanValue")})) {
                    setState(State::Failed, trText("Agent settings tree was malformed."));
                    emit decodeError(m_error);
                    return;
                }
                const auto nodeId = unsignedValue(node.value(QStringLiteral("nodeId")));
                const auto depth = unsignedValue(node.value(QStringLiteral("depth")));
                const auto key = stringField(node, QStringLiteral("key"), 512);
                const auto kind = stringField(node, QStringLiteral("kind"), 64);
                const auto children =
                    unsignedValue(node.value(QStringLiteral("childCount")));
                if (!nodeId || !depth || !key || !kind || !children
                    || *nodeId > std::numeric_limits<quint32>::max()
                    || *depth > maximumSettingsTreeDepth
                    || *children > std::numeric_limits<quint32>::max()
                    || nodeIndices.contains(static_cast<quint32>(*nodeId))
                    || (*kind != QStringLiteral("object")
                        && *kind != QStringLiteral("array")
                        && *kind != QStringLiteral("string")
                        && *kind != QStringLiteral("integer")
                        && *kind != QStringLiteral("number")
                        && *kind != QStringLiteral("boolean")
                        && *kind != QStringLiteral("null"))) {
                    setState(State::Failed, trText("Agent settings tree exceeded its bound."));
                    emit decodeError(m_error);
                    return;
                }
                std::optional<quint32> parent;
                const auto parentValue = node.value(QStringLiteral("parentId"));
                if (!parentValue.isNull()) {
                    const auto parentId = unsignedValue(parentValue);
                    if (!parentId
                        || *parentId > std::numeric_limits<quint32>::max()) {
                        setState(State::Failed, trText("Agent settings tree was malformed."));
                        emit decodeError(m_error);
                        return;
                    }
                    parent = static_cast<quint32>(*parentId);
                }
                if ((!parent && *depth != 0)
                    || (parent && (*depth == 0 || *parent == *nodeId))) {
                    setState(State::Failed, trText("Agent settings tree was malformed."));
                    emit decodeError(m_error);
                    return;
                }

                const auto stringValue =
                    node.value(QStringLiteral("stringValue"));
                const auto integerValue =
                    node.value(QStringLiteral("integerValue"));
                const auto numberValue =
                    node.value(QStringLiteral("numberValue"));
                const auto booleanValue =
                    node.value(QStringLiteral("booleanValue"));
                QString decodedString;
                std::optional<qint64> decodedInteger;
                std::optional<double> decodedNumber;
                std::optional<bool> decodedBoolean;
                if (!stringValue.isNull()) {
                    if (!stringValue.isString()
                        || !bounded(
                            stringValue.toString(),
                            maximumSettingsStringBytes,
                            true)) {
                        setState(State::Failed, trText("Agent settings tree was malformed."));
                        emit decodeError(m_error);
                        return;
                    }
                    decodedString = stringValue.toString();
                }
                if (!integerValue.isNull()) {
                    decodedInteger = signedValue(integerValue);
                    if (!decodedInteger) {
                        setState(State::Failed, trText("Agent settings tree was malformed."));
                        emit decodeError(m_error);
                        return;
                    }
                }
                if (!numberValue.isNull()) {
                    if (!numberValue.isDouble()
                        || !std::isfinite(numberValue.toDouble())) {
                        setState(State::Failed, trText("Agent settings tree was malformed."));
                        emit decodeError(m_error);
                        return;
                    }
                    decodedNumber = numberValue.toDouble();
                }
                if (!booleanValue.isNull()) {
                    if (!booleanValue.isBool()) {
                        setState(State::Failed, trText("Agent settings tree was malformed."));
                        emit decodeError(m_error);
                        return;
                    }
                    decodedBoolean = booleanValue.toBool();
                }
                const auto hasString = !stringValue.isNull();
                const auto hasInteger = !integerValue.isNull();
                const auto hasNumber = !numberValue.isNull();
                const auto hasBoolean = !booleanValue.isNull();
                const auto valueCount = static_cast<int>(hasString)
                    + static_cast<int>(hasInteger)
                    + static_cast<int>(hasNumber)
                    + static_cast<int>(hasBoolean);
                const auto scalarShapeValid =
                    ((*kind == QStringLiteral("object")
                         || *kind == QStringLiteral("array")
                         || *kind == QStringLiteral("null"))
                        && valueCount == 0)
                    || (*kind == QStringLiteral("string")
                        && hasString && valueCount == 1)
                    || (*kind == QStringLiteral("integer")
                        && hasInteger && valueCount == 1)
                    || (*kind == QStringLiteral("number")
                        && hasNumber && valueCount == 1)
                    || (*kind == QStringLiteral("boolean")
                        && hasBoolean && valueCount == 1);
                if (!scalarShapeValid
                    || ((*kind != QStringLiteral("object")
                            && *kind != QStringLiteral("array"))
                        && *children != 0)) {
                    setState(State::Failed, trText("Agent settings tree was malformed."));
                    emit decodeError(m_error);
                    return;
                }
                QString scalar;
                if (decodedInteger) {
                    scalar = QString::number(*decodedInteger);
                } else if (decodedNumber) {
                    scalar = QString::number(*decodedNumber, 'g', 17);
                } else if (decodedBoolean) {
                    scalar = *decodedBoolean
                        ? trText("On") : trText("Off");
                } else if (!stringValue.isNull()) {
                    scalar = decodedString;
                } else if (*kind == QStringLiteral("null")) {
                    scalar = trText("Not set");
                }
                nodeIndices.insert(
                    static_cast<quint32>(*nodeId),
                    treeNodes.size());
                treeNodes.push_back({
                    .agent = *settingsAgent,
                    .scope = scope,
                    .nodeId = static_cast<quint32>(*nodeId),
                    .parentId = parent,
                    .depth = static_cast<quint16>(*depth),
                    .key = *key,
                    .kind = *kind,
                    .childCount = static_cast<quint32>(*children),
                    .value = scalar,
                });
            }
            QHash<quint32, quint32> actualChildCounts;
            auto rootCount = 0;
            for (const auto& node : treeNodes) {
                if (!node.parentId) {
                    ++rootCount;
                    continue;
                }
                const auto parentIndex = nodeIndices.constFind(*node.parentId);
                if (parentIndex == nodeIndices.cend()) {
                    setState(State::Failed, trText("Agent settings tree was malformed."));
                    emit decodeError(m_error);
                    return;
                }
                const auto& parent = treeNodes.at(*parentIndex);
                if ((parent.kind != QStringLiteral("object")
                        && parent.kind != QStringLiteral("array"))
                    || node.depth != parent.depth + 1) {
                    setState(State::Failed, trText("Agent settings tree was malformed."));
                    emit decodeError(m_error);
                    return;
                }
                ++actualChildCounts[*node.parentId];
            }
            for (const auto& node : treeNodes) {
                if (actualChildCounts.value(node.nodeId) != node.childCount) {
                    setState(State::Failed, trText("Agent settings tree was malformed."));
                    emit decodeError(m_error);
                    return;
                }
            }
            if (!treeNodes.isEmpty() && rootCount != 1) {
                setState(State::Failed, trText("Agent settings tree was malformed."));
                emit decodeError(m_error);
                return;
            }
            settingsNodes += treeNodes;
        }
    }

    const auto preservingSnapshot = m_hasSourceSnapshot;
    const auto invalidateDetails =
        std::exchange(m_invalidateDetailsAfterRefresh, false);
    const auto resumesMemory =
        m_resumeAfterSnapshot
        && (m_resumeAfterSnapshot->operation == Operation::ReadMemory
            || m_resumeAfterSnapshot->operation == Operation::OpenMemory);
    const auto resumesAgent =
        m_resumeAfterSnapshot
        && (m_resumeAfterSnapshot->operation == Operation::ReadAgent
            || m_resumeAfterSnapshot->operation == Operation::OpenAgent);

    source->selectionToken = *sourceToken;
    m_sessions = std::move(sessions);
    m_memories = std::move(memories);
    m_agents = std::move(agents);
    m_settingsNodes = std::move(settingsNodes);
    if (invalidateDetails) {
        invalidateRefreshedDetailState();
        m_selectedMemoryId.clear();
        m_memoryContent.clear();
        m_memoryError.clear();
        m_memoryLoading = false;
        m_selectedAgentId.clear();
        m_agentDescription.clear();
        m_agentModel.clear();
        m_agentTools.clear();
        m_agentParseErrors.clear();
        m_agentFrontmatter.clear();
        m_agentPrompt.clear();
        m_agentDetailError.clear();
        m_agentDetailLoading = false;
    } else if (preservingSnapshot && !resumesMemory) {
        if (memoryForId(m_selectedMemoryId) == nullptr) {
            m_selectedMemoryId.clear();
            m_memoryContent.clear();
        }
        m_memoryLoading = false;
        m_memoryError.clear();
    }
    if (!invalidateDetails && preservingSnapshot && !resumesAgent) {
        if (agentForId(m_selectedAgentId) == nullptr) {
            m_selectedAgentId.clear();
            m_agentDescription.clear();
            m_agentModel.clear();
            m_agentTools.clear();
            m_agentParseErrors.clear();
            m_agentFrontmatter.clear();
            m_agentPrompt.clear();
        }
        m_agentDetailLoading = false;
        m_agentDetailError.clear();
    }
    QVector<PresentationListModel::Row> sessionRows;
    for (const auto& session : m_sessions) {
        sessionRows.push_back({
            .itemId = session.id,
            .title = session.title,
            .subtitle = session.metadata,
            .kind = session.transcriptAvailable
                ? QStringLiteral("active") : QStringLiteral("archived"),
            .agent = session.agent,
            .status = session.status,
            .mode = session.mode,
            .available = session.transcriptAvailable,
        });
    }
    QVector<PresentationListModel::Row> memoryRows;
    for (const auto& memory : m_memories) {
        memoryRows.push_back({
            .itemId = memory.id,
            .title = memory.filename,
            .subtitle = memory.kind,
            .kind = QStringLiteral("memory"),
            .available = true,
        });
    }
    QVector<PresentationListModel::Row> agentRows;
    for (const auto& agent : m_agents) {
        agentRows.push_back({
            .itemId = agent.id,
            .title = agent.name.isEmpty()
                ? trText("Unnamed agent") : agent.name,
            .subtitle = agent.description,
            .kind = QStringLiteral("agent"),
            .agent = agent.model,
            .metadata = agent.tools.join(QStringLiteral(", ")),
            .available = true,
            .numberA = agent.errorCount,
        });
    }
    m_sessionsModel.replace(std::move(sessionRows));
    m_memoriesModel.replace(std::move(memoryRows));
    m_agentsModel.replace(std::move(agentRows));
    m_customizationsModel.replace(std::move(customizationRows));
    rebuildSettingsTree();
    emit settingsFilterChanged();
    m_hasSourceSnapshot = true;
    setState(State::Ready);
    m_failedPending.reset();
    emit sourceChanged();
    emit memoryChanged();
    emit agentChanged();
    (void)resumeAfterSnapshot();
}

void ProjectIntelligenceModel::applyMemoryDetail(
    const QJsonObject& payload,
    const Pending& pending)
{
    if (pending.itemId != m_selectedMemoryId) {
        return;
    }
    QString memoryType;
    const auto content = stringField(
        payload,
        QStringLiteral("content"),
        maximumContentBytes,
        true);
    const auto selectionToken =
        stringField(payload, QStringLiteral("selectionToken"));
    const auto canonicalCwd =
        stringField(
            payload,
            QStringLiteral("canonicalCwd"),
            identifierLimit,
            true);
    const auto projectSlug =
        stringField(payload, QStringLiteral("projectSlug"));
    const auto filename =
        stringField(payload, QStringLiteral("filename"), 255);
    const auto memoryTypeValue =
        payload.value(QStringLiteral("memoryType"));
    auto* memory = memoryForId(pending.itemId);
    if (!exactFields(
            payload,
            {QStringLiteral("content"),
             QStringLiteral("selectionToken"),
             QStringLiteral("canonicalCwd"),
             QStringLiteral("projectSlug"),
             QStringLiteral("filename"),
             QStringLiteral("memoryType")})
        || !content || !selectionToken || !canonicalCwd || !projectSlug
        || !filename
        || !nullableString(
            payload,
            QStringLiteral("memoryType"),
            memoryType,
            128)
        || memory == nullptr
        || *selectionToken != pending.consumedToken
        || !validCanonicalWorkingDirectory(
            *canonicalCwd,
            pending.sourceKind)
        || !validProjectSlug(*projectSlug)
        || (pending.expectedCanonicalIdentityKnown
            && (*canonicalCwd != pending.expectedCanonicalCwd
                || *projectSlug != pending.expectedProjectSlug))
        || *filename != pending.expectedFilename
        || memoryType != pending.expectedMemoryType
        || memoryTypeValue.isNull()
            != pending.expectedMemoryTypeIsNull) {
        m_memoryContent.clear();
        m_memoryError =
            trText("The runtime returned Project Memory for a different selection.");
        rememberFailure(pending);
        emit decodeError(m_memoryError);
    } else {
        m_memoryContent = *content;
        m_memoryError.clear();
        storeMemoryDetail(
            *memory,
            *content,
            *canonicalCwd,
            *projectSlug);
        m_failedPending.reset();
        if (pending.reacquired) {
            emit actionMessage(
                trText("Project capabilities refreshed."),
                false);
        }
    }
    m_memoryLoading = false;
    emit memoryChanged();
}

void ProjectIntelligenceModel::applyAgentDetail(
    const QJsonObject& payload,
    const Pending& pending)
{
    if (pending.itemId != m_selectedAgentId) {
        return;
    }
    const auto selectionToken =
        stringField(payload, QStringLiteral("selectionToken"));
    const auto target =
        stringField(payload, QStringLiteral("target"), 64);
    const auto name =
        stringField(payload, QStringLiteral("name"), 256, true);
    const auto description =
        stringField(payload, QStringLiteral("description"), labelLimit, true);
    const auto frontmatter =
        stringField(
            payload,
            QStringLiteral("frontmatter"),
            maximumCustomAgentFrontmatterBytes,
            true);
    const auto prompt =
        stringField(
            payload,
            QStringLiteral("prompt"),
            maximumCustomAgentSourceBytes,
            true);
    const auto modelValue = payload.value(QStringLiteral("model"));
    auto* selectedAgent = agentForId(pending.itemId);
    QString model;
    QStringList tools;
    QStringList disallowedTools;
    auto arraysValid = true;
    if (payload.value(QStringLiteral("tools")).isArray()) {
        for (const auto& tool :
             payload.value(QStringLiteral("tools")).toArray()) {
            if (!tool.isString() || !bounded(tool.toString(), 128, false)
                || tools.size() >= 64) {
                arraysValid = false;
                break;
            }
            tools.push_back(tool.toString());
        }
    } else {
        arraysValid = false;
    }
    if (payload.value(QStringLiteral("disallowedTools")).isArray()) {
        for (const auto& tool :
             payload.value(QStringLiteral("disallowedTools")).toArray()) {
            if (!tool.isString() || !bounded(tool.toString(), 128, false)
                || disallowedTools.size() >= 64) {
                arraysValid = false;
                break;
            }
            disallowedTools.push_back(tool.toString());
        }
    } else {
        arraysValid = false;
    }
    QStringList parseErrors;
    if (payload.value(QStringLiteral("errors")).isArray()) {
        for (const auto& error :
             payload.value(QStringLiteral("errors")).toArray()) {
            if (!error.isString()
                || !bounded(
                    error.toString(),
                    maximumCustomAgentDetailTextBytes,
                    true)
                || parseErrors.size() >= 64) {
                arraysValid = false;
                break;
            }
            parseErrors.push_back(error.toString());
        }
    } else {
        arraysValid = false;
    }
    if (!exactFields(
            payload,
            {QStringLiteral("selectionToken"),
             QStringLiteral("target"),
             QStringLiteral("name"),
             QStringLiteral("description"),
             QStringLiteral("model"),
             QStringLiteral("tools"),
             QStringLiteral("disallowedTools"),
             QStringLiteral("errors"),
             QStringLiteral("frontmatter"),
             QStringLiteral("prompt")})
        || !selectionToken || !target || !name || !description
        || !frontmatter || !prompt
        || QJsonDocument(payload).toJson(QJsonDocument::Compact).size()
            > maximumCustomAgentDetailBytes
        || frontmatter->toUtf8().size() + prompt->toUtf8().size()
            > maximumCustomAgentSourceBytes
        || !nullableString(payload, QStringLiteral("model"), model, 256)
        || !arraysValid
        || selectedAgent == nullptr
        || *selectionToken != pending.consumedToken
        || *target != pending.expectedTarget
        || *name != pending.expectedName
        || *description != pending.expectedDescription
        || model != pending.expectedModel
        || modelValue.isNull() != pending.expectedModelIsNull
        || tools != pending.expectedTools
        || (pending.expectedDisallowedToolsKnown
            && disallowedTools
                != pending.expectedDisallowedTools)) {
        m_agentFrontmatter.clear();
        m_agentPrompt.clear();
        m_agentParseErrors.clear();
        m_agentDetailError =
            trText("The runtime returned a different Custom Agent selection.");
        rememberFailure(pending);
        emit decodeError(m_agentDetailError);
    } else {
        m_agentDescription = *description;
        m_agentModel = model;
        m_agentTools = tools;
        m_agentParseErrors = std::move(parseErrors);
        m_agentFrontmatter = *frontmatter;
        m_agentPrompt = *prompt;
        m_agentDetailError.clear();
        storeAgentDetail(
            *selectedAgent,
            disallowedTools,
            m_agentParseErrors,
            m_agentFrontmatter,
            m_agentPrompt);
        m_failedPending.reset();
        if (pending.reacquired) {
            emit actionMessage(
                trText("Project capabilities refreshed."),
                false);
        }
    }
    m_agentDetailLoading = false;
    emit agentChanged();
}

void ProjectIntelligenceModel::applyOpen(
    const QJsonObject& payload,
    const Pending& pending)
{
    const auto returnedHandoffId =
        payload.value(QStringLiteral("handoffId")).toString();
    if (!exactFields(
            payload,
            {QStringLiteral("displayName"),
             QStringLiteral("handoffId"),
             QStringLiteral("handoffPath")})) {
        if (canonicalUuidV7(returnedHandoffId)) {
            dispatchRelease(returnedHandoffId);
        }
        rememberFailure(pending);
        emit actionMessage(trText("The native open handoff was malformed."), true);
        return;
    }
    const auto displayName =
        stringField(payload, QStringLiteral("displayName"), 512, true);
    const auto handoffId = stringField(payload, QStringLiteral("handoffId"));
    const auto handoffPath = stringField(payload, QStringLiteral("handoffPath"));
    if (!displayName || !handoffId || !handoffPath
        || !canonicalUuidV7(*handoffId)
        || !QDir::isAbsolutePath(*handoffPath)
        || QDir::cleanPath(*handoffPath) != *handoffPath) {
        if (handoffId && canonicalUuidV7(*handoffId)) {
            dispatchRelease(*handoffId);
        }
        rememberFailure(pending);
        emit actionMessage(trText("The native open handoff was malformed."), true);
        return;
    }
    const auto purpose = pending.operation == Operation::OpenMemory
        ? DesktopFileIntegration::Purpose::ProjectMemory
        : DesktopFileIntegration::Purpose::ProjectCustomAgent;
    const auto opened = m_desktopFiles.openBoundHandoff(
        *handoffPath,
        QUuid::createUuidV7().toString(QUuid::WithoutBraces),
        purpose);
    dispatchRelease(*handoffId);
    emit actionMessage(
        opened ? trText("Opened in the default application.")
               : m_desktopFiles.errorMessage(),
        !opened);
    if (!opened) {
        rememberFailure(pending);
    } else {
        m_failedPending.reset();
    }
}

void ProjectIntelligenceModel::applyCopy(
    const QJsonObject& payload,
    const Pending& pending)
{
    if (!exactFields(
            payload,
            {QStringLiteral("mutationId"),
             QStringLiteral("outcome"),
             QStringLiteral("filename"),
             QStringLiteral("targetLabel"),
             QStringLiteral("detail")})) {
        settleCopyDelivery(
            trText("The copy receipt was malformed."),
            true);
        return;
    }
    const auto mutationId =
        stringField(payload, QStringLiteral("mutationId"));
    const auto outcome = stringField(payload, QStringLiteral("outcome"), 64);
    const auto filename = stringField(payload, QStringLiteral("filename"), 255);
    const auto target = stringField(payload, QStringLiteral("targetLabel"), labelLimit);
    const auto detailValue = payload.value(QStringLiteral("detail"));
    QString detail;
    if (detailValue.isString()) {
        detail = detailValue.toString();
    }
    if (!mutationId || !canonicalUuidV7(*mutationId)
        || *mutationId != pending.mutationId
        || !outcome || !filename || !target
        || (*outcome != QStringLiteral("copied")
            && *outcome != QStringLiteral("alreadyExists")
            && *outcome != QStringLiteral("indeterminate"))
        || *filename != pending.expectedFilename
        || *target != pending.expectedTargetLabel
        || ((*outcome == QStringLiteral("copied")
             || *outcome == QStringLiteral("alreadyExists"))
            && !detailValue.isNull())
        || (*outcome == QStringLiteral("indeterminate")
            && (!detailValue.isString()
                || !bounded(detail, metadataLimit, false)))) {
        settleCopyDelivery(
            trText("The copy receipt was malformed."),
            true);
        return;
    }
    if (*outcome == QStringLiteral("copied")) {
        emit actionMessage(
            trText("Copied %1 to %2.").arg(*filename, *target),
            false);
    } else if (*outcome == QStringLiteral("alreadyExists")) {
        emit actionMessage(
            trText("%1 already exists in %2.").arg(*filename, *target),
            false);
    } else {
        emit actionMessage(
            detail.isEmpty()
                ? trText("The copy outcome could not be confirmed.")
                : detail,
            true);
    }
    recoverConsumedOperation(pending);
}

void ProjectIntelligenceModel::applyError(
    const QJsonObject& object,
    const Pending& pending)
{
    if (!exactAccountEnvelope(
            object,
            {QStringLiteral("message"),
             QStringLiteral("failureKind"),
             QStringLiteral("mutationId"),
             QStringLiteral("reconciliationRequired")})) {
        if (pending.operation == Operation::CopyMemory
            || pending.operation == Operation::ReconcileCopy) {
            settleCopyDelivery(
                trText("Project Intelligence error reply was malformed."),
                true);
            return;
        }
        rememberFailure(pending);
        setState(
            State::Failed,
            trText("Project Intelligence error reply was malformed."));
        emit decodeError(m_error);
        return;
    }
    const auto message =
        stringField(object, QStringLiteral("message"), metadataLimit);
    const auto failureKind =
        stringField(object, QStringLiteral("failureKind"), 64);
    const auto mutationValue = object.value(QStringLiteral("mutationId"));
    const auto reconciliationValue =
        object.value(QStringLiteral("reconciliationRequired"));
    QString mutationId;
    if (mutationValue.isString()) {
        mutationId = mutationValue.toString();
    }
    if (!message || !failureKind
        || (*failureKind != QStringLiteral("deterministic")
            && *failureKind != QStringLiteral("deliveryAmbiguous"))
        || (!mutationValue.isNull()
            && (!mutationValue.isString()
                || !canonicalUuidV7(mutationId)))
        || !reconciliationValue.isBool()) {
        if (pending.operation == Operation::CopyMemory
            || pending.operation == Operation::ReconcileCopy) {
            settleCopyDelivery(
                trText("Project Intelligence error reply was malformed."),
                true);
            return;
        }
        rememberFailure(pending);
        setState(
            State::Failed,
            trText("Project Intelligence error reply was malformed."));
        emit decodeError(m_error);
        return;
    }

    if ((pending.operation == Operation::CopyMemory
         || pending.operation == Operation::ReconcileCopy)
        && *failureKind == QStringLiteral("deliveryAmbiguous")
        && reconciliationValue.toBool()
        && mutationId == pending.mutationId) {
        (void)dispatchCopyReconcile(
            pending,
            trText(
                "The copy result was ambiguous. Kodosi is reconciling it."));
        return;
    }

    rememberFailure(pending);
    if (pending.operation == Operation::ReadMemory) {
        m_memoryLoading = false;
        m_memoryError = *message;
        emit memoryChanged();
    } else if (pending.operation == Operation::ReadAgent) {
        m_agentDetailLoading = false;
        m_agentDetailError = *message;
        emit agentChanged();
    } else if (pending.operation == Operation::CopyMemory
        || pending.operation == Operation::ReconcileCopy) {
        settleCopyDelivery(*message);
    } else if (pending.operation == Operation::OpenMemory
        || pending.operation == Operation::OpenAgent) {
        emit actionMessage(*message, true);
        setState(State::Ready);
    } else {
        setState(State::Failed, *message);
    }
}

bool ProjectIntelligenceModel::handleReleaseEvent(
    const QJsonObject& object)
{
    const auto requestId = object.value(QStringLiteral("requestId")).toString();
    const auto found = m_releaseRequests.find(requestId);
    if (found == m_releaseRequests.end()) {
        return false;
    }
    const auto handoffId = found.value();
    const auto releaseStillPending =
        m_ownedHandoffs.contains(handoffId);
    m_releaseRequests.erase(found);
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("agent.intel.reply")
        && exactAccountEnvelope(object, {QStringLiteral("payload")})
        && object.contains(QStringLiteral("payload"))
        && object.value(QStringLiteral("payload")).isNull()) {
        m_ownedHandoffs.remove(handoffId);
        return true;
    }
    if (type == QStringLiteral("agent.intel.error")
        && exactAccountEnvelope(
            object,
            {QStringLiteral("message"),
             QStringLiteral("failureKind"),
             QStringLiteral("mutationId"),
             QStringLiteral("reconciliationRequired")})
        && stringField(
            object,
            QStringLiteral("message"),
            metadataLimit)
        && object.value(QStringLiteral("failureKind")).toString()
            == QStringLiteral("deterministic")
        && object.value(QStringLiteral("mutationId")).isNull()
        && object.value(QStringLiteral("reconciliationRequired")).isBool()
        && !object.value(QStringLiteral("reconciliationRequired")).toBool()) {
        if (releaseStillPending) {
            emit actionMessage(
                object.value(QStringLiteral("message")).toString(),
                true);
        }
        return true;
    }
    emit decodeError(trText("Open-handoff release reply was malformed."));
    return true;
}

bool ProjectIntelligenceModel::handleCancelledOpenEvent(
    const QJsonObject& object)
{
    const auto requestId = object.value(QStringLiteral("requestId")).toString();
    const auto found = m_cancelledOpenRequests.find(requestId);
    if (found == m_cancelledOpenRequests.end()) {
        return false;
    }
    m_cancelledOpenRequests.erase(found);
    if (object.value(QStringLiteral("type")).toString()
            != QStringLiteral("agent.intel.reply")
        || !object.value(QStringLiteral("payload")).isObject()) {
        return true;
    }
    const auto payload = object.value(QStringLiteral("payload")).toObject();
    const auto handoffId =
        stringField(payload, QStringLiteral("handoffId"));
    if (handoffId && canonicalUuidV7(*handoffId)) {
        dispatchRelease(*handoffId);
    }
    if (!exactAccountEnvelope(object, {QStringLiteral("payload")})) {
        emit decodeError(trText("Cancelled open-handoff reply was malformed."));
    }
    return true;
}

void ProjectIntelligenceModel::dispatchRelease(const QString& handoffId)
{
    if (!canonicalUuidV7(handoffId)) {
        return;
    }
    if (!m_ownedHandoffs.contains(handoffId)
        && m_ownedHandoffs.size() >= maximumReleaseRequests) {
        m_ownedHandoffs.erase(m_ownedHandoffs.begin());
    }
    m_ownedHandoffs.insert(handoffId);
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.releaseOpenHandoff")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("handoffId"), handoffId},
    };
    const auto result = m_dispatcher.send(
        CommandLane::AgentIntel,
        QJsonDocument(command).toJson(QJsonDocument::Compact));
    if (!result) {
        return;
    }
    if (m_releaseRequests.size() >= maximumReleaseRequests) {
        m_ownedHandoffs.remove(handoffId);
        return;
    }
    m_releaseRequests.insert(requestId, handoffId);
}

void ProjectIntelligenceModel::releaseOwnedHandoffsBestEffort()
{
    const auto handoffs = m_ownedHandoffs.values();
    for (const auto& handoffId : handoffs) {
        dispatchRelease(handoffId);
    }
}

void ProjectIntelligenceModel::rebuildSourceRows()
{
    QVector<PresentationListModel::Row> rows;
    QVector<PresentationListModel::Row> copyDestinationRows;
    rows.reserve(m_sources.size());
    copyDestinationRows.reserve(m_sources.size());
    for (const auto& source : m_sources) {
        PresentationListModel::Row row {
            .itemId = source.id,
            .title = source.label,
            .subtitle = trText(
                source.kind == QStringLiteral("active")
                    ? "Project intelligence"
                    : source.kind == QStringLiteral("claudeArchive")
                      ? "Claude project archive"
                      : "Copilot repository archive"),
            .kind = source.kind,
            .agent = source.agent,
            .available = !source.selectionToken.isEmpty(),
            .numberA = static_cast<qint64>(source.sessionCount),
            .numberB = source.memoryCount,
        };
        rows.push_back(row);
        if (source.kind != QStringLiteral("copilotArchive")
            && !source.ambiguousAuthorityIdentity) {
            copyDestinationRows.push_back(std::move(row));
        }
    }
    m_sourcesModel.replace(std::move(rows));
    m_copyDestinationsModel.replace(std::move(copyDestinationRows));
}

void ProjectIntelligenceModel::rebuildSettingsTree()
{
    QVector<PresentationListModel::Row> rows;
    for (const auto& node : m_settingsNodes) {
        if (node.agent != m_settingsAgent || node.scope != m_settingsScope) {
            continue;
        }
        rows.push_back({
            .itemId = stableId(
                QStringLiteral("setting|") + m_selectedSourceId + QLatin1Char('|')
                + node.agent + QLatin1Char('|') + node.scope + QLatin1Char('|')
                + QString::number(node.nodeId)),
            .title = node.key,
            .subtitle = node.value,
            .kind = node.kind,
            .agent = node.agent,
            .status = node.scope,
            .available = true,
            .numberA = node.depth,
            .numberB = node.childCount,
        });
    }
    m_settingsTreeModel.replace(std::move(rows));
}

std::optional<ProjectIntelligenceModel::MemoryDetailCache>
ProjectIntelligenceModel::cachedMemoryDetail(
    Memory& memory,
    const bool allowCachedContent)
{
    if (auto learned = m_learnedMemoryIdentities.find(memory.id);
        learned != m_learnedMemoryIdentities.end()) {
        if (learned->filename != memory.filename
            || learned->memoryType != memory.kind
            || learned->memoryTypeIsNull != memory.memoryTypeIsNull) {
            m_learnedMemoryIdentities.erase(learned);
            m_learnedMemoryIdentityOrder.removeAll(memory.id);
        } else {
            memory.canonicalCwd = learned->canonicalCwd;
            memory.projectSlug = learned->projectSlug;
            memory.canonicalIdentityKnown = true;
            m_learnedMemoryIdentityOrder.removeAll(memory.id);
            m_learnedMemoryIdentityOrder.push_back(memory.id);
        }
    }
    if (!allowCachedContent) {
        return std::nullopt;
    }
    const auto found = m_memoryDetailCache.find(memory.id);
    if (found == m_memoryDetailCache.end()) {
        return std::nullopt;
    }
    if (found->filename != memory.filename
        || found->memoryType != memory.kind
        || found->memoryTypeIsNull != memory.memoryTypeIsNull) {
        m_memoryDetailCacheBytes =
            std::max<qsizetype>(
                0,
                m_memoryDetailCacheBytes - found->byteSize);
        m_memoryDetailCache.erase(found);
        m_memoryDetailCacheOrder.removeAll(memory.id);
        return std::nullopt;
    }
    memory.canonicalCwd = found->canonicalCwd;
    memory.projectSlug = found->projectSlug;
    memory.canonicalIdentityKnown = true;
    m_memoryDetailCacheOrder.removeAll(memory.id);
    m_memoryDetailCacheOrder.push_back(memory.id);
    return *found;
}

std::optional<ProjectIntelligenceModel::AgentDetailCache>
ProjectIntelligenceModel::cachedAgentDetail(
    Agent& agent,
    const bool allowCachedContent)
{
    if (!allowCachedContent) {
        return std::nullopt;
    }
    if (auto learned = m_learnedAgentIdentities.find(agent.id);
        learned != m_learnedAgentIdentities.end()) {
        if (learned->target != agent.target
            || learned->name != agent.name
            || learned->description != agent.description
            || learned->model != agent.model
            || learned->modelIsNull != agent.modelIsNull
            || learned->tools != agent.tools) {
            m_learnedAgentIdentities.erase(learned);
            m_learnedAgentIdentityOrder.removeAll(agent.id);
        } else {
            agent.disallowedTools = learned->disallowedTools;
            agent.disallowedToolsKnown = true;
            m_learnedAgentIdentityOrder.removeAll(agent.id);
            m_learnedAgentIdentityOrder.push_back(agent.id);
        }
    }
    const auto found = m_agentDetailCache.find(agent.id);
    if (found == m_agentDetailCache.end()) {
        return std::nullopt;
    }
    if (found->target != agent.target
        || found->name != agent.name
        || found->description != agent.description
        || found->model != agent.model
        || found->modelIsNull != agent.modelIsNull
        || found->tools != agent.tools) {
        m_agentDetailCacheBytes =
            std::max<qsizetype>(
                0,
                m_agentDetailCacheBytes - found->byteSize);
        m_agentDetailCache.erase(found);
        m_agentDetailCacheOrder.removeAll(agent.id);
        return std::nullopt;
    }
    agent.disallowedTools = found->disallowedTools;
    agent.disallowedToolsKnown = true;
    m_agentDetailCacheOrder.removeAll(agent.id);
    m_agentDetailCacheOrder.push_back(agent.id);
    return *found;
}

void ProjectIntelligenceModel::storeMemoryDetail(
    Memory& memory,
    QString content,
    QString canonicalCwd,
    QString projectSlug)
{
    memory.canonicalCwd = canonicalCwd;
    memory.projectSlug = projectSlug;
    memory.canonicalIdentityKnown = true;
    m_learnedMemoryIdentityOrder.removeAll(memory.id);
    if (!m_learnedMemoryIdentities.contains(memory.id)
        && m_learnedMemoryIdentities.size()
            >= maximumLearnedItemIdentities) {
        const auto evictedId =
            m_learnedMemoryIdentityOrder.takeFirst();
        m_learnedMemoryIdentities.remove(evictedId);
    }
    m_learnedMemoryIdentities.insert(
        memory.id,
        LearnedMemoryIdentity {
            .canonicalCwd = canonicalCwd,
            .projectSlug = projectSlug,
            .filename = memory.filename,
            .memoryType = memory.kind,
            .memoryTypeIsNull = memory.memoryTypeIsNull,
        });
    m_learnedMemoryIdentityOrder.push_back(memory.id);
    MemoryDetailCache detail {
        .content = std::move(content),
        .canonicalCwd = std::move(canonicalCwd),
        .projectSlug = std::move(projectSlug),
        .filename = memory.filename,
        .memoryType = memory.kind,
        .memoryTypeIsNull = memory.memoryTypeIsNull,
    };
    detail.byteSize = encodedBytes(detail.content)
        + encodedBytes(detail.canonicalCwd)
        + encodedBytes(detail.projectSlug)
        + encodedBytes(detail.filename)
        + encodedBytes(detail.memoryType);
    if (const auto existing = m_memoryDetailCache.find(memory.id);
        existing != m_memoryDetailCache.end()) {
        m_memoryDetailCacheBytes =
            std::max<qsizetype>(
                0,
                m_memoryDetailCacheBytes - existing->byteSize);
        m_memoryDetailCache.erase(existing);
    }
    m_memoryDetailCacheOrder.removeAll(memory.id);
    if (detail.byteSize > maximumDetailCacheBytes) {
        return;
    }
    while (!m_memoryDetailCacheOrder.isEmpty()
        && (m_memoryDetailCache.size() >= maximumDetailCacheEntries
            || m_memoryDetailCacheBytes + detail.byteSize
                > maximumDetailCacheBytes)) {
        const auto evictedId = m_memoryDetailCacheOrder.takeFirst();
        const auto evicted = m_memoryDetailCache.take(evictedId);
        m_memoryDetailCacheBytes =
            std::max<qsizetype>(
                0,
                m_memoryDetailCacheBytes - evicted.byteSize);
    }
    m_memoryDetailCacheBytes += detail.byteSize;
    m_memoryDetailCache.insert(memory.id, std::move(detail));
    m_memoryDetailCacheOrder.push_back(memory.id);
}

void ProjectIntelligenceModel::storeAgentDetail(
    Agent& agent,
    QStringList disallowedTools,
    QStringList parseErrors,
    QString frontmatter,
    QString prompt)
{
    agent.disallowedTools = disallowedTools;
    agent.disallowedToolsKnown = true;
    m_learnedAgentIdentityOrder.removeAll(agent.id);
    if (!m_learnedAgentIdentities.contains(agent.id)
        && m_learnedAgentIdentities.size()
            >= maximumLearnedItemIdentities) {
        const auto evictedId =
            m_learnedAgentIdentityOrder.takeFirst();
        m_learnedAgentIdentities.remove(evictedId);
    }
    m_learnedAgentIdentities.insert(
        agent.id,
        LearnedAgentIdentity {
            .target = agent.target,
            .name = agent.name,
            .description = agent.description,
            .model = agent.model,
            .modelIsNull = agent.modelIsNull,
            .tools = agent.tools,
            .disallowedTools = disallowedTools,
        });
    m_learnedAgentIdentityOrder.push_back(agent.id);
    AgentDetailCache detail {
        .target = agent.target,
        .name = agent.name,
        .description = agent.description,
        .model = agent.model,
        .modelIsNull = agent.modelIsNull,
        .tools = agent.tools,
        .disallowedTools = std::move(disallowedTools),
        .parseErrors = std::move(parseErrors),
        .frontmatter = std::move(frontmatter),
        .prompt = std::move(prompt),
    };
    detail.byteSize = encodedBytes(detail.target)
        + encodedBytes(detail.name)
        + encodedBytes(detail.description)
        + encodedBytes(detail.model)
        + encodedBytes(detail.tools)
        + encodedBytes(detail.disallowedTools)
        + encodedBytes(detail.parseErrors)
        + encodedBytes(detail.frontmatter)
        + encodedBytes(detail.prompt);
    if (const auto existing = m_agentDetailCache.find(agent.id);
        existing != m_agentDetailCache.end()) {
        m_agentDetailCacheBytes =
            std::max<qsizetype>(
                0,
                m_agentDetailCacheBytes - existing->byteSize);
        m_agentDetailCache.erase(existing);
    }
    m_agentDetailCacheOrder.removeAll(agent.id);
    if (detail.byteSize > maximumDetailCacheBytes) {
        return;
    }
    while (!m_agentDetailCacheOrder.isEmpty()
        && (m_agentDetailCache.size() >= maximumDetailCacheEntries
            || m_agentDetailCacheBytes + detail.byteSize
                > maximumDetailCacheBytes)) {
        const auto evictedId = m_agentDetailCacheOrder.takeFirst();
        const auto evicted = m_agentDetailCache.take(evictedId);
        m_agentDetailCacheBytes =
            std::max<qsizetype>(
                0,
                m_agentDetailCacheBytes - evicted.byteSize);
    }
    m_agentDetailCacheBytes += detail.byteSize;
    m_agentDetailCache.insert(agent.id, std::move(detail));
    m_agentDetailCacheOrder.push_back(agent.id);
}

void ProjectIntelligenceModel::invalidateRefreshedDetailState()
{
    m_memoryDetailCache.clear();
    m_agentDetailCache.clear();
    m_learnedAgentIdentities.clear();
    m_memoryDetailCacheOrder.clear();
    m_agentDetailCacheOrder.clear();
    m_learnedAgentIdentityOrder.clear();
    m_memoryDetailCacheBytes = 0;
    m_agentDetailCacheBytes = 0;
}

void ProjectIntelligenceModel::clearDetailCaches()
{
    invalidateRefreshedDetailState();
    m_learnedMemoryIdentities.clear();
    m_learnedMemoryIdentityOrder.clear();
}

void ProjectIntelligenceModel::clearSourceDetails()
{
    m_hasSourceSnapshot = false;
    m_sessions.clear();
    m_memories.clear();
    m_agents.clear();
    m_settingsNodes.clear();
    m_sessionsModel.replace({});
    m_memoriesModel.replace({});
    m_agentsModel.replace({});
    m_customizationsModel.replace({});
    m_settingsTreeModel.replace({});
    m_selectedMemoryId.clear();
    m_selectedAgentId.clear();
    m_memoryContent.clear();
    m_memoryError.clear();
    m_agentDescription.clear();
    m_agentModel.clear();
    m_agentTools.clear();
    m_agentParseErrors.clear();
    m_agentFrontmatter.clear();
    m_agentPrompt.clear();
    m_agentDetailError.clear();
    m_memoryLoading = false;
    m_agentDetailLoading = false;
    m_conversation.close();
    emit memoryChanged();
    emit agentChanged();
    emit settingsFilterChanged();
}

void ProjectIntelligenceModel::clearAll()
{
    cancelPending();
    clearDetailCaches();
    m_failedPending.reset();
    m_reacquireAfterList.reset();
    m_resumeAfterSnapshot.reset();
    m_invalidateDetailsAfterRefresh = false;
    m_rebindSources.clear();
    m_rebindUsedSourceIds.clear();
    m_sourceSelectionTokens.clear();
    m_seenSourceCursors.clear();
    m_sources.clear();
    m_sourcesModel.replace({});
    m_copyDestinationsModel.replace({});
    m_nextCursor.clear();
    m_hasMoreSources = false;
    m_selectedSourceId.clear();
    clearSourceDetails();
    setState(State::Dormant);
    emit sourceChanged();
}

void ProjectIntelligenceModel::setState(State state, QString error)
{
    if (m_state == state && m_error == error) {
        return;
    }
    m_state = state;
    m_error = std::move(error);
    emit stateChanged();
}

bool ProjectIntelligenceModel::pendingCurrent(const Pending& pending) const
{
    return pending.runtimeGeneration == m_runtimeGeneration
        && pending.demandGeneration == m_demandGeneration;
}

bool ProjectIntelligenceModel::mutationCurrent(const Pending& pending) const
{
    return pending.runtimeGeneration == m_runtimeGeneration;
}

QString ProjectIntelligenceModel::stableId(const QString& key)
{
    auto found = m_stableIds.find(key);
    if (found != m_stableIds.end()) {
        return found.value();
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_stableIds.insert(key, id);
    return id;
}

ProjectIntelligenceModel::Source* ProjectIntelligenceModel::sourceForId(
    const QString& id)
{
    const auto found = std::find_if(
        m_sources.begin(),
        m_sources.end(),
        [&id](const Source& source) { return source.id == id; });
    return found == m_sources.end() ? nullptr : &*found;
}

const ProjectIntelligenceModel::Source* ProjectIntelligenceModel::sourceForId(
    const QString& id) const
{
    const auto found = std::find_if(
        m_sources.cbegin(),
        m_sources.cend(),
        [&id](const Source& source) { return source.id == id; });
    return found == m_sources.cend() ? nullptr : &*found;
}

ProjectIntelligenceModel::Memory* ProjectIntelligenceModel::memoryForId(
    const QString& id)
{
    const auto found = std::find_if(
        m_memories.begin(),
        m_memories.end(),
        [&id](const Memory& memory) { return memory.id == id; });
    return found == m_memories.end() ? nullptr : &*found;
}

ProjectIntelligenceModel::Agent* ProjectIntelligenceModel::agentForId(
    const QString& id)
{
    const auto found = std::find_if(
        m_agents.begin(),
        m_agents.end(),
        [&id](const Agent& agent) { return agent.id == id; });
    return found == m_agents.end() ? nullptr : &*found;
}

} // namespace kodosi
