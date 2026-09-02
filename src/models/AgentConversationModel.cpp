#include "models/AgentConversationModel.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include <cmath>
#include <limits>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype identifierLimit = 4'096;
constexpr qsizetype noticeLimit = 4'096;
constexpr quint64 maximumExactJsonInteger = 9'007'199'254'740'991ULL;

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
    QString& destination,
    const qsizetype maximum,
    const bool allowEmpty)
{
    if (!object.contains(key)) {
        return false;
    }
    const auto value = object.value(key);
    if (value.isNull()) {
        destination.clear();
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
    if (!boundedText(text, 1'024, false)) {
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
    QSet<QString> actual(keys.begin(), keys.end());
    QSet<QString> expected(fields.begin(), fields.end());
    return actual == expected;
}

bool canonicalUuidV7(const QString& value)
{
    const QUuid id(value);
    return !id.isNull() && id.version() == QUuid::UnixEpoch
        && id.toString(QUuid::WithoutBraces) == value;
}

bool canonicalUuid(const QString& value)
{
    const QUuid id(value);
    return !id.isNull()
        && id.toString(QUuid::WithoutBraces) == value;
}

std::optional<quint64> exactUnsigned(const QJsonValue& value)
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

bool validWorkingDirectory(const QString& path)
{
    return boundedText(path, identifierLimit, false)
        && QDir::isAbsolutePath(path)
        && QDir::cleanPath(path) == path;
}

bool validTranscriptPath(
    const QString& path,
    const QString& nativeSessionId)
{
    if (!boundedText(path, identifierLimit, false)
        || !QDir::isAbsolutePath(path)
        || QDir::cleanPath(path) != path) {
        return false;
    }
    const QFileInfo info(path);
    return info.suffix().compare(
               QStringLiteral("jsonl"),
               Qt::CaseInsensitive)
            == 0
        && info.completeBaseName() == nativeSessionId;
}

bool validTimestamp(const QString& timestamp)
{
    if (!boundedText(timestamp, 64, false)
        || !timestamp.contains(QLatin1Char('T'))) {
        return false;
    }
    auto parsed = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(timestamp, Qt::ISODate);
    }
    return parsed.isValid() && parsed.timeSpec() != Qt::LocalTime;
}

} // namespace

AgentConversationModel::AgentConversationModel(
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
    const auto sessionChanged = [this] { sessionAuthorityChanged(); };
    connect(&sessions, &QAbstractItemModel::modelReset, this, sessionChanged);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, sessionChanged);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, sessionChanged);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, sessionChanged);
    connect(
        &sessionIntel,
        &QAbstractItemModel::modelReset,
        this,
        sessionChanged);
    connect(
        &sessionIntel,
        &QAbstractItemModel::dataChanged,
        this,
        sessionChanged);
}

int AgentConversationModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant AgentConversationModel::data(
    const QModelIndex& index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case RoleRole: return entry.role;
    case ContentRole: return entry.content;
    case ToolNameRole: return entry.toolName;
    case TimestampRole: return entry.timestamp;
    default: return {};
    }
}

QHash<int, QByteArray> AgentConversationModel::roleNames() const
{
    return {
        {RoleRole, QByteArrayLiteral("role")},
        {ContentRole, QByteArrayLiteral("content")},
        {ToolNameRole, QByteArrayLiteral("toolName")},
        {TimestampRole, QByteArrayLiteral("timestamp")},
    };
}

bool AgentConversationModel::loading() const noexcept
{
    return m_loading;
}

QString AgentConversationModel::error() const
{
    return m_error;
}

QString AgentConversationModel::degradedWarning() const
{
    return m_degradedWarning;
}

bool AgentConversationModel::hasEarlier() const noexcept
{
    return m_nextBeforeByte.has_value();
}

