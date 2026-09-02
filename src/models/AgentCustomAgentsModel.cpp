#include "models/AgentCustomAgentsModel.hpp"

#include <QDir>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype identifierLimit = 4'096;
constexpr qsizetype accountIdLimit = 1'024;
constexpr qsizetype summaryNameLimit = 256;
constexpr qsizetype summaryDescriptionLimit = 2 * 1024;
constexpr qsizetype summaryModelLimit = 256;
constexpr qsizetype summaryToolLimit = 128;
constexpr qsizetype detailTextLimit = 64 * 1024;
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

bool requiredStringList(
    const QJsonObject& object,
    const QString& key,
    QStringList& destination,
    const qsizetype maximumItems,
    const qsizetype maximumText)
{
    const auto value = object.value(key);
    if (!value.isArray() || value.toArray().size() > maximumItems) {
        return false;
    }
    QStringList decoded;
    decoded.reserve(value.toArray().size());
    for (const auto& entry : value.toArray()) {
        if (!entry.isString()) {
            return false;
        }
        const auto text = entry.toString();
        if (!boundedText(text, maximumText, false)) {
            return false;
        }
        decoded.push_back(text);
    }
    destination = std::move(decoded);
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

bool exactBoundedInteger(
    const QJsonObject& object,
    const QString& key,
    const int maximum,
    int& destination)
{
    const auto value = object.value(key);
    if (!value.isDouble()) {
        return false;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < 0 || number > maximum) {
        return false;
    }
    destination = static_cast<int>(number);
    return true;
}

bool validWorkingDirectory(const QString& path)
{
    return boundedText(path, identifierLimit, false)
        && QDir::isAbsolutePath(path)
        && QDir::cleanPath(path) == path;
}

std::optional<QString> customAgentsDirectory(const QString& cwd)
{
    if (!validWorkingDirectory(cwd)) {
        return std::nullopt;
    }
    const auto directory = QDir::cleanPath(
        QDir(cwd).filePath(QStringLiteral(".claude/agents")));
    if (!QDir::isAbsolutePath(directory)
        || QDir::cleanPath(directory) != directory) {
        return std::nullopt;
    }
    return directory;
}

QByteArray summaryKey(
    const QString& target,
    const QString& name,
    const QString& description,
    const std::optional<QString>& model,
    const QStringList& tools,
    const int errorCount)
{
    QJsonObject object {
        {QStringLiteral("target"), target},
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), description},
        {QStringLiteral("model"),
         model ? QJsonValue(*model) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("tools"), QJsonArray::fromStringList(tools)},
        {QStringLiteral("errorCount"), errorCount},
    };
    return QJsonDocument(std::move(object)).toJson(QJsonDocument::Compact);
}

QString accessibleId(
    const QString& selectionToken)
{
    QByteArray identity("kodosi.custom-agent.presentation.v1");
    identity.append('\0');
    identity.append(selectionToken.toUtf8());
    return QString::fromLatin1(
        QCryptographicHash::hash(
            identity,
            QCryptographicHash::Sha256)
            .toHex());
}

} // namespace

AgentCustomAgentsModel::AgentCustomAgentsModel(
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

int AgentCustomAgentsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant AgentCustomAgentsModel::data(
    const QModelIndex& index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_items.size()) {
        return {};
    }
    const auto& item = m_items.at(index.row());
    switch (role) {
    case ItemTokenRole: return item.itemToken;
    case NameRole: return item.name;
    case DescriptionRole: return item.description;
    case ModelRole: return item.model.value_or(QString {});
    case ToolsRole: return item.tools;
    case ParseErrorSummaryRole: return item.parseErrorSummary;
    case AccessibleIdRole: return item.accessibleId;
    default: return {};
    }
}

QHash<int, QByteArray> AgentCustomAgentsModel::roleNames() const
{
    return {
        {ItemTokenRole, QByteArrayLiteral("itemToken")},
        {NameRole, QByteArrayLiteral("name")},
        {DescriptionRole, QByteArrayLiteral("description")},
        {ModelRole, QByteArrayLiteral("model")},
        {ToolsRole, QByteArrayLiteral("tools")},
        {ParseErrorSummaryRole, QByteArrayLiteral("parseErrorSummary")},
        {AccessibleIdRole, QByteArrayLiteral("accessibleId")},
    };
}

