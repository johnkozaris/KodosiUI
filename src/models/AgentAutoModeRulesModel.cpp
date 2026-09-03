#include "models/AgentAutoModeRulesModel.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype maximumRuleBytes = 10 * 1024;
constexpr qsizetype maximumRuleCount = 1'024;
constexpr qsizetype maximumTextBytes =
    maximumRuleCount * maximumRuleBytes + maximumRuleCount - 1;
constexpr qsizetype maximumDetailBytes = 8'192;
constexpr quint8 maximumReconcileAttempts = 3;

QString translated(const char* value)
{
    return QCoreApplication::translate("AgentAutoModeRulesModel", value);
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

bool canonicalRevision(const QString& value)
{
    return value.size() == 16
        && std::ranges::all_of(value, [](const QChar character) {
               return (character >= QLatin1Char('0')
                       && character <= QLatin1Char('9'))
                   || (character >= QLatin1Char('a')
                       && character <= QLatin1Char('f'));
           });
}

bool decodeRuleArray(
    const QJsonValue& value,
    QStringList& lines)
{
    if (value.isUndefined()) {
        lines.clear();
        return true;
    }
    if (!value.isArray()) {
        return false;
    }
    for (const auto& item : value.toArray()) {
        if (!item.isString() || item.toString().contains(QChar::Null)
            || item.toString().toUtf8().size() > maximumRuleBytes
            || lines.size() >= maximumRuleCount) {
            return false;
        }
        lines.push_back(item.toString());
    }
    return true;
}

bool decodeRules(
    const QJsonValue& value,
    QStringList& environment,
    QStringList& allow,
    QStringList& softDeny,
    QStringList& hardDeny)
{
    if (!value.isObject()) {
        return false;
    }
    const auto rules = value.toObject();
    for (const auto& key : rules.keys()) {
        if (key != QStringLiteral("environment")
            && key != QStringLiteral("allow")
            && key != QStringLiteral("soft_deny")
            && key != QStringLiteral("hard_deny")) {
            return false;
        }
    }
    return decodeRuleArray(
               rules.value(QStringLiteral("environment")),
               environment)
        && decodeRuleArray(
            rules.value(QStringLiteral("allow")),
            allow)
        && decodeRuleArray(
            rules.value(QStringLiteral("soft_deny")),
            softDeny)
        && decodeRuleArray(
            rules.value(QStringLiteral("hard_deny")),
            hardDeny);
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
    if (!value.isString() || value.toString().isEmpty()
        || value.toString().contains(QChar::Null)
        || value.toString().toUtf8().size() > 1'024) {
        return false;
    }
    output = value.toString();
    return true;
}

} // namespace

