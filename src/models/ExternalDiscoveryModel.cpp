#include "models/ExternalDiscoveryModel.hpp"

#include "platform/DesktopFileIntegration.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype maximumItems = 512;
constexpr qsizetype maximumStringBytes = 4'096;
constexpr qsizetype maximumDiscoveryBytes = 512 * 1024;
constexpr qsizetype maximumReleaseRequests = 32;
constexpr quint64 maximumExactJsonInteger = 9'007'199'254'740'991ULL;

QString translated(const char* value)
{
    return QCoreApplication::translate("ExternalDiscoveryModel", value);
}

bool canonicalUuidV7(const QString& value)
{
    const QUuid id(value);
    return !id.isNull() && id.version() == QUuid::UnixEpoch
        && id.toString(QUuid::WithoutBraces) == value;
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

bool bounded(const QString& value, const qsizetype maximum, const bool empty = false)
{
    return (empty || !value.isEmpty()) && !value.contains(QChar::Null)
        && value.toUtf8().size() <= maximum;
}

bool nullableString(
    const QJsonObject& object,
    const QString& key,
    QString& output,
    const qsizetype maximum)
{
    if (!object.contains(key)) {
        return false;
    }
    const auto value = object.value(key);
    if (value.isNull()) {
        output.clear();
        return true;
    }
    if (!value.isString() || !bounded(value.toString(), maximum, true)) {
        return false;
    }
    output = value.toString();
    return true;
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
    if (!value.isString() || !bounded(value.toString(), 1'024)) {
        return false;
    }
    output = value.toString();
    return true;
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

bool absoluteNativePath(const QString& path)
{
    return bounded(path, maximumStringBytes)
        && QDir::isAbsolutePath(path)
        && QDir::cleanPath(path) == path;
}

} // namespace

ExternalDiscoveryModel::ExternalDiscoveryModel(
    CommandDispatcher& dispatcher,
    DesktopFileIntegration& desktopFiles,
    const qint64 replyTimeoutMs,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_desktopFiles(desktopFiles)
    , m_servers(this)
    , m_sessions(this)
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
        if (pending.operation == Operation::Open) {
            if (m_cancelledOpenRequests.size() >= maximumReleaseRequests) {
                m_cancelledOpenRequests.erase(
                    m_cancelledOpenRequests.begin());
            }
            m_cancelledOpenRequests.insert(
                pending.requestId,
                pending.itemIdentity);
        }
        if (pending.operation == Operation::Discover) {
            m_state = State::Failed;
            m_error = translated("The runtime did not reply in time.");
            emit changed();
        } else {
            refreshAfterConsumedAction(
                translated("The source action did not reply in time."),
                true);
        }
    });
}

ExternalDiscoveryModel::~ExternalDiscoveryModel()
{
    releaseOwnedHandoffsBestEffort();
}

ExternalDiscoveryModel::State ExternalDiscoveryModel::state() const noexcept
{
    return m_state;
}
QString ExternalDiscoveryModel::error() const { return m_error; }
bool ExternalDiscoveryModel::loading() const noexcept { return m_state == State::Loading; }
quint64 ExternalDiscoveryModel::capabilityRevision() const noexcept
{
    return m_capabilityRevision;
}
PresentationListModel* ExternalDiscoveryModel::servers() noexcept { return &m_servers; }
PresentationListModel* ExternalDiscoveryModel::sessions() noexcept { return &m_sessions; }

bool ExternalDiscoveryModel::refresh(const bool force)
{
    const auto wasDemanded = m_demanded;
    m_demanded = true;
    if (!m_hasAccountContext) {
        return false;
    }
    if (m_pending) {
        if (!force) {
            return true;
        }
        cancelPending();
    }
    if (!force && wasDemanded && m_state == State::Ready) {
        return true;
    }
    if (force) {
        ++m_demandGeneration;
    }
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_state = State::Loading;
    m_error.clear();
    emit changed();
    return send(
        {
            {QStringLiteral("type"),
             QStringLiteral("agent.intel.discoverExternalBound")},
            {QStringLiteral("requestId"), requestId},
        },
        {
            .requestId = requestId,
            .operation = Operation::Discover,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
        });
}

