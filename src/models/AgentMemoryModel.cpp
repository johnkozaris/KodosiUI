#include "models/AgentMemoryModel.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype identifierLimit = 4'096;
constexpr qsizetype accountIdLimit = 1'024;
constexpr qsizetype memoryTypeLimit = 128;
constexpr qsizetype filenameLimit = 255;
constexpr qsizetype noticeLimit = 4'096;

bool boundedText(
    const QStringView value,
    const qsizetype maximum,
    const bool allowEmpty)
{
    if ((!allowEmpty && value.isEmpty()) || value.contains(QChar::Null)) {
        return false;
    }
    return value.toUtf8().size() <= maximum;
}

std::optional<QString> requiredString(
    const QJsonObject& object,
    const QString& key,
    const qsizetype maximum = identifierLimit,
    const bool allowEmpty = false)
{
    const auto value = object.value(key);
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto text = value.toString();
    return boundedText(text, maximum, allowEmpty)
        ? std::optional<QString>(text)
        : std::nullopt;
}

bool nullableString(
    const QJsonObject& object,
    const QString& key,
    std::optional<QString>& destination,
    const qsizetype maximum,
    const bool allowEmpty)
{
    if (!object.contains(key)) {
        return false;
    }
    const auto value = object.value(key);
    if (value.isNull()) {
        destination.reset();
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    const auto text = value.toString();
    if (!boundedText(text, maximum, allowEmpty)) {
        return false;
    }
    destination = text;
    return true;
}

bool nullableAccountId(
    const QJsonObject& object,
    const QString& key,
    QString& destination)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        destination.clear();
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    const auto text = value.toString();
    if (!boundedText(text, accountIdLimit, false)) {
        return false;
    }
    destination = text;
    return true;
}

bool exactFields(
    const QJsonObject& object,
    const std::initializer_list<QString> fields)
{
    const auto keys = object.keys();
    return QSet<QString>(keys.begin(), keys.end())
        == QSet<QString>(fields.begin(), fields.end());
}

bool canonicalUuidV7(const QString& value)
{
    const QUuid id(value);
    return !id.isNull() && id.version() == QUuid::UnixEpoch
        && id.toString(QUuid::WithoutBraces) == value;
}

bool validWorkingDirectory(const QString& path)
{
    return boundedText(path, identifierLimit, false)
        && QDir::isAbsolutePath(path)
        && QDir::cleanPath(path) == path;
}

bool validProjectSlug(const QString& slug)
{
    return boundedText(slug, identifierLimit, false)
        && slug != QStringLiteral(".") && slug != QStringLiteral("..")
        && !slug.contains(QLatin1Char('/'))
        && !slug.contains(QLatin1Char('\\'));
}

bool validFilename(const QString& filename)
{
    if (!boundedText(filename, filenameLimit, false)
        || filename == QStringLiteral(".")
        || filename == QStringLiteral("..")
        || filename.contains(QLatin1Char('/'))
        || filename.contains(QLatin1Char('\\'))) {
        return false;
    }
    const QFileInfo info(filename);
    return info.fileName() == filename
        && info.suffix() == QStringLiteral("md");
}

} // namespace

AgentMemoryModel::AgentMemoryModel(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    AgentSessionIntelModel& sessionIntel,
    const qint64 replyTimeoutMs,
    QObject* parent)
    : QAbstractListModel(parent)
    , m_dispatcher(dispatcher)
    , m_sessions(sessions)
    , m_sessionIntel(sessionIntel)
    , m_replyTimeoutMs(replyTimeoutMs)
{
    Q_ASSERT(replyTimeoutMs >= 0);
    Q_ASSERT(replyTimeoutMs <= std::numeric_limits<int>::max());
    m_replyTimer.setSingleShot(true);
    connect(&m_replyTimer, &QTimer::timeout, this, [this] {
        if (!m_pending || !pendingIsCurrent(*m_pending)) {
            return;
        }
        setFailure(
            m_pending->operation,
            QStringLiteral("The runtime did not reply in time."),
            false);
    });
    const auto changed = [this] { authorityChanged(); };
    connect(&sessions, &QAbstractItemModel::modelReset, this, changed);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, changed);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, changed);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, changed);
    connect(
        &sessionIntel,
        &QAbstractItemModel::modelReset,
        this,
        changed);
    connect(
        &sessionIntel,
        &QAbstractItemModel::dataChanged,
        this,
        changed);
    connect(
        &sessionIntel,
        &AgentSessionIntelModel::hydrationStateChanged,
        this,
        changed);
}

int AgentMemoryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant AgentMemoryModel::data(
    const QModelIndex& index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_items.size()) {
        return {};
    }
    const auto& item = m_items.at(index.row());
    switch (role) {
    case FilenameRole: return item.filename;
    case KindRole: return item.memoryType.value_or(QString {});
    case DescriptionRole: return item.description;
    default: return {};
    }
}

QHash<int, QByteArray> AgentMemoryModel::roleNames() const
{
    return {
        {FilenameRole, QByteArrayLiteral("filename")},
        {KindRole, QByteArrayLiteral("kind")},
        {DescriptionRole, QByteArrayLiteral("description")},
    };
}

AgentMemoryModel::State AgentMemoryModel::state() const noexcept
{
    return m_state;
}

bool AgentMemoryModel::loading() const noexcept
{
    return m_state == State::Waiting || m_state == State::Loading;
}

QString AgentMemoryModel::error() const
{
    return m_error;
}

QString AgentMemoryModel::statusMessage() const
{
    return m_statusMessage;
}

QString AgentMemoryModel::selectedFilename() const
{
    return m_selectedFilename;
}

QString AgentMemoryModel::content() const
{
    return m_content;
}

bool AgentMemoryModel::contentLoaded() const noexcept
{
    return m_contentLoaded;
}

bool AgentMemoryModel::contentLoading() const noexcept
{
    return m_contentLoading;
}

QString AgentMemoryModel::contentError() const
{
    return m_contentError;
}

bool AgentMemoryModel::inspect(const QString& sessionId)
{
    if (!boundedText(sessionId, identifierLimit, false)) {
        return false;
    }
    if (m_demanded && sessionId == m_selectedSessionId) {
        attemptEligibility();
        return true;
    }
    clearAuthority(true);
    ++m_demandGeneration;
    m_demanded = true;
    m_selectedSessionId = sessionId;
    setState(
        State::Waiting,
        {},
        QStringLiteral("Waiting for the current Claude session identity."));
    attemptEligibility();
    return true;
}

void AgentMemoryModel::close()
{
    ++m_demandGeneration;
    ++m_selectionGeneration;
    clearAuthority(true);
}

bool AgentMemoryModel::retry()
{
    if (!m_demanded || m_pending) {
        return false;
    }
    if (m_failedOperation == Operation::Read
        && !m_selectedFilename.isEmpty()) {
        m_retrySelectionFilename = m_selectedFilename;
    }
    if (m_selectedIncarnationId.isEmpty()) {
        if (m_sessionIntel.hydrationState()
            == AgentSessionIntelModel::HydrationState::Failed) {
            (void)m_sessionIntel.refresh();
        }
        attemptEligibility();
        return true;
    }
    return beginList();
}

bool AgentMemoryModel::select(const QString& filename)
{
    if (!m_demanded
        || !boundedText(filename, filenameLimit, false)) {
        return false;
    }
    if (m_pending) {
        if (m_pending->operation != Operation::Read) {
            return false;
        }
        m_replyTimer.stop();
        m_pending.reset();
        setContentState(false);
    }
    const auto found =
        std::ranges::find(m_items, filename, &Item::filename);
    if (found == m_items.end()) {
        return false;
    }
    ++m_selectionGeneration;
    m_selectedFilename = filename;
    m_content.clear();
    m_contentLoaded = false;
    m_retrySelectionFilename.clear();
    setContentState(false);
    emit selectionChanged();
    const auto index = std::distance(m_items.begin(), found);
    if (!found->tokenAvailable) {
        m_retrySelectionFilename = filename;
        return beginList();
    }
    return beginRead(index);
}