AgentAutoModeRulesModel::AgentAutoModeRulesModel(
    CommandDispatcher& dispatcher,
    const qint64 replyTimeoutMs,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_replyTimeoutMs(replyTimeoutMs)
{
    Q_ASSERT(replyTimeoutMs >= 0);
    Q_ASSERT(replyTimeoutMs <= std::numeric_limits<int>::max());
    m_replyTimer.setSingleShot(true);
    connect(&m_replyTimer, &QTimer::timeout, this, [this] {
        if (!m_pending || !pendingCurrent(*m_pending)) {
            return;
        }
        m_pending.reset();
        m_error = translated("The runtime did not reply in time.");
        m_state = State::Failed;
        emit changed();
    });
    m_deliveryTimer.setSingleShot(true);
    connect(&m_deliveryTimer, &QTimer::timeout, this, [this] {
        if (!m_delivery || !deliveryCurrent(*m_delivery)) {
            return;
        }
        auto delivery = *m_delivery;
        m_delivery.reset();
        if (dispatchReconcile(std::move(delivery))) {
            m_status = translated(
                "The save result was delayed. Kodosi is reconciling it.");
            emit changed();
            return;
        }
        if (m_error.isEmpty() && m_statusAfterRead.isEmpty()) {
            settleDeliveryFailure(
                translated("The save result could not be reconciled."));
        }
    });
}

AgentAutoModeRulesModel::State AgentAutoModeRulesModel::state() const noexcept
{
    return m_state;
}
QString AgentAutoModeRulesModel::error() const { return m_error; }
QString AgentAutoModeRulesModel::environmentText() const { return m_environment; }
QString AgentAutoModeRulesModel::allowText() const { return m_allow; }
QString AgentAutoModeRulesModel::softDenyText() const { return m_softDeny; }
QString AgentAutoModeRulesModel::hardDenyText() const { return m_hardDeny; }
bool AgentAutoModeRulesModel::dirty() const noexcept
{
    return normalizedLines(m_environment) != normalizedLines(m_baselineEnvironment)
        || normalizedLines(m_allow) != normalizedLines(m_baselineAllow)
        || normalizedLines(m_softDeny) != normalizedLines(m_baselineSoftDeny)
        || normalizedLines(m_hardDeny) != normalizedLines(m_baselineHardDeny);
}
bool AgentAutoModeRulesModel::saving() const noexcept
{
    return m_delivery.has_value();
}
QString AgentAutoModeRulesModel::statusMessage() const { return m_status; }

void AgentAutoModeRulesModel::setEnvironmentText(const QString& value)
{
    setText(m_environment, value);
}
void AgentAutoModeRulesModel::setAllowText(const QString& value)
{
    setText(m_allow, value);
}
void AgentAutoModeRulesModel::setSoftDenyText(const QString& value)
{
    setText(m_softDeny, value);
}
void AgentAutoModeRulesModel::setHardDenyText(const QString& value)
{
    setText(m_hardDeny, value);
}

bool AgentAutoModeRulesModel::refresh(const bool force)
{
    const auto wasDemanded = m_demanded;
    m_demanded = true;
    if (!m_hasAccountContext) {
        return false;
    }
    if (m_delivery) {
        return true;
    }
    if (m_pending) {
        if (!force || m_pending->operation != Operation::Read) {
            return true;
        }
        ++m_demandGeneration;
        m_pending.reset();
        m_replyTimer.stop();
    }
    if (!force && wasDemanded && m_state == State::Ready
        && !m_targetToken.isEmpty()) {
        return true;
    }
    if (force) {
        ++m_demandGeneration;
    }
    return dispatchRead(!force && dirty());
}

bool AgentAutoModeRulesModel::save()
{
    if (!m_hasAccountContext || m_pending || m_delivery || !dirty()
        || m_targetToken.isEmpty() || m_revision.isEmpty()) {
        return false;
    }

    return dispatchWrite(
        QUuid::createUuidV7().toString(QUuid::WithoutBraces));
}

void AgentAutoModeRulesModel::close()
{
    m_demanded = false;
    ++m_demandGeneration;
    m_pending.reset();
    m_replyTimer.stop();
    m_targetToken.clear();
    m_revision.clear();
    clearRules();
    m_state = State::Dormant;
    m_error.clear();
    m_status.clear();
    if (!m_delivery) {
        m_statusAfterRead.clear();
    }
    emit changed();
}

void AgentAutoModeRulesModel::installSyntheticFixture()
{
    ++m_demandGeneration;
    m_pending.reset();
    m_delivery.reset();
    m_replyTimer.stop();
    m_deliveryTimer.stop();
    m_demanded = true;
    m_environment = QStringLiteral("Read\nGrep\nGlob");
    m_allow = QStringLiteral("$defaults\ncargo test *\njust check");
    m_softDeny = QStringLiteral("git push *");
    m_hardDeny = QStringLiteral("rm -rf *\nshutdown *");
    m_baselineEnvironment = m_environment;
    m_baselineAllow = m_allow;
    m_baselineSoftDeny = m_softDeny;
    m_baselineHardDeny = m_hardDeny;
    m_state = State::Ready;
    m_error.clear();
    m_status = translated("Bound to the current Claude settings revision.");
    m_statusAfterRead.clear();
    emit changed();
}

void AgentAutoModeRulesModel::ingestAuthEvent(QByteArray json)
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
        emit decodeError(translated("Authentication context is malformed."));
        return;
    }
    activateAccount(std::move(userId), *epoch);
}