bool AgentConversationModel::inspect(const QString& sessionId)
{
    if (sessionId == m_selectedSessionId
        && (m_pending || m_transcript || !m_error.isEmpty())) {
        return true;
    }
    clearAuthority(true);
    ++m_selectionGeneration;
    m_selectedSessionId = sessionId;
    if (!m_hasAccountContext) {
        setState(
            false,
            QStringLiteral("Conversation history requires an active account."),
            {});
        return false;
    }
    const auto context = m_sessions.conversationContext(sessionId);
    if (!context) {
        setState(
            false,
            QStringLiteral("The selected session is no longer available."),
            {});
        return false;
    }
    if (context->kind != QStringLiteral("local")) {
        setState(
            false,
            QStringLiteral("Conversation history is available only for local sessions."),
            {});
        return false;
    }
    if (context->incarnationId.isEmpty()) {
        setState(
            false,
            QStringLiteral("The selected local session has no current incarnation."),
            {});
        return false;
    }
    if (!validWorkingDirectory(context->workingDirectory)) {
        setState(
            false,
            QStringLiteral("The selected local session has no valid working directory."),
            {});
        return false;
    }
    const auto agent =
        m_sessionIntel.conversationAgent(sessionId, context->incarnationId);
    if (!agent) {
        setState(
            false,
            QStringLiteral("The selected session has no supported active agent transcript."),
            {});
        return false;
    }
    m_selectedIncarnationId = context->incarnationId;
    m_selectedAgent = *agent;
    m_workingDirectory = context->workingDirectory;
    return beginResolve();
}

void AgentConversationModel::close()
{
    ++m_selectionGeneration;
    clearAuthority(true);
}

bool AgentConversationModel::retry()
{
    if (m_selectedSessionId.isEmpty() || m_pending) {
        return false;
    }
    if (m_failedOperation == Operation::ReadEarlier
        && m_transcript && m_nextBeforeByte) {
        return beginRead(m_nextBeforeByte);
    }
    const auto selected = m_selectedSessionId;
    clearAuthority(true);
    return inspect(selected);
}

bool AgentConversationModel::loadEarlier()
{
    if (!m_transcript || !m_nextBeforeByte || m_pending) {
        return false;
    }
    return beginRead(m_nextBeforeByte);
}

void AgentConversationModel::ingestAuthEvent(QByteArray json)
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

void AgentConversationModel::ingestAgentIntelEvent(QByteArray json)
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
            QStringLiteral("Future conversation reply exceeds the ABI frame limit."));
    }
    if (admission == AccountEventAdmission::Current) {
        applyAgentIntelEvent(json);
    }
}

void AgentConversationModel::resetRuntimeAuthority()
{
    ++m_runtimeGeneration;
    ++m_selectionGeneration;
    m_accountFence.reset();
    m_hasAccountContext = false;
    clearAuthority(true);
}

void AgentConversationModel::activateAccount(
    QString userId,
    const quint64 epoch)
{
    auto activation = m_accountFence.activate({
        .userId = std::move(userId),
        .epoch = epoch,
    });
    if (!activation.accepted) {
        return;
    }
    m_hasAccountContext = true;
    if (activation.changed) {
        ++m_selectionGeneration;
        clearAuthority(true);
    }
    for (auto& pending : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(pending));
    }
}

void AgentConversationModel::applyAgentIntelEvent(const QByteArray& json)
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
        || !m_pending || m_pending->requestId != *requestId) {
        return;
    }
    if (!pendingIsCurrent(*m_pending)) {
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
            QStringLiteral("The runtime returned a malformed conversation reply."),
            true);
        return;
    }
    if (type == QStringLiteral("agent.intel.error")) {
        const auto message = requiredString(
            object,
            QStringLiteral("message"),
            noticeLimit);
        const auto publicMessage = [this] {
            switch (m_pending->operation) {
            case Operation::Resolve:
                return QStringLiteral(
                    "Couldn't resolve the current session transcript. Try again.");
            case Operation::ReadInitial:
                return QStringLiteral(
                    "Couldn't load conversation history. Try again.");
            case Operation::ReadEarlier:
                return QStringLiteral(
                    "Couldn't load earlier conversation entries. Try again.");
            }
            return QStringLiteral("Couldn't load conversation history. Try again.");
        }();
        setFailure(
            m_pending->operation,
            message ? publicMessage
                    : QStringLiteral(
                        "The runtime returned a malformed conversation error."),
            !message);
        return;
    }
    const auto payloadValue = object.value(QStringLiteral("payload"));
    if (!payloadValue.isObject()) {
        setFailure(
            m_pending->operation,
            QStringLiteral("The runtime returned a malformed conversation reply."),
            true);
        return;
    }

    const auto pending = *m_pending;
    if (pending.operation == Operation::Resolve) {
        TranscriptIdentity identity;
        if (!decodeResolveReply(payloadValue.toObject(), identity)) {
            setFailure(
                pending.operation,
                QStringLiteral("The runtime returned an invalid transcript identity."),
                true);
            return;
        }
        m_replyTimer.stop();
        m_pending.reset();
        m_transcript = std::move(identity);
        (void)beginRead(std::nullopt);
        return;
    }

    QVector<Entry> entries;
    std::optional<quint64> nextBeforeByte;
    quint64 sourceFileBytes = 0;
    QString degradedWarning;
    if (!decodePageReply(
            payloadValue.toObject(),
            pending,
            entries,
            nextBeforeByte,
            sourceFileBytes,
            degradedWarning)) {
        setFailure(
            pending.operation,
            QStringLiteral("The runtime returned a malformed conversation page."),
            true);
        return;
    }
    applyPage(
        pending,
        std::move(entries),
        nextBeforeByte,
        sourceFileBytes,
        std::move(degradedWarning));
}