void AgentMemoryModel::ingestAuthEvent(QByteArray json)
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
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    QString userId;
    const auto validUserId = type == QStringLiteral("auth.required")
        || nullableAccountId(object, QStringLiteral("userId"), userId);
    if (!epoch || !validUserId) {
        emit decodeError(
            QStringLiteral("Authentication context is malformed."));
        return;
    }
    activateAccount(std::move(userId), *epoch);
}

void AgentMemoryModel::ingestAgentIntelEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(
            QStringLiteral("Agent-intelligence event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(
            QStringLiteral("Agent-intelligence event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    QString accountUserId;
    if (!epoch
        || !nullableAccountId(
            object,
            QStringLiteral("accountUserId"),
            accountUserId)) {
        emit decodeError(
            QStringLiteral("Agent-intelligence event has malformed account authority."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {.userId = std::move(accountUserId), .epoch = *epoch},
        json);
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(
            QStringLiteral("Future Project Memory reply exceeds the ABI frame limit."));
    }
    if (admission == AccountEventAdmission::Current) {
        applyAgentIntelEvent(json);
    }
}

void AgentMemoryModel::resetRuntimeAuthority()
{
    ++m_runtimeGeneration;
    ++m_demandGeneration;
    ++m_selectionGeneration;
    m_accountFence.reset();
    m_hasAccountContext = false;
    clearAuthority(true);
}

void AgentMemoryModel::activateAccount(
    QString userId,
    const quint64 epoch)
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
        ++m_demandGeneration;
        ++m_selectionGeneration;
        clearAuthority(true);
    }
    for (auto& pending : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(pending));
    }
    if (m_demanded) {
        attemptEligibility();
    }
}

void AgentMemoryModel::applyAgentIntelEvent(const QByteArray& json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("agent.intel.reply")
        && type != QStringLiteral("agent.intel.error")) {
        return;
    }
    const auto requestId =
        requiredString(object, QStringLiteral("requestId"));
    if (!requestId || !canonicalUuidV7(*requestId)
        || !m_pending || m_pending->requestId != *requestId
        || !pendingIsCurrent(*m_pending)) {
        return;
    }
    const auto hasAccountUserId =
        object.contains(QStringLiteral("accountUserId"));
    const auto validEnvelope = type == QStringLiteral("agent.intel.reply")
        ? (hasAccountUserId
                ? exactFields(
                    object,
                    {QStringLiteral("authority"),
                     QStringLiteral("accountUserId"),
                     QStringLiteral("accountEpoch"),
                     QStringLiteral("type"),
                     QStringLiteral("requestId"),
                     QStringLiteral("payload")})
                : exactFields(
                    object,
                    {QStringLiteral("authority"),
                     QStringLiteral("accountEpoch"),
                     QStringLiteral("type"),
                     QStringLiteral("requestId"),
                     QStringLiteral("payload")}))
        : (hasAccountUserId
                ? exactFields(
                    object,
                    {QStringLiteral("authority"),
                     QStringLiteral("accountUserId"),
                     QStringLiteral("accountEpoch"),
                     QStringLiteral("type"),
                     QStringLiteral("requestId"),
                     QStringLiteral("message")})
                : exactFields(
                    object,
                    {QStringLiteral("authority"),
                     QStringLiteral("accountEpoch"),
                     QStringLiteral("type"),
                     QStringLiteral("requestId"),
                     QStringLiteral("message")}));
    if (!validEnvelope) {
        setFailure(
            m_pending->operation,
            QStringLiteral("The runtime returned a malformed Project Memory reply."),
            true);
        return;
    }
    if (type == QStringLiteral("agent.intel.error")) {
        const auto message =
            requiredString(object, QStringLiteral("message"), noticeLimit);
        const auto operation = m_pending->operation;
        setFailure(
            operation,
            message
                ? (operation == Operation::List
                        ? QStringLiteral(
                            "Couldn't load Project Memory. Try again.")
                        : QStringLiteral(
                            "Couldn't read the selected memory file. Refresh and try again."))
                : QStringLiteral(
                    "The runtime returned a malformed Project Memory error."),
            !message);
        return;
    }
    const auto payloadValue = object.value(QStringLiteral("payload"));
    if (!payloadValue.isObject()) {
        setFailure(
            m_pending->operation,
            QStringLiteral("The runtime returned a malformed Project Memory reply."),
            true);
        return;
    }
    const auto pending = *m_pending;
    if (pending.operation == Operation::List) {
        QVector<Item> items;
        if (!decodeListReply(payloadValue.toObject(), items)) {
            setFailure(
                pending.operation,
                QStringLiteral("The runtime returned an invalid Project Memory list."),
                true);
            return;
        }
        applyList(std::move(items));
        return;
    }
    QString content;
    if (!decodeReadReply(payloadValue.toObject(), pending, content)) {
        setFailure(
            pending.operation,
            QStringLiteral("The runtime returned Project Memory for a different selection."),
            true);
        return;
    }
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation.reset();
    m_content = std::move(content);
    m_contentLoaded = true;
    setContentState(false);
    emit selectionChanged();
}