bool ExternalDiscoveryModel::copySourcePath(const QString& itemId)
{
    return action(itemId, Operation::CopyPath);
}

bool ExternalDiscoveryModel::openSource(const QString& itemId)
{
    return action(itemId, Operation::Open);
}

bool ExternalDiscoveryModel::revealSource(const QString& itemId)
{
    return action(itemId, Operation::Reveal);
}

bool ExternalDiscoveryModel::canCopySourcePath(const QString& itemId) const
{
    const auto* item = itemForId(itemId);
    return item != nullptr && !item->token.isEmpty() && item->canCopy;
}

bool ExternalDiscoveryModel::canOpenSource(const QString& itemId) const
{
    const auto* item = itemForId(itemId);
    return item != nullptr && !item->token.isEmpty() && item->canOpen;
}

bool ExternalDiscoveryModel::canRevealSource(const QString& itemId) const
{
    const auto* item = itemForId(itemId);
    return item != nullptr && !item->token.isEmpty() && item->canReveal;
}

void ExternalDiscoveryModel::close()
{
    releaseOwnedHandoffsBestEffort();
    m_demanded = false;
    ++m_demandGeneration;
    cancelPending();
    m_replyTimer.stop();
    m_items.clear();
    m_servers.replace({});
    m_sessions.replace({});
    m_state = State::Dormant;
    m_error.clear();
    m_syntheticFixture = false;
    ++m_capabilityRevision;
    emit changed();
}

void ExternalDiscoveryModel::installSyntheticFixture()
{
    ++m_demandGeneration;
    m_pending.reset();
    m_replyTimer.stop();
    const auto mcpId = stableId(QStringLiteral("synthetic|external|mcp"));
    const auto sessionId = stableId(QStringLiteral("synthetic|external|session"));
    const auto mcpToken =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto sessionToken =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_items = {
        {
            .id = mcpId,
            .token = mcpToken,
            .title = QStringLiteral("filesystem"),
            .subtitle = QStringLiteral("Claude Desktop"),
            .kind = QStringLiteral("mcp"),
            .metadata = QStringLiteral("stdio"),
            .canCopy = true,
            .canOpen = true,
            .canReveal = true,
            .server = true,
        },
        {
            .id = sessionId,
            .token = sessionToken,
            .title = QStringLiteral("External release review"),
            .subtitle = QStringLiteral("Kodosi"),
            .kind = QStringLiteral("session"),
            .agent = QStringLiteral("claude"),
            .status = QStringLiteral("alive"),
            .metadata = QStringLiteral("PID 4242 · started moments ago"),
            .canCopy = true,
            .canOpen = true,
            .canReveal = true,
        },
    };
    m_servers.replace({
        {
            .itemId = mcpId,
            .title = QStringLiteral("filesystem"),
            .subtitle = QStringLiteral("Claude Desktop"),
            .kind = QStringLiteral("mcp"),
            .metadata = QStringLiteral("stdio"),
            .available = true,
        },
    });
    m_sessions.replace({
        {
            .itemId = sessionId,
            .title = QStringLiteral("External release review"),
            .subtitle = QStringLiteral("Kodosi"),
            .kind = QStringLiteral("session"),
            .agent = QStringLiteral("claude"),
            .status = QStringLiteral("alive"),
            .metadata = QStringLiteral("PID 4242 · started moments ago"),
            .available = true,
        },
    });
    m_state = State::Ready;
    m_error.clear();
    m_syntheticFixture = true;
    ++m_capabilityRevision;
    emit changed();
}

void ExternalDiscoveryModel::ingestAuthEvent(QByteArray json)
{
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
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
        emit decodeError(translated("Authentication context is malformed."));
        return;
    }
    activateAccount(std::move(userId), *epoch);
}