AgentCustomAgentsModel::State AgentCustomAgentsModel::state() const noexcept
{
    return m_state;
}

bool AgentCustomAgentsModel::loading() const noexcept
{
    return m_state == State::Waiting || m_state == State::Loading;
}

QString AgentCustomAgentsModel::error() const
{
    return m_error;
}

QString AgentCustomAgentsModel::statusMessage() const
{
    return m_statusMessage;
}

QString AgentCustomAgentsModel::selectedItemToken() const
{
    return m_selectedItemToken;
}

QString AgentCustomAgentsModel::selectedName() const
{
    const auto* item = selectedItem();
    return item
        ? (item->detailLoaded ? item->detailName : item->name)
        : QString {};
}

QString AgentCustomAgentsModel::selectedDescription() const
{
    const auto* item = selectedItem();
    return item
        ? (item->detailLoaded ? item->detailDescription : item->description)
        : QString {};
}

QString AgentCustomAgentsModel::selectedModel() const
{
    const auto* item = selectedItem();
    const auto& model = item && item->detailLoaded
        ? item->detailModel
        : item
            ? item->model
            : std::optional<QString> {};
    return model.value_or(QString {});
}

QStringList AgentCustomAgentsModel::selectedTools() const
{
    const auto* item = selectedItem();
    return item
        ? (item->detailLoaded ? item->detailTools : item->tools)
        : QStringList {};
}

QStringList AgentCustomAgentsModel::selectedDisallowedTools() const
{
    const auto* item = selectedItem();
    return item && item->detailLoaded
        ? item->disallowedTools
        : QStringList {};
}

QString AgentCustomAgentsModel::frontmatter() const
{
    const auto* item = selectedItem();
    return item ? item->frontmatter : QString {};
}

QString AgentCustomAgentsModel::systemPrompt() const
{
    const auto* item = selectedItem();
    return item ? item->systemPrompt : QString {};
}

QString AgentCustomAgentsModel::detailErrors() const
{
    const auto* item = selectedItem();
    return item ? item->errors.join(QLatin1Char('\n')) : QString {};
}

bool AgentCustomAgentsModel::detailLoading() const noexcept
{
    const auto* item = selectedItem();
    return item && item->detailLoading;
}

bool AgentCustomAgentsModel::detailLoaded() const noexcept
{
    const auto* item = selectedItem();
    return item && item->detailLoaded;
}

QString AgentCustomAgentsModel::detailError() const
{
    const auto* item = selectedItem();
    return item ? item->detailError : QString {};
}

bool AgentCustomAgentsModel::inspect(const QString& sessionId)
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

void AgentCustomAgentsModel::close()
{
    ++m_demandGeneration;
    ++m_selectionGeneration;
    clearAuthority(true);
}

bool AgentCustomAgentsModel::retry()
{
    if (!m_demanded || m_pending) {
        return false;
    }
    if (m_failedOperation == Operation::Read
        && !m_selectedItemToken.isEmpty()) {
        m_retryItemToken = m_selectedItemToken;
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

bool AgentCustomAgentsModel::select(const QString& itemToken)
{
    if (!m_demanded || !canonicalUuidV7(itemToken) || m_pending) {
        return false;
    }
    const auto found =
        std::ranges::find(m_items, itemToken, &Item::itemToken);
    if (found == m_items.end()) {
        return false;
    }
    if (m_selectedItemToken != itemToken) {
        ++m_selectionGeneration;
        m_selectedItemToken = itemToken;
        emit selectionChanged();
    }
    if (found->detailLoaded || found->detailLoading) {
        return true;
    }
    const auto index = std::distance(m_items.begin(), found);
    if (!found->tokenAvailable) {
        m_retryItemToken = itemToken;
        return beginList();
    }
    return beginRead(index);
}

void AgentCustomAgentsModel::ingestAuthEvent(QByteArray json)
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

void AgentCustomAgentsModel::ingestAgentIntelEvent(QByteArray json)
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
            QStringLiteral("Future Custom Agents reply exceeds the ABI frame limit."));
    }
    if (admission == AccountEventAdmission::Current) {
        applyAgentIntelEvent(json);
    }
}

void AgentCustomAgentsModel::resetRuntimeAuthority()
{
    ++m_runtimeGeneration;
    ++m_demandGeneration;
    ++m_selectionGeneration;
    m_accountFence.reset();
    m_hasAccountContext = false;
    clearAuthority(true);
}