void AgentAutoModeRulesModel::ingestAgentIntelEvent(QByteArray json)
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
        emit decodeError(translated("Agent-intelligence authority is malformed."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {.userId = std::move(userId), .epoch = *epoch},
        json);
    if (admission == AccountEventAdmission::Current) {
        applyAgentIntelEvent(json);
    }
}

void AgentAutoModeRulesModel::resetRuntimeAuthority()
{
    ++m_runtimeGeneration;
    ++m_demandGeneration;
    m_accountFence.reset();
    m_hasAccountContext = false;
    m_pending.reset();
    m_delivery.reset();
    m_replyTimer.stop();
    m_deliveryTimer.stop();
    m_targetToken.clear();
    m_revision.clear();
    clearRules();
    m_state = State::Dormant;
    m_error.clear();
    m_status.clear();
    m_statusAfterRead.clear();
    emit changed();
}

bool AgentAutoModeRulesModel::dispatchRead(const bool preserveDraft)
{
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_state = State::Loading;
    m_error.clear();
    emit changed();
    return send(
        {
            {QStringLiteral("type"),
             QStringLiteral("agent.intel.readClaudeAutoModeRulesBound")},
            {QStringLiteral("requestId"), requestId},
        },
        {
            .requestId = requestId,
            .operation = Operation::Read,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .preserveDraft = preserveDraft,
        });
}

bool AgentAutoModeRulesModel::dispatchWrite(const QString& mutationId)
{
    const auto environment = normalizedLines(m_environment);
    const auto allow = normalizedLines(m_allow);
    const auto softDeny = normalizedLines(m_softDeny);
    const auto hardDeny = normalizedLines(m_hardDeny);
    const auto validRules = [](const QStringList& rules) {
        return rules.size() <= maximumRuleCount
            && std::ranges::all_of(rules, [](const QString& rule) {
                   return !rule.contains(QChar::Null)
                       && rule.toUtf8().size() <= maximumRuleBytes;
               });
    };
    if (!validRules(environment) || !validRules(allow)
        || !validRules(softDeny) || !validRules(hardDeny)) {
        m_state = State::Failed;
        m_error = translated("Auto-mode rules exceeded their bounds.");
        emit changed();
        return false;
    }
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command;
    command.insert(
        QStringLiteral("type"),
        QStringLiteral("agent.intel.writeClaudeAutoModeRulesBound"));
    command.insert(QStringLiteral("requestId"), requestId);
    command.insert(QStringLiteral("mutationId"), mutationId);
    command.insert(QStringLiteral("targetToken"), m_targetToken);
    command.insert(QStringLiteral("expectedRevision"), m_revision);
    const auto insertLines = [&command](
                                 const QString& key,
                                 const QStringList& lines) {
        QJsonArray values;
        for (const auto& line : lines) {
            values.append(line);
        }
        command.insert(key, values);
    };
    insertLines(QStringLiteral("environment"), environment);
    insertLines(QStringLiteral("allow"), allow);
    insertLines(QStringLiteral("softDeny"), softDeny);
    insertLines(QStringLiteral("hardDeny"), hardDeny);
    m_error.clear();
    m_status.clear();
    m_statusAfterRead.clear();
    const auto delivered = sendDelivery(
        std::move(command),
        {
            .requestId = requestId,
            .mutationId = mutationId,
            .operation = Operation::Write,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .expectedEnvironment = environment,
            .expectedAllow = allow,
            .expectedSoftDeny = softDeny,
            .expectedHardDeny = hardDeny,
        });
    if (delivered) {
        m_targetToken.clear();
    }
    emit changed();
    return delivered;
}