void ExternalDiscoveryModel::ingestAgentIntelEvent(QByteArray json)
{
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
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
        emit decodeError(translated("External discovery authority is malformed."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {.userId = std::move(userId), .epoch = *epoch},
        json);
    if (admission == AccountEventAdmission::Current) {
        applyAgentIntelEvent(json);
    }
}

void ExternalDiscoveryModel::resetRuntimeAuthority()
{
    releaseOwnedHandoffsBestEffort();
    ++m_runtimeGeneration;
    ++m_demandGeneration;
    m_accountFence.reset();
    m_hasAccountContext = false;
    cancelPending();
    m_replyTimer.stop();
    m_items.clear();
    m_releaseRequests.clear();
    m_ownedHandoffs.clear();
    m_cancelledOpenRequests.clear();
    m_servers.replace({});
    m_sessions.replace({});
    m_state = State::Dormant;
    m_error.clear();
    m_syntheticFixture = false;
    ++m_capabilityRevision;
    emit changed();
}

bool ExternalDiscoveryModel::action(
    const QString& itemId,
    const Operation operation)
{
    auto* item = itemForId(itemId);
    if (item == nullptr || item->token.isEmpty() || m_pending
        || (operation == Operation::CopyPath && !item->canCopy)
        || (operation == Operation::Open && !item->canOpen)
        || (operation == Operation::Reveal && !item->canReveal)) {
        return false;
    }
    if (m_syntheticFixture) {
        item->token.clear();
        ++m_capabilityRevision;
        emit changed();
        emit actionMessage(
            translated("Synthetic source action completed."),
            false);
        return true;
    }
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto token = std::exchange(item->token, {});
    ++m_capabilityRevision;
    QString actionName;
    switch (operation) {
    case Operation::CopyPath:
        actionName = QStringLiteral("copyPath");
        break;
    case Operation::Open:
        actionName = QStringLiteral("open");
        break;
    case Operation::Reveal:
        actionName = QStringLiteral("reveal");
        break;
    case Operation::Discover:
        return false;
    }
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.externalSourceActionBound")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("selectionToken"), token},
        {QStringLiteral("action"), actionName},
    };
    const auto accepted = send(
        std::move(command),
        {
            .requestId = requestId,
            .itemId = itemId,
            .operation = operation,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .consumedToken = token,
            .itemIdentity = item->identity,
        });
    if (!accepted) {
        const auto message = m_error.isEmpty()
            ? translated("The source action could not be sent.")
            : m_error;
        refreshAfterConsumedAction(message, true);
    } else {
        emit changed();
    }
    return accepted;
}

bool ExternalDiscoveryModel::send(QJsonObject command, Pending pending)
{
    const auto result = m_dispatcher.send(
        CommandLane::AgentIntel,
        QJsonDocument(command).toJson(QJsonDocument::Compact));
    if (!result) {
        m_state = State::Failed;
        m_error = result.error().message;
        emit changed();
        return false;
    }
    m_pending = std::move(pending);
    m_replyTimer.start(static_cast<int>(m_replyTimeoutMs));
    return true;
}

void ExternalDiscoveryModel::cancelPending()
{
    if (m_pending && m_pending->operation == Operation::Open) {
        if (m_cancelledOpenRequests.size() >= maximumReleaseRequests) {
            m_cancelledOpenRequests.erase(m_cancelledOpenRequests.begin());
        }
        m_cancelledOpenRequests.insert(
            m_pending->requestId,
            m_pending->itemIdentity);
    }
    m_pending.reset();
    m_replyTimer.stop();
}

void ExternalDiscoveryModel::refreshAfterConsumedAction(
    QString message,
    const bool error)
{
    emit actionMessage(std::move(message), error);
    if (m_demanded && m_hasAccountContext && !m_pending) {
        (void)refresh(true);
    }
}

void ExternalDiscoveryModel::activateAccount(QString userId, const quint64 epoch)
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
        ++m_demandGeneration;
        cancelPending();
        m_replyTimer.stop();
        m_items.clear();
        m_releaseRequests.clear();
        m_ownedHandoffs.clear();
        m_cancelledOpenRequests.clear();
        m_servers.replace({});
        m_sessions.replace({});
        m_state = State::Dormant;
        m_error.clear();
        m_syntheticFixture = false;
        ++m_capabilityRevision;
        emit changed();
    }
    for (auto& event : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(event));
    }
    if (m_demanded && !m_pending && m_state != State::Ready) {
        (void)refresh(false);
    }
}