void AgentCustomAgentsModel::activateAccount(
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

void AgentCustomAgentsModel::applyAgentIntelEvent(const QByteArray& json)
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
            QStringLiteral("The runtime returned a malformed Custom Agents reply."),
            true);
        return;
    }
    const auto pending = *m_pending;
    if (type == QStringLiteral("agent.intel.error")) {
        const auto message =
            requiredString(object, QStringLiteral("message"), noticeLimit);
        setFailure(
            pending.operation,
            message
                ? (pending.operation == Operation::List
                        ? QStringLiteral(
                            "Couldn't load Custom Agents. Try again.")
                        : QStringLiteral(
                            "Couldn't read the selected Custom Agent. Refresh and try again."))
                : QStringLiteral(
                    "The runtime returned a malformed Custom Agents error."),
            !message);
        return;
    }
    const auto payload = object.value(QStringLiteral("payload"));
    if (!payload.isObject()) {
        setFailure(
            pending.operation,
            QStringLiteral("The runtime returned a malformed Custom Agents reply."),
            true);
        return;
    }
    if (pending.operation == Operation::List) {
        QVector<Item> items;
        if (!decodeListReply(payload.toObject(), items)) {
            setFailure(
                pending.operation,
                QStringLiteral("The runtime returned an invalid Custom Agents list."),
                true);
            return;
        }
        applyList(std::move(items));
        return;
    }
    Item detail;
    if (!decodeReadReply(payload.toObject(), pending, detail)) {
        setFailure(
            pending.operation,
            QStringLiteral("The runtime returned a different Custom Agent selection."),
            true);
        return;
    }
    auto* selected = selectedItem();
    if (!selected || selected->itemToken != pending.itemToken) {
        return;
    }
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation.reset();
    selected->detailName = std::move(detail.detailName);
    selected->detailDescription = std::move(detail.detailDescription);
    selected->detailModel = std::move(detail.detailModel);
    selected->detailTools = std::move(detail.detailTools);
    selected->disallowedTools = std::move(detail.disallowedTools);
    selected->frontmatter = std::move(detail.frontmatter);
    selected->systemPrompt = std::move(detail.systemPrompt);
    selected->errors = std::move(detail.errors);
    selected->detailError.clear();
    selected->detailLoading = false;
    selected->detailLoaded = true;
    emit selectionChanged();
}

void AgentCustomAgentsModel::authorityChanged()
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
                "The selected session changed. Reopen Custom Agents."));
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
            m_retryItemToken.clear();
            m_selectedIncarnationId.clear();
            m_selectedAgent.clear();
            m_workingDirectory.clear();
            m_agentsDirectory.clear();
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

void AgentCustomAgentsModel::attemptEligibility()
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
                "Custom Agents are available only for local sessions."));
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
            m_retryItemToken.clear();
            m_selectedIncarnationId.clear();
            m_selectedAgent.clear();
            m_workingDirectory.clear();
            m_agentsDirectory.clear();
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
                    "Custom Agents are available when Claude is active in this session."));
        }
        return;
    }
    if (identity->agent != QStringLiteral("claude")) {
        clearAuthority(false);
        setState(
            State::Ineligible,
            {},
            QStringLiteral(
                "Custom Agents are available only for Claude sessions."));
        return;
    }
    const auto agentsDirectory =
        customAgentsDirectory(identity->workingDirectory);
    if (!agentsDirectory
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
        && m_workingDirectory == identity->workingDirectory
        && m_agentsDirectory == *agentsDirectory;
    if (sameIdentity
        && (m_state == State::Ready || m_state == State::Failed
            || m_state == State::Loading)) {
        return;
    }
    if (!sameIdentity) {
        m_replyTimer.stop();
        m_pending.reset();
        m_failedOperation.reset();
        m_retryItemToken.clear();
        clearRows();
        clearSelection();
        m_selectedIncarnationId = session->incarnationId;
        m_selectedAgent = identity->agent;
        m_workingDirectory = identity->workingDirectory;
        m_agentsDirectory = *agentsDirectory;
    }
    (void)beginList();
}

void AgentCustomAgentsModel::invalidateDemand(QString message)
{
    ++m_demandGeneration;
    ++m_selectionGeneration;
    clearAuthority(true);
    setState(State::Ineligible, {}, std::move(message));
}