bool AgentAutoModeRulesModel::dispatchReconcile(Pending mutation)
{
    if (mutation.reconcileAttempts >= maximumReconcileAttempts) {
        settleDeliveryFailure(
            translated("The save result could not be reconciled."));
        return false;
    }
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.reconcileClaudeAutoModeRulesWrite")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("mutationId"), mutation.mutationId},
    };
    mutation.requestId = requestId;
    mutation.operation = Operation::Reconcile;
    mutation.runtimeGeneration = m_runtimeGeneration;
    ++mutation.reconcileAttempts;
    return sendDelivery(std::move(command), std::move(mutation));
}

bool AgentAutoModeRulesModel::send(QJsonObject command, Pending pending)
{
    const auto bytes = QJsonDocument(command).toJson(QJsonDocument::Compact);
    const auto result = m_dispatcher.send(CommandLane::AgentIntel, bytes);
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

bool AgentAutoModeRulesModel::sendDelivery(
    QJsonObject command,
    Pending pending)
{
    const auto bytes = QJsonDocument(command).toJson(QJsonDocument::Compact);
    const auto result = m_dispatcher.send(CommandLane::AgentIntel, bytes);
    if (!result) {
        settleDeliveryFailure(result.error().message);
        return false;
    }
    m_delivery = std::move(pending);
    m_deliveryTimer.start(static_cast<int>(m_replyTimeoutMs));
    emit changed();
    return true;
}

void AgentAutoModeRulesModel::settleDeliveryFailure(
    QString message,
    const bool malformed)
{
    m_delivery.reset();
    m_deliveryTimer.stop();
    if (m_demanded) {
        m_error = message;
        m_state = State::Failed;
    } else {
        m_statusAfterRead = message;
        m_state = State::Dormant;
    }
    if (malformed) {
        emit decodeError(message);
    }
    emit changed();
}

void AgentAutoModeRulesModel::applyAgentIntelEvent(const QByteArray& json)
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
    Pending pending;
    if (m_delivery && requestId == m_delivery->requestId
        && deliveryCurrent(*m_delivery)) {
        pending = *m_delivery;
        m_delivery.reset();
        m_deliveryTimer.stop();
    } else if (m_pending && requestId == m_pending->requestId
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
        return;
    }
    if (type != QStringLiteral("agent.intel.reply")
        || !exactAccountEnvelope(object, {QStringLiteral("payload")})
        || !object.value(QStringLiteral("payload")).isObject()) {
        const auto message = translated("Auto-mode reply was malformed.");
        if (pending.operation == Operation::Read) {
            m_error = message;
            m_state = State::Failed;
            emit decodeError(m_error);
            emit changed();
        } else {
            settleDeliveryFailure(message, true);
        }
        return;
    }
    const auto payload = object.value(QStringLiteral("payload")).toObject();
    if (pending.operation == Operation::Read) {
        applyRead(payload, pending);
    } else {
        applyReceipt(payload, pending);
    }
}

void AgentAutoModeRulesModel::activateAccount(QString userId, const quint64 epoch)
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
        ++m_runtimeGeneration;
        ++m_demandGeneration;
        m_pending.reset();
        m_delivery.reset();
        m_replyTimer.stop();
        m_deliveryTimer.stop();
        m_targetToken.clear();
        m_revision.clear();
        clearRules();
        m_state = State::Dormant;
        m_error.clear();
        m_status.clear();
        m_statusAfterRead.clear();
        emit changed();
    }
    for (auto& event : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(event));
    }
    if (m_demanded && !m_pending
        && (m_state != State::Ready || m_targetToken.isEmpty())) {
        (void)dispatchRead(dirty());
    }
}