void ExternalDiscoveryModel::applyAgentIntelEvent(const QByteArray& json)
{
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto requestId = object.value(QStringLiteral("requestId")).toString();
    if (!canonicalUuidV7(requestId)) {
        return;
    }
    if (handleReleaseEvent(object) || handleCancelledOpenEvent(object)) {
        return;
    }
    if (!m_pending || requestId != m_pending->requestId
        || !pendingCurrent(*m_pending)) {
        return;
    }
    const auto pending = *m_pending;
    m_pending.reset();
    m_replyTimer.stop();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("agent.intel.error")) {
        applyError(object, pending);
        return;
    }
    if (type != QStringLiteral("agent.intel.reply")
        || !exactAccountEnvelope(object, {QStringLiteral("payload")})
        || !object.value(QStringLiteral("payload")).isObject()) {
        if (pending.operation == Operation::Discover) {
            m_state = State::Failed;
            m_error = translated("External discovery reply was malformed.");
            emit decodeError(m_error);
            emit changed();
        } else {
            refreshAfterConsumedAction(
                translated("The external source action reply was malformed."),
                true);
        }
        return;
    }
    const auto payload = object.value(QStringLiteral("payload")).toObject();
    if (pending.operation == Operation::Discover) {
        applyDiscovery(payload);
    } else {
        applyAction(payload, pending);
    }
}