void AgentMemoryModel::authorityChanged()
{
    if (!m_demanded) {
        return;
    }
    const auto context =
        m_sessions.conversationContext(m_selectedSessionId);
    if (!m_selectedIncarnationId.isEmpty()
        && (!context
            || context->incarnationId != m_selectedIncarnationId)) {
        invalidateDemand(
            QStringLiteral(
                "The selected session changed. Reopen Project Memory."));
        return;
    }
    if (!m_selectedIncarnationId.isEmpty()) {
        const auto identity = context
            ? m_sessionIntel.projectMemoryContext(
                  m_selectedSessionId,
                  m_selectedIncarnationId)
            : std::nullopt;
        if (!identity || identity->agent != m_selectedAgent
            || identity->workingDirectory != m_workingDirectory
            || context->workingDirectory != m_workingDirectory) {
            m_replyTimer.stop();
            m_pending.reset();
            m_failedOperation.reset();
            m_retrySelectionFilename.clear();
            m_selectedIncarnationId.clear();
            m_selectedAgent.clear();
            m_workingDirectory.clear();
            clearRows();
            clearSelection();
            setState(
                State::Waiting,
                {},
                QStringLiteral(
                    "Waiting for the current Claude session identity."));
        }
    }
    attemptEligibility();
}

void AgentMemoryModel::attemptEligibility()
{
    if (!m_demanded || m_pending) {
        return;
    }
    if (!m_hasAccountContext) {
        setState(
            State::Waiting,
            {},
            QStringLiteral("Waiting for an active account."));
        return;
    }
    const auto session =
        m_sessions.conversationContext(m_selectedSessionId);
    if (!session) {
        setState(
            State::Waiting,
            {},
            QStringLiteral("Waiting for the selected session."));
        return;
    }
    if (session->kind != QStringLiteral("local")) {
        clearAuthority(false);
        setState(
            State::Ineligible,
            {},
            QStringLiteral(
                "Project Memory is available only for local sessions."));
        return;
    }
    if (session->incarnationId.isEmpty()) {
        setState(
            State::Waiting,
            {},
            QStringLiteral(
                "Waiting for the current session incarnation."));
        return;
    }
    const auto identity = m_sessionIntel.projectMemoryContext(
        m_selectedSessionId,
        session->incarnationId);
    if (!identity) {
        if (!m_selectedIncarnationId.isEmpty()) {
            m_replyTimer.stop();
            m_pending.reset();
            m_failedOperation.reset();
            m_selectedIncarnationId.clear();
            m_selectedAgent.clear();
            m_workingDirectory.clear();
            clearRows();
            clearSelection();
        }
        if (m_sessionIntel.hydrationState()
            == AgentSessionIntelModel::HydrationState::Failed) {
            setState(
                State::Failed,
                QStringLiteral(
                    "Live agent identity is unavailable. Try again."),
                {});
        } else if (m_sessionIntel.hydrationState()
            == AgentSessionIntelModel::HydrationState::Loading) {
            setState(
                State::Waiting,
                {},
                QStringLiteral(
                    "Waiting for the current Claude session identity."));
        } else {
            setState(
                State::Ineligible,
                {},
                QStringLiteral(
                    "Project Memory is available when Claude is active in this session."));
        }
        return;
    }
    if (identity->agent != QStringLiteral("claude")) {
        clearAuthority(false);
        setState(
            State::Ineligible,
            {},
            QStringLiteral(
                "Project Memory is available only for Claude sessions."));
        return;
    }
    if (!validWorkingDirectory(identity->workingDirectory)
        || identity->workingDirectory != session->workingDirectory) {
        clearAuthority(false);
        setState(
            State::Ineligible,
            {},
            QStringLiteral(
                "The current Claude session has no verified project directory."));
        return;
    }
    const auto sameIdentity =
        m_selectedIncarnationId == session->incarnationId
        && m_selectedAgent == identity->agent
        && m_workingDirectory == identity->workingDirectory;
    if (sameIdentity
        && (m_state == State::Ready || m_state == State::Failed
            || m_state == State::Loading)) {
        return;
    }
    if (!sameIdentity) {
        m_replyTimer.stop();
        m_pending.reset();
        m_failedOperation.reset();
        clearRows();
        clearSelection();
        m_selectedIncarnationId = session->incarnationId;
        m_selectedAgent = identity->agent;
        m_workingDirectory = identity->workingDirectory;
    }
    (void)beginList();
}