void AgentAutoModeRulesModel::applyRead(
    const QJsonObject& payload,
    const Pending& pending)
{
    if (!exactFields(
            payload,
            {QStringLiteral("targetToken"),
             QStringLiteral("revision"),
             QStringLiteral("rules")})) {
        m_error = translated("Auto-mode read reply was malformed.");
        m_state = State::Failed;
        emit decodeError(m_error);
        emit changed();
        return;
    }
    const auto target = payload.value(QStringLiteral("targetToken")).toString();
    const auto revision = payload.value(QStringLiteral("revision")).toString();
    QStringList environment;
    QStringList allow;
    QStringList softDeny;
    QStringList hardDeny;
    if (!canonicalUuidV7(target) || !canonicalRevision(revision)
        || !decodeRules(
            payload.value(QStringLiteral("rules")),
            environment,
            allow,
            softDeny,
            hardDeny)) {
        m_error = translated("Auto-mode read reply was malformed.");
        m_state = State::Failed;
        emit decodeError(m_error);
        emit changed();
        return;
    }
    if (!pending.preserveDraft) {
        m_environment = environment.join(QLatin1Char('\n'));
        m_allow = allow.join(QLatin1Char('\n'));
        m_softDeny = softDeny.join(QLatin1Char('\n'));
        m_hardDeny = hardDeny.join(QLatin1Char('\n'));
    }
    m_targetToken = target;
    m_revision = revision;
    m_baselineEnvironment = environment.join(QLatin1Char('\n'));
    m_baselineAllow = allow.join(QLatin1Char('\n'));
    m_baselineSoftDeny = softDeny.join(QLatin1Char('\n'));
    m_baselineHardDeny = hardDeny.join(QLatin1Char('\n'));
    m_error.clear();
    m_status = std::exchange(m_statusAfterRead, {});
    m_state = State::Ready;
    emit changed();
}

void AgentAutoModeRulesModel::applyReceipt(
    const QJsonObject& payload,
    const Pending& pending)
{
    if (!exactFields(
            payload,
            {QStringLiteral("mutationId"),
             QStringLiteral("outcome"),
             QStringLiteral("revision"),
             QStringLiteral("rules"),
             QStringLiteral("detail")})) {
        settleDeliveryFailure(
            translated("Auto-mode mutation receipt was malformed."),
            true);
        return;
    }
    const auto mutationId = payload.value(QStringLiteral("mutationId")).toString();
    const auto outcome = payload.value(QStringLiteral("outcome")).toString();
    const auto revision = payload.value(QStringLiteral("revision")).toString();
    const auto detailValue = payload.value(QStringLiteral("detail"));
    QString detail;
    if (detailValue.isString()) {
        detail = detailValue.toString();
    }
    QStringList environment;
    QStringList allow;
    QStringList softDeny;
    QStringList hardDeny;
    if (!canonicalUuidV7(mutationId) || mutationId != pending.mutationId
        || (outcome != QStringLiteral("applied")
            && outcome != QStringLiteral("indeterminate"))
        || !canonicalRevision(revision)
        || (outcome == QStringLiteral("applied")
            && !detailValue.isNull())
        || (outcome == QStringLiteral("indeterminate")
            && (!detailValue.isString() || detail.isEmpty()
                || detail.contains(QChar::Null)
                || detail.toUtf8().size() > maximumDetailBytes))
        || !decodeRules(
            payload.value(QStringLiteral("rules")),
            environment,
            allow,
            softDeny,
            hardDeny)
        || environment != pending.expectedEnvironment
        || allow != pending.expectedAllow
        || softDeny != pending.expectedSoftDeny
        || hardDeny != pending.expectedHardDeny) {
        settleDeliveryFailure(
            translated("Auto-mode mutation receipt was malformed."),
            true);
        return;
    }
    m_statusAfterRead = outcome == QStringLiteral("applied")
        ? translated("Rules saved.")
        : detail;
    if (!m_demanded) {
        m_state = State::Dormant;
        emit changed();
        return;
    }
    m_environment = environment.join(QLatin1Char('\n'));
    m_allow = allow.join(QLatin1Char('\n'));
    m_softDeny = softDeny.join(QLatin1Char('\n'));
    m_hardDeny = hardDeny.join(QLatin1Char('\n'));
    m_revision = revision;
    m_baselineEnvironment = m_environment;
    m_baselineAllow = m_allow;
    m_baselineSoftDeny = m_softDeny;
    m_baselineHardDeny = m_hardDeny;
    m_error.clear();
    m_status = m_statusAfterRead;
    m_state = State::Ready;
    emit changed();
    (void)dispatchRead(false);
}