void ExternalDiscoveryModel::applyDiscovery(const QJsonObject& payload)
{
    if (!exactFields(
            payload,
            {QStringLiteral("mcpServers"),
             QStringLiteral("sessions")})
        || !payload.value(QStringLiteral("mcpServers")).isArray()
        || !payload.value(QStringLiteral("sessions")).isArray()
        || QJsonDocument(payload).toJson(QJsonDocument::Compact).size()
            > maximumDiscoveryBytes) {
        m_state = State::Failed;
        m_error = translated("External discovery reply was malformed.");
        emit decodeError(m_error);
        emit changed();
        return;
    }
    const auto servers = payload.value(QStringLiteral("mcpServers")).toArray();
    const auto sessions = payload.value(QStringLiteral("sessions")).toArray();
    if (servers.size() + sessions.size() > maximumItems) {
        m_state = State::Failed;
        m_error = translated("External discovery exceeded its item bound.");
        emit changed();
        return;
    }
    QVector<Item> items;
    QVector<PresentationListModel::Row> serverRows;
    QVector<PresentationListModel::Row> sessionRows;
    QSet<QString> selectionTokens;
    QHash<QString, qsizetype> identityOccurrences;
    for (const auto& value : servers) {
        if (!value.isObject()) {
            m_state = State::Failed;
            m_error = translated("External MCP discovery was malformed.");
            emit decodeError(m_error);
            emit changed();
            return;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("selectionToken"),
                 QStringLiteral("source"),
                 QStringLiteral("serverName"),
                 QStringLiteral("transport"),
                 QStringLiteral("canCopySourcePath"),
                 QStringLiteral("canOpenSource"),
                 QStringLiteral("canRevealSource")})) {
            m_state = State::Failed;
            m_error = translated("External MCP discovery was malformed.");
            emit decodeError(m_error);
            emit changed();
            return;
        }
        const auto token = object.value(QStringLiteral("selectionToken")).toString();
        const auto source = object.value(QStringLiteral("source")).toString();
        const auto name = object.value(QStringLiteral("serverName")).toString();
        QString transport;
        if (!canonicalUuidV7(token) || !bounded(source, 256)
            || !bounded(name, 256)
            || !nullableString(object, QStringLiteral("transport"), transport, 128)
            || !object.value(QStringLiteral("canCopySourcePath")).isBool()
            || !object.value(QStringLiteral("canOpenSource")).isBool()
            || !object.value(QStringLiteral("canRevealSource")).isBool()
            || selectionTokens.contains(token)) {
            m_state = State::Failed;
            m_error = translated("External MCP discovery was malformed.");
            emit decodeError(m_error);
            emit changed();
            return;
        }
        selectionTokens.insert(token);
        const auto signature =
            QStringLiteral("mcp|") + source + QLatin1Char('|') + name;
        const auto occurrence = identityOccurrences[signature]++;
        const auto identity =
            signature + QLatin1Char('|') + QString::number(occurrence);
        const auto id = stableId(QStringLiteral("external|") + identity);
        const Item item {
            .id = id,
            .token = token,
            .identity = identity,
            .title = name,
            .subtitle = source,
            .kind = QStringLiteral("mcp"),
            .metadata = transport,
            .canCopy = object.value(QStringLiteral("canCopySourcePath")).toBool(),
            .canOpen = object.value(QStringLiteral("canOpenSource")).toBool(),
            .canReveal = object.value(QStringLiteral("canRevealSource")).toBool(),
            .server = true,
        };
        items.push_back(item);
        serverRows.push_back({
            .itemId = id,
            .title = name,
            .subtitle = source,
            .kind = QStringLiteral("mcp"),
            .metadata = transport,
            .available = item.canCopy || item.canOpen || item.canReveal,
        });
    }
    for (const auto& value : sessions) {
        if (!value.isObject()) {
            m_state = State::Failed;
            m_error = translated("External session discovery was malformed.");
            emit decodeError(m_error);
            emit changed();
            return;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("selectionToken"),
                 QStringLiteral("agent"),
                 QStringLiteral("sessionId"),
                 QStringLiteral("pid"),
                 QStringLiteral("liveness"),
                 QStringLiteral("workspaceLabel"),
                 QStringLiteral("name"),
                 QStringLiteral("startedAt"),
                 QStringLiteral("canCopySourcePath"),
                 QStringLiteral("canOpenSource"),
                 QStringLiteral("canRevealSource")})) {
            m_state = State::Failed;
            m_error = translated("External session discovery was malformed.");
            emit decodeError(m_error);
            emit changed();
            return;
        }
        const auto token = object.value(QStringLiteral("selectionToken")).toString();
        const auto agent = object.value(QStringLiteral("agent")).toString();
        const auto sessionId = object.value(QStringLiteral("sessionId")).toString();
        const auto liveness = object.value(QStringLiteral("liveness")).toString();
        QString workspace;
        QString name;
        QString started;
        if (!canonicalUuidV7(token) || !bounded(agent, 64)
            || !bounded(sessionId, 256) || !bounded(liveness, 64)
            || !nullableString(
                object,
                QStringLiteral("workspaceLabel"),
                workspace,
                512)
            || !nullableString(object, QStringLiteral("name"), name, 512)
            || !nullableString(object, QStringLiteral("startedAt"), started, 256)
            || !object.value(QStringLiteral("canCopySourcePath")).isBool()
            || !object.value(QStringLiteral("canOpenSource")).isBool()
            || !object.value(QStringLiteral("canRevealSource")).isBool()
            || selectionTokens.contains(token)) {
            m_state = State::Failed;
            m_error = translated("External session discovery was malformed.");
            emit decodeError(m_error);
            emit changed();
            return;
        }
        QString pid;
        const auto pidValue = object.value(QStringLiteral("pid"));
        if (!pidValue.isNull()) {
            const auto decodedPid = exactUnsigned(pidValue);
            if (!decodedPid
                || *decodedPid > std::numeric_limits<quint32>::max()) {
                m_state = State::Failed;
                m_error = translated("External session discovery was malformed.");
                emit decodeError(m_error);
                emit changed();
                return;
            }
            pid = translated("PID %1").arg(
                static_cast<quint32>(*decodedPid));
        }
        selectionTokens.insert(token);
        const auto signature =
            QStringLiteral("session|") + agent + QLatin1Char('|') + sessionId;
        const auto occurrence = identityOccurrences[signature]++;
        const auto identity =
            signature + QLatin1Char('|') + QString::number(occurrence);
        const auto id = stableId(QStringLiteral("external|") + identity);
        const auto display = name.isEmpty()
            ? translated("Unnamed external session")
            : name;
        QStringList metadata;
        if (!started.isEmpty()) {
            metadata.push_back(started);
        }
        if (!pid.isEmpty()) {
            metadata.push_back(pid);
        }
        const Item item {
            .id = id,
            .token = token,
            .identity = identity,
            .title = display,
            .subtitle = workspace,
            .kind = QStringLiteral("session"),
            .agent = agent,
            .status = liveness,
            .metadata = metadata.join(QStringLiteral(" · ")),
            .canCopy = object.value(QStringLiteral("canCopySourcePath")).toBool(),
            .canOpen = object.value(QStringLiteral("canOpenSource")).toBool(),
            .canReveal = object.value(QStringLiteral("canRevealSource")).toBool(),
        };
        items.push_back(item);
        sessionRows.push_back({
            .itemId = id,
            .title = display,
            .subtitle = workspace,
            .kind = QStringLiteral("session"),
            .agent = agent,
            .status = liveness,
            .metadata = item.metadata,
            .available = item.canCopy || item.canOpen || item.canReveal,
        });
    }
    m_items = std::move(items);
    m_servers.replace(std::move(serverRows));
    m_sessions.replace(std::move(sessionRows));
    m_state = State::Ready;
    m_error.clear();
    m_syntheticFixture = false;
    ++m_capabilityRevision;
    emit changed();
}