void AgentCustomAgentsModel::clearAuthority(const bool clearDemand)
{
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation.reset();
    m_retryItemToken.clear();
    m_selectedIncarnationId.clear();
    m_selectedAgent.clear();
    m_workingDirectory.clear();
    m_agentsDirectory.clear();
    clearRows();
    clearSelection();
    if (clearDemand) {
        m_demanded = false;
        m_selectedSessionId.clear();
        setState(State::Dormant);
    }
}

void AgentCustomAgentsModel::clearRows()
{
    if (m_items.isEmpty()) {
        return;
    }
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void AgentCustomAgentsModel::clearSelection()
{
    if (m_selectedItemToken.isEmpty()) {
        return;
    }
    ++m_selectionGeneration;
    m_selectedItemToken.clear();
    emit selectionChanged();
}

void AgentCustomAgentsModel::setState(
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

void AgentCustomAgentsModel::setFailure(
    const Operation operation,
    QString message,
    const bool protocolFault)
{
    if (protocolFault) {
        emit decodeError(message);
    }
    const auto pending = m_pending;
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation = operation;
    if (operation == Operation::Read) {
        if (pending) {
            const auto found =
                std::ranges::find(m_items, pending->itemToken, &Item::itemToken);
            if (found != m_items.end()) {
                found->detailLoading = false;
                found->detailLoaded = false;
                found->detailError = std::move(message);
                m_retryItemToken = found->itemToken;
            }
        }
        setState(State::Ready);
        emit selectionChanged();
    } else {
        setState(State::Failed, std::move(message));
    }
}

bool AgentCustomAgentsModel::beginList()
{
    if (!m_demanded || m_pending
        || m_selectedIncarnationId.isEmpty()
        || !validWorkingDirectory(m_workingDirectory)
        || m_agentsDirectory
            != customAgentsDirectory(m_workingDirectory).value_or(QString {})) {
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.listCustomAgentsBound")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("directory"), m_agentsDirectory},
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
            .agentsDirectory = m_agentsDirectory,
            .itemToken = {},
            .selectionToken = {},
            .target = {},
        });
}

bool AgentCustomAgentsModel::beginRead(const qsizetype index)
{
    if (m_pending || index < 0 || index >= m_items.size()) {
        return false;
    }
    auto& item = m_items[index];
    if (!item.tokenAvailable || !canonicalUuidV7(item.selectionToken)) {
        return false;
    }
    item.tokenAvailable = false;
    item.detailLoading = true;
    item.detailLoaded = false;
    item.detailError.clear();
    item.frontmatter.clear();
    item.systemPrompt.clear();
    item.errors.clear();
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.readCustomAgentBound")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("selectionToken"), item.selectionToken},
    };
    emit selectionChanged();
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
            .agentsDirectory = m_agentsDirectory,
            .itemToken = item.itemToken,
            .selectionToken = item.selectionToken,
            .target = item.target,
        });
}

bool AgentCustomAgentsModel::sendCommand(
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
                    "Couldn't request Custom Agents. Try again.")
                : QStringLiteral(
                    "Couldn't request the selected Custom Agent. Refresh and try again."),
            false);
    }
    return false;
}