void AgentMemoryModel::invalidateDemand(QString message)
{
    ++m_demandGeneration;
    ++m_selectionGeneration;
    clearAuthority(true);
    setState(State::Ineligible, {}, std::move(message));
}

void AgentMemoryModel::clearAuthority(const bool clearDemand)
{
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation.reset();
    m_retrySelectionFilename.clear();
    m_selectedIncarnationId.clear();
    m_selectedAgent.clear();
    m_workingDirectory.clear();
    clearRows();
    clearSelection();
    if (clearDemand) {
        m_demanded = false;
        m_selectedSessionId.clear();
        setState(State::Dormant);
    }
}

void AgentMemoryModel::clearRows()
{
    if (m_items.isEmpty()) {
        return;
    }
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void AgentMemoryModel::clearSelection()
{
    if (m_selectedFilename.isEmpty() && m_content.isEmpty()
        && m_contentError.isEmpty() && !m_contentLoading
        && !m_contentLoaded) {
        return;
    }
    ++m_selectionGeneration;
    m_selectedFilename.clear();
    m_content.clear();
    m_contentLoaded = false;
    m_contentError.clear();
    m_contentLoading = false;
    emit selectionChanged();
}

void AgentMemoryModel::setState(
    const State state,
    QString error,
    QString statusMessage)
{
    if (m_state == state && m_error == error
        && m_statusMessage == statusMessage) {
        return;
    }
    m_state = state;
    m_error = std::move(error);
    m_statusMessage = std::move(statusMessage);
    emit stateChanged();
}

void AgentMemoryModel::setContentState(
    const bool loading,
    QString error)
{
    if (m_contentLoading == loading && m_contentError == error) {
        return;
    }
    m_contentLoading = loading;
    m_contentError = std::move(error);
    emit selectionChanged();
}

void AgentMemoryModel::setFailure(
    const Operation operation,
    QString message,
    const bool protocolFault)
{
    if (protocolFault) {
        emit decodeError(message);
    }
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation = operation;
    if (operation == Operation::Read) {
        m_retrySelectionFilename = m_selectedFilename;
        setContentState(false, std::move(message));
        setState(State::Ready);
    } else {
        setState(State::Failed, std::move(message));
    }
}

bool AgentMemoryModel::beginList()
{
    if (!m_demanded || m_pending
        || m_selectedIncarnationId.isEmpty()
        || !validWorkingDirectory(m_workingDirectory)) {
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.listClaudeMemoryBound")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("cwd"), m_workingDirectory},
    };
    setState(State::Loading);
    return sendCommand(
        std::move(command),
        Pending {
            .requestId = requestId,
            .operation = Operation::List,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .selectionGeneration = m_selectionGeneration,
            .sessionId = m_selectedSessionId,
            .sessionIncarnationId = m_selectedIncarnationId,
            .filename = {},
            .selectionToken = {},
        });
}

bool AgentMemoryModel::beginRead(const qsizetype index)
{
    if (m_pending || index < 0 || index >= m_items.size()) {
        return false;
    }
    auto& item = m_items[index];
    if (!item.tokenAvailable) {
        return false;
    }
    item.tokenAvailable = false;
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.readClaudeMemoryBound")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("selectionToken"), item.selectionToken},
    };
    setContentState(true);
    return sendCommand(
        std::move(command),
        Pending {
            .requestId = requestId,
            .operation = Operation::Read,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .selectionGeneration = m_selectionGeneration,
            .sessionId = m_selectedSessionId,
            .sessionIncarnationId = m_selectedIncarnationId,
            .filename = item.filename,
            .selectionToken = item.selectionToken,
        });
}