void ExternalDiscoveryModel::applyAction(
    const QJsonObject& payload,
    const Pending& pending)
{
    const auto returnedHandoffId =
        payload.value(QStringLiteral("handoffId")).toString();
    const auto releaseReturnedHandoff = [this, &returnedHandoffId] {
        if (canonicalUuidV7(returnedHandoffId)) {
            dispatchRelease(returnedHandoffId);
        }
    };
    const auto* item = itemForId(pending.itemId);
    if (item == nullptr || item->identity != pending.itemIdentity
        || !item->token.isEmpty()
        || !exactFields(
            payload,
            {QStringLiteral("action"),
             QStringLiteral("sourcePath"),
             QStringLiteral("handoffId"),
             QStringLiteral("handoffPath"),
             QStringLiteral("displayName")})) {
        releaseReturnedHandoff();
        refreshAfterConsumedAction(
            translated("The external source action reply was malformed."),
            true);
        return;
    }
    const auto action = payload.value(QStringLiteral("action")).toString();
    const auto displayName = payload.value(QStringLiteral("displayName"));
    const auto sourcePath = payload.value(QStringLiteral("sourcePath"));
    const auto handoffId = payload.value(QStringLiteral("handoffId"));
    const auto handoffPath = payload.value(QStringLiteral("handoffPath"));
    QString expectedAction;
    switch (pending.operation) {
    case Operation::CopyPath:
        expectedAction = QStringLiteral("copyPath");
        break;
    case Operation::Open:
        expectedAction = QStringLiteral("open");
        break;
    case Operation::Reveal:
        expectedAction = QStringLiteral("reveal");
        break;
    case Operation::Discover:
        break;
    }
    if (action != expectedAction || !displayName.isString()
        || !bounded(displayName.toString(), 512, true)) {
        releaseReturnedHandoff();
        refreshAfterConsumedAction(
            translated("The external source action reply was malformed."),
            true);
        return;
    }

    if (pending.operation == Operation::CopyPath
        || pending.operation == Operation::Reveal) {
        if (!sourcePath.isString()
            || !absoluteNativePath(sourcePath.toString())
            || !handoffId.isNull() || !handoffPath.isNull()) {
            releaseReturnedHandoff();
            refreshAfterConsumedAction(
                translated("The source path reply was malformed."),
                true);
            return;
        }
        if (pending.operation == Operation::CopyPath) {
            auto* clipboard = QGuiApplication::clipboard();
            if (clipboard == nullptr) {
                refreshAfterConsumedAction(
                    translated("The system clipboard is unavailable."),
                    true);
                return;
            }
            clipboard->setText(sourcePath.toString());
            refreshAfterConsumedAction(
                translated("Source path copied."),
                false);
            return;
        }
        const auto revealed = m_desktopFiles.revealBoundSource(
            sourcePath.toString(),
            QUuid::createUuidV7().toString(QUuid::WithoutBraces),
            DesktopFileIntegration::Purpose::ExternalSource);
        refreshAfterConsumedAction(
            revealed ? translated("Revealed the selected source.")
                     : m_desktopFiles.errorMessage(),
            !revealed);
        return;
    }

    if (!sourcePath.isNull() || !handoffId.isString()
        || !canonicalUuidV7(handoffId.toString())
        || !handoffPath.isString()
        || !absoluteNativePath(handoffPath.toString())) {
        releaseReturnedHandoff();
        refreshAfterConsumedAction(
            translated("The native open handoff was malformed."),
            true);
        return;
    }
    const auto opened = m_desktopFiles.openBoundHandoff(
        handoffPath.toString(),
        QUuid::createUuidV7().toString(QUuid::WithoutBraces),
        DesktopFileIntegration::Purpose::ExternalSource);
    dispatchRelease(handoffId.toString());
    refreshAfterConsumedAction(
        opened ? translated("Opened the selected source.")
               : m_desktopFiles.errorMessage(),
        !opened);
}