void AgentAutoModeRulesModel::applyError(
    const QJsonObject& object,
    const Pending& pending)
{
    if (!exactAccountEnvelope(
            object,
            {QStringLiteral("message"),
             QStringLiteral("failureKind"),
             QStringLiteral("mutationId"),
             QStringLiteral("reconciliationRequired")})) {
        const auto message =
            translated("Auto-mode error reply was malformed.");
        if (pending.operation == Operation::Read) {
            m_error = message;
            m_state = State::Failed;
            emit decodeError(message);
            emit changed();
        } else {
            settleDeliveryFailure(message, true);
        }
        return;
    }
    const auto messageValue = object.value(QStringLiteral("message"));
    const auto failureValue = object.value(QStringLiteral("failureKind"));
    const auto mutationValue = object.value(QStringLiteral("mutationId"));
    const auto reconciliationValue =
        object.value(QStringLiteral("reconciliationRequired"));
    QString mutationId;
    if (mutationValue.isString()) {
        mutationId = mutationValue.toString();
    }
    if (!messageValue.isString() || messageValue.toString().isEmpty()
        || messageValue.toString().contains(QChar::Null)
        || messageValue.toString().toUtf8().size() > maximumDetailBytes
        || !failureValue.isString()
        || (failureValue.toString() != QStringLiteral("deterministic")
            && failureValue.toString() != QStringLiteral("deliveryAmbiguous"))
        || (!mutationValue.isNull()
            && (!mutationValue.isString()
                || !canonicalUuidV7(mutationId)))
        || !reconciliationValue.isBool()) {
        const auto message =
            translated("Auto-mode error reply was malformed.");
        if (pending.operation == Operation::Read) {
            m_error = message;
            m_state = State::Failed;
            emit decodeError(message);
            emit changed();
        } else {
            settleDeliveryFailure(message, true);
        }
        return;
    }
    if ((pending.operation == Operation::Write
         || pending.operation == Operation::Reconcile)
        && failureValue.toString() == QStringLiteral("deliveryAmbiguous")
        && reconciliationValue.toBool()
        && mutationId == pending.mutationId) {
        if (dispatchReconcile(pending)) {
            m_status = translated("The save result was ambiguous. Kodosi is reconciling it.");
            emit changed();
            return;
        }
    }
    if (pending.operation == Operation::Read) {
        m_error = messageValue.toString();
        m_state = State::Failed;
        emit changed();
    } else {
        settleDeliveryFailure(messageValue.toString());
    }
}

void AgentAutoModeRulesModel::clearRules()
{
    m_environment.clear();
    m_allow.clear();
    m_softDeny.clear();
    m_hardDeny.clear();
    m_baselineEnvironment.clear();
    m_baselineAllow.clear();
    m_baselineSoftDeny.clear();
    m_baselineHardDeny.clear();
}

void AgentAutoModeRulesModel::setText(QString& field, const QString& value)
{
    if (field == value || value.contains(QChar::Null)
        || value.toUtf8().size() > maximumTextBytes) {
        return;
    }
    field = value;
    emit changed();
}

QStringList AgentAutoModeRulesModel::normalizedLines(const QString& value)
{
    QStringList lines;
    for (const auto& line : value.split(QLatin1Char('\n'))) {
        const auto trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            lines.push_back(trimmed);
        }
    }
    return lines;
}

bool AgentAutoModeRulesModel::pendingCurrent(const Pending& pending) const
{
    return pending.runtimeGeneration == m_runtimeGeneration
        && pending.demandGeneration == m_demandGeneration;
}

bool AgentAutoModeRulesModel::deliveryCurrent(const Pending& pending) const
{
    return pending.runtimeGeneration == m_runtimeGeneration;
}

} // namespace kodosi