void AgentConversationModel::sessionAuthorityChanged()
{
    if (m_selectedSessionId.isEmpty()) {
        return;
    }
    const auto context =
        m_sessions.conversationContext(m_selectedSessionId);
    const auto agent = context
        ? m_sessionIntel.conversationAgent(
              m_selectedSessionId,
              context->incarnationId)
        : std::nullopt;
    if (!context || context->kind != QStringLiteral("local")
        || context->incarnationId != m_selectedIncarnationId
        || context->workingDirectory != m_workingDirectory
        || !agent || *agent != m_selectedAgent) {
        close();
    }
}

void AgentConversationModel::clearAuthority(const bool clearSelection)
{
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation.reset();
    m_transcript.reset();
    m_nextBeforeByte.reset();
    m_sourceFileBytes.reset();
    clearRows();
    if (clearSelection) {
        m_selectedSessionId.clear();
        m_selectedIncarnationId.clear();
        m_selectedAgent.clear();
        m_workingDirectory.clear();
    }
    setState(false, {}, {});
}

void AgentConversationModel::clearRows()
{
    if (m_entries.isEmpty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
}

void AgentConversationModel::setFailure(
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
    if (operation != Operation::ReadEarlier) {
        m_transcript.reset();
        m_nextBeforeByte.reset();
        m_sourceFileBytes.reset();
        clearRows();
    }
    setState(false, std::move(message), m_degradedWarning);
}

void AgentConversationModel::setState(
    const bool loading,
    QString error,
    QString degradedWarning)
{
    if (m_loading == loading && m_error == error
        && m_degradedWarning == degradedWarning) {
        return;
    }
    m_loading = loading;
    m_error = std::move(error);
    m_degradedWarning = std::move(degradedWarning);
    emit stateChanged();
}

bool AgentConversationModel::beginResolve()
{
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.resolveActiveSession")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("cwd"), m_workingDirectory},
        {QStringLiteral("sessionId"), m_selectedSessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"),
         m_selectedIncarnationId},
    };
    return sendCommand(
        std::move(command),
        Pending {
            .requestId = requestId,
            .operation = Operation::Resolve,
            .runtimeGeneration = m_runtimeGeneration,
            .selectionGeneration = m_selectionGeneration,
            .sessionId = m_selectedSessionId,
            .sessionIncarnationId = m_selectedIncarnationId,
            .beforeByte = std::nullopt,
        });
}

bool AgentConversationModel::beginRead(
    const std::optional<quint64> beforeByte)
{
    if (!m_transcript) {
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.readSessionConversation")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("agent"), m_transcript->agent},
        {QStringLiteral("cwd"), m_transcript->workingDirectory},
        {QStringLiteral("sessionId"), m_transcript->nativeSessionId},
        {QStringLiteral("maxRecords"),
         static_cast<qint64>(maximumPageRecords)},
        {QStringLiteral("maxBytes"),
         static_cast<qint64>(maximumPageBytes)},
    };
    if (beforeByte) {
        command.insert(
            QStringLiteral("beforeByte"),
            static_cast<double>(*beforeByte));
    }
    return sendCommand(
        std::move(command),
        Pending {
            .requestId = requestId,
            .operation = beforeByte
                ? Operation::ReadEarlier
                : Operation::ReadInitial,
            .runtimeGeneration = m_runtimeGeneration,
            .selectionGeneration = m_selectionGeneration,
            .sessionId = m_selectedSessionId,
            .sessionIncarnationId = m_selectedIncarnationId,
            .beforeByte = beforeByte,
        });
}