void ExternalDiscoveryModel::applyError(
    const QJsonObject& object,
    const Pending& pending)
{
    if (!exactAccountEnvelope(
            object,
            {QStringLiteral("message"),
             QStringLiteral("failureKind"),
             QStringLiteral("mutationId"),
             QStringLiteral("reconciliationRequired")})
        || !object.value(QStringLiteral("message")).isString()
        || !bounded(
            object.value(QStringLiteral("message")).toString(),
            8'192)
        || !object.value(QStringLiteral("failureKind")).isString()
        || (object.value(QStringLiteral("failureKind")).toString()
                != QStringLiteral("deterministic")
            && object.value(QStringLiteral("failureKind")).toString()
                != QStringLiteral("deliveryAmbiguous"))
        || (!object.value(QStringLiteral("mutationId")).isNull()
            && (!object.value(QStringLiteral("mutationId")).isString()
                || !canonicalUuidV7(
                    object.value(QStringLiteral("mutationId")).toString())))
        || !object.value(QStringLiteral("reconciliationRequired")).isBool()) {
        if (pending.operation == Operation::Discover) {
            m_state = State::Failed;
            m_error = translated("External discovery error reply was malformed.");
            emit decodeError(m_error);
            emit changed();
        } else {
            refreshAfterConsumedAction(
                translated("The external source error reply was malformed."),
                true);
        }
        return;
    }
    if (pending.operation == Operation::Discover) {
        m_state = State::Failed;
        m_error = object.value(QStringLiteral("message")).toString();
        emit changed();
        return;
    }
    refreshAfterConsumedAction(
        object.value(QStringLiteral("message")).toString(),
        true);
}

bool ExternalDiscoveryModel::handleReleaseEvent(const QJsonObject& object)
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
        && object.value(QStringLiteral("message")).isString()
        && bounded(
            object.value(QStringLiteral("message")).toString(),
            8'192)
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
    emit decodeError(translated("Open-handoff release reply was malformed."));
    return true;
}

bool ExternalDiscoveryModel::handleCancelledOpenEvent(
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
        payload.value(QStringLiteral("handoffId")).toString();
    if (canonicalUuidV7(handoffId)) {
        dispatchRelease(handoffId);
    }
    if (!exactAccountEnvelope(object, {QStringLiteral("payload")})) {
        emit decodeError(translated("Cancelled open-handoff reply was malformed."));
    }
    return true;
}

void ExternalDiscoveryModel::dispatchRelease(const QString& handoffId)
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

void ExternalDiscoveryModel::releaseOwnedHandoffsBestEffort()
{
    const auto handoffs = m_ownedHandoffs.values();
    for (const auto& handoffId : handoffs) {
        dispatchRelease(handoffId);
    }
}

QString ExternalDiscoveryModel::stableId(const QString& key)
{
    auto found = m_stableIds.find(key);
    if (found != m_stableIds.end()) {
        return found.value();
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_stableIds.insert(key, id);
    return id;
}

ExternalDiscoveryModel::Item* ExternalDiscoveryModel::itemForId(
    const QString& itemId)
{
    const auto found = std::find_if(
        m_items.begin(),
        m_items.end(),
        [&itemId](const Item& item) { return item.id == itemId; });
    return found == m_items.end() ? nullptr : &*found;
}

const ExternalDiscoveryModel::Item* ExternalDiscoveryModel::itemForId(
    const QString& itemId) const
{
    const auto found = std::find_if(
        m_items.cbegin(),
        m_items.cend(),
        [&itemId](const Item& item) { return item.id == itemId; });
    return found == m_items.cend() ? nullptr : &*found;
}

bool ExternalDiscoveryModel::pendingCurrent(const Pending& pending) const
{
    return pending.runtimeGeneration == m_runtimeGeneration
        && pending.demandGeneration == m_demandGeneration;
}

} // namespace kodosi