bool AgentMemoryModel::sendCommand(
    QJsonObject command,
    Pending pending)
{
    Q_ASSERT(!m_pending);
    m_pending = pending;
    m_failedOperation.reset();
    const auto json =
        QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::AgentIntel, json)) {
        m_replyTimer.start(static_cast<int>(m_replyTimeoutMs));
        return true;
    }
    if (m_pending && m_pending->requestId == pending.requestId) {
        setFailure(
            pending.operation,
            pending.operation == Operation::List
                ? QStringLiteral(
                    "Couldn't request Project Memory. Try again.")
                : QStringLiteral(
                    "Couldn't request the selected memory file. Refresh and try again."),
            false);
    }
    return false;
}

bool AgentMemoryModel::pendingIsCurrent(
    const Pending& pending) const
{
    if (!m_demanded
        || pending.runtimeGeneration != m_runtimeGeneration
        || pending.demandGeneration != m_demandGeneration
        || pending.selectionGeneration != m_selectionGeneration
        || pending.sessionId != m_selectedSessionId
        || pending.sessionIncarnationId != m_selectedIncarnationId) {
        return false;
    }
    const auto session =
        m_sessions.conversationContext(pending.sessionId);
    const auto identity = session
        ? m_sessionIntel.projectMemoryContext(
              pending.sessionId,
              pending.sessionIncarnationId)
        : std::nullopt;
    return session && identity
        && session->kind == QStringLiteral("local")
        && session->incarnationId == pending.sessionIncarnationId
        && session->workingDirectory == m_workingDirectory
        && identity->agent == m_selectedAgent
        && identity->workingDirectory == m_workingDirectory;
}

bool AgentMemoryModel::decodeListReply(
    const QJsonObject& payload,
    QVector<Item>& items) const
{
    if (!exactFields(
            payload,
            {QStringLiteral("canonicalCwd"),
             QStringLiteral("projectSlug"),
             QStringLiteral("items")})) {
        return false;
    }
    const auto canonicalCwd =
        requiredString(payload, QStringLiteral("canonicalCwd"));
    const auto projectSlug =
        requiredString(payload, QStringLiteral("projectSlug"));
    const auto itemsValue = payload.value(QStringLiteral("items"));
    if (!canonicalCwd || !validWorkingDirectory(*canonicalCwd)
        || !projectSlug || !validProjectSlug(*projectSlug)
        || !itemsValue.isArray()
        || itemsValue.toArray().size() > maximumItems) {
        return false;
    }
    QSet<QString> tokens;
    QSet<QString> filenames;
    for (const auto& value : itemsValue.toArray()) {
        if (!value.isObject()) {
            return false;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("selectionToken"),
                 QStringLiteral("filename"),
                 QStringLiteral("memoryType")})) {
            return false;
        }
        const auto token =
            requiredString(object, QStringLiteral("selectionToken"), 36);
        const auto filename =
            requiredString(object, QStringLiteral("filename"), filenameLimit);
        std::optional<QString> memoryType;
        if (!token || !canonicalUuidV7(*token)
            || !filename || !validFilename(*filename)
            || !nullableString(
                object,
                QStringLiteral("memoryType"),
                memoryType,
                memoryTypeLimit,
                true)
            || tokens.contains(*token)
            || filenames.contains(*filename)) {
            return false;
        }
        tokens.insert(*token);
        filenames.insert(*filename);
        items.push_back({
            .filename = *filename,
            .memoryType = memoryType,
            .description = describe(memoryType),
            .selectionToken = *token,
            .canonicalCwd = *canonicalCwd,
            .projectSlug = *projectSlug,
            .tokenAvailable = true,
        });
    }
    return true;
}