bool AgentConversationModel::sendCommand(
    QJsonObject command,
    Pending pending)
{
    Q_ASSERT(!m_pending);
    m_pending = pending;
    m_failedOperation.reset();
    setState(true, {}, m_degradedWarning);
    const auto json =
        QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::AgentIntel, json)) {
        m_replyTimer.start(static_cast<int>(m_replyTimeoutMs));
        return true;
    }
    if (m_pending && m_pending->requestId == pending.requestId) {
        setFailure(
            pending.operation,
            QStringLiteral("Couldn't request conversation history. Try again."),
            false);
    }
    return false;
}

bool AgentConversationModel::pendingIsCurrent(
    const Pending& pending) const
{
    if (pending.runtimeGeneration != m_runtimeGeneration
        || pending.selectionGeneration != m_selectionGeneration
        || pending.sessionId != m_selectedSessionId
        || pending.sessionIncarnationId != m_selectedIncarnationId) {
        return false;
    }
    const auto context =
        m_sessions.conversationContext(pending.sessionId);
    return context && context->kind == QStringLiteral("local")
        && context->incarnationId == pending.sessionIncarnationId
        && context->workingDirectory == m_workingDirectory;
}

bool AgentConversationModel::decodeResolveReply(
    const QJsonObject& payload,
    TranscriptIdentity& identity) const
{
    if (!exactFields(
            payload,
            {QStringLiteral("runtimeSessionId"),
             QStringLiteral("runtimeIncarnationId"),
             QStringLiteral("nativeSessionId"),
             QStringLiteral("activeJsonl")})) {
        return false;
    }
    const auto runtimeSessionId =
        requiredString(payload, QStringLiteral("runtimeSessionId"));
    const auto runtimeIncarnationId =
        requiredString(payload, QStringLiteral("runtimeIncarnationId"));
    const auto nativeSessionId =
        requiredString(payload, QStringLiteral("nativeSessionId"));
    const auto transcriptPath =
        requiredString(payload, QStringLiteral("activeJsonl"));
    if (!runtimeSessionId || !runtimeIncarnationId || !nativeSessionId
        || !transcriptPath
        || *runtimeSessionId != m_selectedSessionId
        || *runtimeIncarnationId != m_selectedIncarnationId
        || !canonicalUuidV7(*runtimeIncarnationId)
        || !canonicalUuid(*nativeSessionId)
        || !validTranscriptPath(*transcriptPath, *nativeSessionId)) {
        return false;
    }
    identity = {
        .agent = m_selectedAgent,
        .workingDirectory = m_workingDirectory,
        .nativeSessionId = *nativeSessionId,
    };
    return true;
}