bool AgentCustomAgentsModel::pendingIsCurrent(
    const Pending& pending) const
{
    if (!m_demanded
        || pending.runtimeGeneration != m_runtimeGeneration
        || pending.demandGeneration != m_demandGeneration
        || pending.selectionGeneration != m_selectionGeneration
        || pending.sessionId != m_selectedSessionId
        || pending.sessionIncarnationId != m_selectedIncarnationId
        || pending.agentsDirectory != m_agentsDirectory
        || (pending.operation == Operation::Read
            && pending.itemToken != m_selectedItemToken)) {
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
        && identity->agent == QStringLiteral("claude")
        && identity->agent == m_selectedAgent
        && identity->workingDirectory == m_workingDirectory
        && customAgentsDirectory(identity->workingDirectory)
            == std::optional<QString>(m_agentsDirectory);
}

bool AgentCustomAgentsModel::decodeListReply(
    const QJsonObject& payload,
    QVector<Item>& items) const
{
    if (!exactFields(payload, {QStringLiteral("items")})
        || !payload.value(QStringLiteral("items")).isArray()
        || QJsonDocument(payload).toJson(QJsonDocument::Compact).size()
            > maximumSummaryBytes) {
        return false;
    }
    const auto array = payload.value(QStringLiteral("items")).toArray();
    if (array.size() > maximumItems) {
        return false;
    }
    QSet<QString> selectionTokens;
    items.reserve(array.size());
    for (const auto& value : array) {
        if (!value.isObject()) {
            return false;
        }
        const auto object = value.toObject();
        if (!exactFields(
                object,
                {QStringLiteral("selectionToken"),
                 QStringLiteral("target"),
                 QStringLiteral("name"),
                 QStringLiteral("description"),
                 QStringLiteral("model"),
                 QStringLiteral("tools"),
                 QStringLiteral("errorCount")})) {
            return false;
        }
        const auto selectionToken =
            requiredString(object, QStringLiteral("selectionToken"));
        const auto target =
            requiredString(object, QStringLiteral("target"), 16);
        const auto name = requiredString(
            object,
            QStringLiteral("name"),
            summaryNameLimit,
            true);
        const auto description = requiredString(
            object,
            QStringLiteral("description"),
            summaryDescriptionLimit,
            true);
        std::optional<QString> model;
        QStringList tools;
        int errorCount = 0;
        if (!selectionToken || !canonicalUuidV7(*selectionToken)
            || selectionTokens.contains(*selectionToken)
            || !target
            || (*target != QStringLiteral("claude")
                && *target != QStringLiteral("vsCode")
                && *target != QStringLiteral("unknown"))
            || !name || !description
            || !nullableString(
                object,
                QStringLiteral("model"),
                model,
                summaryModelLimit,
                true)
            || !requiredStringList(
                object,
                QStringLiteral("tools"),
                tools,
                maximumTools,
                summaryToolLimit)
            || !exactBoundedInteger(
                object,
                QStringLiteral("errorCount"),
                maximumErrors,
                errorCount)) {
            return false;
        }
        selectionTokens.insert(*selectionToken);
        items.push_back({
            .itemToken = {},
            .selectionToken = *selectionToken,
            .target = *target,
            .name = *name,
            .description = *description,
            .model = model,
            .tools = tools,
            .errorCount = errorCount,
            .tokenAvailable = true,
            .detailName = {},
            .detailDescription = {},
            .detailModel = std::nullopt,
            .detailTools = {},
            .disallowedTools = {},
            .frontmatter = {},
            .systemPrompt = {},
            .errors = {},
            .parseErrorSummary = errorCount == 0
                ? QString {}
                : errorCount == 1
                    ? QStringLiteral("1 parse error")
                    : QStringLiteral("%1 parse errors").arg(errorCount),
            .accessibleId = accessibleId(*selectionToken),
            .detailError = {},
            .detailLoading = false,
            .detailLoaded = false,
            .summaryKey = summaryKey(
                *target,
                *name,
                *description,
                model,
                tools,
                errorCount),
        });
    }
    return true;
}

bool AgentCustomAgentsModel::decodeReadReply(
    const QJsonObject& payload,
    const Pending& pending,
    Item& detail) const
{
    if (!exactFields(
            payload,
            {QStringLiteral("selectionToken"),
             QStringLiteral("target"),
             QStringLiteral("name"),
             QStringLiteral("description"),
             QStringLiteral("model"),
             QStringLiteral("tools"),
             QStringLiteral("disallowedTools"),
             QStringLiteral("frontmatter"),
             QStringLiteral("prompt"),
             QStringLiteral("errors")})
        || QJsonDocument(payload).toJson(QJsonDocument::Compact).size()
            > maximumDetailBytes) {
        return false;
    }
    const auto selectionToken =
        requiredString(payload, QStringLiteral("selectionToken"));
    const auto target =
        requiredString(payload, QStringLiteral("target"), 16);
    const auto name = requiredString(
        payload,
        QStringLiteral("name"),
        detailTextLimit,
        true);
    const auto description = requiredString(
        payload,
        QStringLiteral("description"),
        detailTextLimit,
        true);
    const auto frontmatter = requiredString(
        payload,
        QStringLiteral("frontmatter"),
        maximumFrontmatterBytes,
        true);
    const auto prompt = requiredString(
        payload,
        QStringLiteral("prompt"),
        maximumSourceBytes,
        true);
    std::optional<QString> model;
    QStringList tools;
    QStringList disallowedTools;
    QStringList errors;
    if (!selectionToken || *selectionToken != pending.selectionToken
        || !canonicalUuidV7(*selectionToken)
        || !target || *target != pending.target
        || !name || !description || !frontmatter || !prompt
        || frontmatter->toUtf8().size() + prompt->toUtf8().size()
            > maximumSourceBytes
        || !nullableString(
            payload,
            QStringLiteral("model"),
            model,
            detailTextLimit,
            true)
        || !requiredStringList(
            payload,
            QStringLiteral("tools"),
            tools,
            maximumFrontmatterBytes,
            detailTextLimit)
        || !requiredStringList(
            payload,
            QStringLiteral("disallowedTools"),
            disallowedTools,
            maximumFrontmatterBytes,
            detailTextLimit)
        || !requiredStringList(
            payload,
            QStringLiteral("errors"),
            errors,
            maximumErrors,
            detailTextLimit)
        || std::ranges::any_of(errors, [&](const QString& entry) {
            return entry.contains(m_agentsDirectory);
        })) {
        return false;
    }
    detail.detailName = *name;
    detail.detailDescription = *description;
    detail.detailModel = std::move(model);
    detail.detailTools = std::move(tools);
    detail.disallowedTools = std::move(disallowedTools);
    detail.frontmatter = *frontmatter;
    detail.systemPrompt = *prompt;
    detail.errors = std::move(errors);
    return true;
}

void AgentCustomAgentsModel::applyList(QVector<Item> items)
{
    m_replyTimer.stop();
    m_pending.reset();
    m_failedOperation.reset();

    QHash<QByteArray, int> oldCounts;
    QHash<QByteArray, int> newCounts;
    QHash<QByteArray, QString> oldTokens;
    QHash<QByteArray, QString> oldAccessibleIds;
    for (const auto& item : m_items) {
        ++oldCounts[item.summaryKey];
        oldTokens.insert(item.summaryKey, item.itemToken);
        oldAccessibleIds.insert(item.summaryKey, item.accessibleId);
    }
    for (const auto& item : items) {
        ++newCounts[item.summaryKey];
    }
    QSet<QString> usedTokens;
    for (auto& item : items) {
        if (oldCounts.value(item.summaryKey) == 1
            && newCounts.value(item.summaryKey) == 1) {
            item.itemToken = oldTokens.value(item.summaryKey);
            item.accessibleId =
                oldAccessibleIds.value(item.summaryKey);
        }
        if (item.itemToken.isEmpty()) {
            do {
                item.itemToken =
                    QUuid::createUuidV7().toString(QUuid::WithoutBraces);
            } while (usedTokens.contains(item.itemToken));
        }
        usedTokens.insert(item.itemToken);
    }
    std::ranges::sort(items, [](const Item& left, const Item& right) {
        const auto byName = QString::compare(
            left.name,
            right.name,
            Qt::CaseSensitive);
        if (byName != 0) {
            return byName < 0;
        }
        const auto byDescription = QString::compare(
            left.description,
            right.description,
            Qt::CaseSensitive);
        return byDescription == 0
            ? left.itemToken < right.itemToken
            : byDescription < 0;
    });
    const auto countChangedValue = m_items.size() != items.size();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }

    const auto selected = std::ranges::find(
        m_items,
        m_selectedItemToken,
        &Item::itemToken);
    const auto selectedIndex = selected == m_items.end()
        ? qsizetype {-1}
        : std::distance(m_items.begin(), selected);
    if (!m_selectedItemToken.isEmpty() && selectedIndex < 0) {
        clearSelection();
    } else if (selectedIndex >= 0) {
        emit selectionChanged();
    }
    m_retryItemToken.clear();
    setState(
        State::Ready,
        {},
        m_items.isEmpty()
            ? QStringLiteral(
                "No Custom Agents are defined for this project.")
            : QString {});
    if (selectedIndex >= 0) {
        (void)beginRead(selectedIndex);
    }
}

const AgentCustomAgentsModel::Item*
AgentCustomAgentsModel::selectedItem() const
{
    const auto found = std::ranges::find(
        m_items,
        m_selectedItemToken,
        &Item::itemToken);
    return found == m_items.end() ? nullptr : &*found;
}

AgentCustomAgentsModel::Item*
AgentCustomAgentsModel::selectedItem()
{
    const auto found = std::ranges::find(
        m_items,
        m_selectedItemToken,
        &Item::itemToken);
    return found == m_items.end() ? nullptr : &*found;
}

} // namespace kodosi