bool AgentMemoryModel::decodeReadReply(
    const QJsonObject& payload,
    const Pending& pending,
    QString& content) const
{
    if (!exactFields(
            payload,
            {QStringLiteral("content"),
             QStringLiteral("selectionToken"),
             QStringLiteral("canonicalCwd"),
             QStringLiteral("projectSlug"),
             QStringLiteral("filename"),
             QStringLiteral("memoryType")})) {
        return false;
    }
    const auto found =
        std::ranges::find(m_items, pending.filename, &Item::filename);
    if (found == m_items.end()
        || found->selectionToken != pending.selectionToken) {
        return false;
    }
    const auto decodedContent =
        requiredString(
            payload,
            QStringLiteral("content"),
            maximumContentBytes,
            true);
    const auto selectionToken =
        requiredString(payload, QStringLiteral("selectionToken"), 36);
    const auto canonicalCwd =
        requiredString(payload, QStringLiteral("canonicalCwd"));
    const auto projectSlug =
        requiredString(payload, QStringLiteral("projectSlug"));
    const auto filename =
        requiredString(payload, QStringLiteral("filename"), filenameLimit);
    std::optional<QString> memoryType;
    if (!decodedContent || !selectionToken || !canonicalCwd
        || !projectSlug || !filename
        || !nullableString(
            payload,
            QStringLiteral("memoryType"),
            memoryType,
            memoryTypeLimit,
            true)
        || *selectionToken != found->selectionToken
        || *canonicalCwd != found->canonicalCwd
        || *projectSlug != found->projectSlug
        || *filename != found->filename
        || memoryType != found->memoryType) {
        return false;
    }
    content = *decodedContent;
    return true;
}

void AgentMemoryModel::applyList(QVector<Item> items)
{
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation.reset();
    const auto countChangedValue = m_items.size() != items.size();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }
    setState(
        State::Ready,
        {},
        m_items.isEmpty()
            ? QStringLiteral(
                "No Project Memory files are available for this session.")
            : QString {});

    const auto retryFilename =
        std::exchange(m_retrySelectionFilename, {});
    if (retryFilename.isEmpty()) {
        if (!m_selectedFilename.isEmpty()
            && std::ranges::find(
                   m_items,
                   m_selectedFilename,
                   &Item::filename)
                == m_items.end()) {
            clearSelection();
        }
        return;
    }
    const auto found =
        std::ranges::find(m_items, retryFilename, &Item::filename);
    if (found == m_items.end()) {
        m_selectedFilename = retryFilename;
        setContentState(
            false,
            QStringLiteral(
                "The selected memory file is no longer available."));
        emit selectionChanged();
        return;
    }
    ++m_selectionGeneration;
    m_selectedFilename = retryFilename;
    m_content.clear();
    m_contentLoaded = false;
    setContentState(false);
    emit selectionChanged();
    (void)beginRead(std::distance(m_items.begin(), found));
}

QString AgentMemoryModel::describe(
    const std::optional<QString>& memoryType)
{
    if (!memoryType) {
        return QStringLiteral("Project memory");
    }
    if (*memoryType == QStringLiteral("feedback")) {
        return QStringLiteral("Feedback memory");
    }
    if (*memoryType == QStringLiteral("project")) {
        return QStringLiteral("Project memory");
    }
    if (*memoryType == QStringLiteral("user")) {
        return QStringLiteral("User memory");
    }
    return memoryType->isEmpty()
        ? QStringLiteral("Unclassified memory")
        : QStringLiteral("Claude memory");
}

} // namespace kodosi