bool AgentConversationModel::decodePageReply(
    const QJsonObject& payload,
    const Pending& pending,
    QVector<Entry>& entries,
    std::optional<quint64>& nextBeforeByte,
    quint64& sourceFileBytes,
    QString& degradedWarning) const
{
    if (!exactFields(
            payload,
            {QStringLiteral("entries"),
             QStringLiteral("nextBeforeByte"),
             QStringLiteral("sourceFileBytes"),
             QStringLiteral("readBytes"),
             QStringLiteral("sourceRecords"),
             QStringLiteral("degradedReason")})) {
        return false;
    }
    const auto entriesValue = payload.value(QStringLiteral("entries"));
    const auto sourceBytes =
        exactUnsigned(payload.value(QStringLiteral("sourceFileBytes")));
    const auto readBytes =
        exactUnsigned(payload.value(QStringLiteral("readBytes")));
    const auto sourceRecords =
        exactUnsigned(payload.value(QStringLiteral("sourceRecords")));
    if (!entriesValue.isArray() || !sourceBytes || !readBytes
        || !sourceRecords || *readBytes > maximumPageBytes
        || *readBytes > *sourceBytes
        || *sourceRecords > static_cast<quint64>(maximumPageRecords)
        || entriesValue.toArray().size() > maximumPageEntries
        || (*sourceRecords == 0 && !entriesValue.toArray().isEmpty())
        || (*readBytes == 0 && *sourceRecords != 0)) {
        return false;
    }
    const auto boundary = pending.beforeByte.value_or(*sourceBytes);
    if (boundary > *sourceBytes || *readBytes > boundary) {
        return false;
    }
    if (pending.operation == Operation::ReadEarlier
        && m_sourceFileBytes && *sourceBytes < *m_sourceFileBytes) {
        return false;
    }
    const auto cursorValue = payload.value(QStringLiteral("nextBeforeByte"));
    if (cursorValue.isNull()) {
        nextBeforeByte.reset();
    } else {
        const auto cursor = exactUnsigned(cursorValue);
        if (!cursor || *cursor == 0 || *cursor >= boundary
            || *cursor > *sourceBytes) {
            return false;
        }
        nextBeforeByte = *cursor;
    }
    if (!nullableString(
            payload,
            QStringLiteral("degradedReason"),
            degradedWarning,
            noticeLimit,
            false)
        || (!degradedWarning.isEmpty() && !nextBeforeByte)) {
        return false;
    }

    quint64 decodedBytes = 0;
    for (const auto& value : entriesValue.toArray()) {
        if (!value.isObject()) {
            return false;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("role"),
                 QStringLiteral("content"),
                 QStringLiteral("toolName"),
                 QStringLiteral("timestamp")})) {
            return false;
        }
        const auto role =
            requiredString(object, QStringLiteral("role"), 16);
        const auto content = requiredString(
            object,
            QStringLiteral("content"),
            static_cast<qsizetype>(maximumPageBytes),
            true);
        QString toolName;
        QString timestamp;
        if (!role || !content
            || (*role != QStringLiteral("user")
                && *role != QStringLiteral("assistant")
                && *role != QStringLiteral("tool_use"))
            || !nullableString(
                object,
                QStringLiteral("toolName"),
                toolName,
                1'024,
                false)
            || !nullableString(
                object,
                QStringLiteral("timestamp"),
                timestamp,
                64,
                false)
            || (!timestamp.isEmpty() && !validTimestamp(timestamp))
            || (*role == QStringLiteral("tool_use")
                ? toolName.isEmpty()
                : !toolName.isEmpty())
            || ((*role == QStringLiteral("user")
                    || *role == QStringLiteral("assistant"))
                && content->isEmpty())) {
            return false;
        }
        decodedBytes += static_cast<quint64>(content->toUtf8().size());
        decodedBytes += static_cast<quint64>(toolName.toUtf8().size());
        decodedBytes += static_cast<quint64>(timestamp.toUtf8().size());
        if (decodedBytes > maximumPageBytes) {
            return false;
        }
        Entry entry {
            .role = *role,
            .content = *content,
            .toolName = std::move(toolName),
            .timestamp = std::move(timestamp),
        };
        entries.push_back(std::move(entry));
    }
    sourceFileBytes = *sourceBytes;
    return true;
}

void AgentConversationModel::applyPage(
    const Pending& pending,
    QVector<Entry> entries,
    const std::optional<quint64> nextBeforeByte,
    const quint64 sourceFileBytes,
    QString degradedWarning)
{
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation.reset();
    if (pending.operation == Operation::ReadInitial) {
        const auto countChangedValue = m_entries.size() != entries.size();
        beginResetModel();
        m_entries = std::move(entries);
        endResetModel();
        if (countChangedValue) {
            emit countChanged();
        }
    } else if (!entries.isEmpty()) {
        beginInsertRows({}, 0, entries.size() - 1);
        entries += m_entries;
        m_entries = std::move(entries);
        endInsertRows();
        emit countChanged();
    }
    m_nextBeforeByte = nextBeforeByte;
    m_sourceFileBytes = sourceFileBytes;
    setState(false, {}, std::move(degradedWarning));
}

} // namespace kodosi
