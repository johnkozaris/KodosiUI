#include "models/TrustModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

std::optional<QString> requiredString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    if (!value.isString() || value.toString().isEmpty()) {
        return std::nullopt;
    }
    return value.toString();
}

QString optionalString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    return value.isString() ? value.toString() : QString {};
}

std::optional<quint64> unsignedInteger(
    const QJsonValue& value,
    const quint64 maximum)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0
        || number > static_cast<double>(maximum)
        || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<quint64>(number);
}

std::optional<qint64> signedInteger(const QJsonValue& value)
{
    constexpr auto maximumExactJsonInteger = 9'007'199'254'740'991.0;
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < -maximumExactJsonInteger
        || number > maximumExactJsonInteger || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<qint64>(number);
}

} // namespace

TrustModel::TrustModel(CommandDispatcher& dispatcher, QObject* parent)
    : QAbstractListModel(parent)
    , m_dispatcher(dispatcher)
{
}

int TrustModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_pins.size();
}

QVariant TrustModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_pins.size()) {
        return {};
    }
    const auto& pin = m_pins[index.row()];
    switch (role) {
    case UserIdRole:
        return pin.userId;
    case GenerationRole:
        return QVariant::fromValue(pin.generation);
    case SignerDeviceIdRole:
        return pin.signerDeviceId;
    case DeviceCountRole:
        return pin.deviceCount;
    case PinnedAtMsRole:
        return pin.pinnedAtMs;
    case ResetAvailabilityRole:
        return QVariant::fromValue(resetAvailability(pin.userId));
    default:
        return {};
    }
}

QHash<int, QByteArray> TrustModel::roleNames() const
{
    return {
        {UserIdRole, QByteArrayLiteral("userId")},
        {GenerationRole, QByteArrayLiteral("generation")},
        {SignerDeviceIdRole, QByteArrayLiteral("signerDeviceId")},
        {DeviceCountRole, QByteArrayLiteral("deviceCount")},
        {PinnedAtMsRole, QByteArrayLiteral("pinnedAtMs")},
        {ResetAvailabilityRole, QByteArrayLiteral("resetAvailability")},
    };
}

bool TrustModel::loading() const noexcept
{
    return m_loading;
}

QString TrustModel::lastError() const
{
    return m_lastError;
}

TrustModel::RetryAction TrustModel::retryAction() const noexcept
{
    return m_retryAction;
}

QString TrustModel::confirmationUserId() const
{
    return m_resetConfirmation
        ? m_resetConfirmation->targetUserId
        : QString {};
}

QString TrustModel::pendingResetUserId() const
{
    return m_pendingResetUserId;
}

bool TrustModel::refresh()
{
    clearError();
    ++m_refreshGeneration;
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_pendingRefresh = PendingRefresh {
        .requestId = requestId,
        .generation = m_refreshGeneration,
    };
    m_loading = true;
    notifyStateChanged();
    const auto dispatched = sendCommand({
        {QStringLiteral("type"), QStringLiteral("trust.refresh")},
        {QStringLiteral("requestId"), requestId},
    });
    if (dispatched) {
        return true;
    }
    if (m_pendingRefresh && m_pendingRefresh->requestId == requestId) {
        m_pendingRefresh.reset();
        m_loading = false;
        setError(
            QStringLiteral("Couldn't ask the runtime for pinned identities."),
            RetryAction::Refresh);
    }
    return false;
}

bool TrustModel::requestResetConfirmation(const QString& userId)
{
    if (!m_authenticated || !m_accountContext
        || m_accountContext->userId.isEmpty() || m_resetConfirmation
        || !m_pendingResetUserId.isEmpty() || !hasPin(userId)) {
        return false;
    }
    m_resetConfirmation = ResetConfirmation {
        .account = *m_accountContext,
        .targetUserId = userId,
    };
    notifyStateChanged();
    return true;
}

void TrustModel::cancelResetConfirmation()
{
    if (!m_resetConfirmation) {
        return;
    }
    m_resetConfirmation.reset();
    notifyStateChanged();
}

bool TrustModel::confirmReset()
{
    if (!m_resetConfirmation || !m_accountContext
        || m_resetConfirmation->account.userId != m_accountContext->userId
        || m_resetConfirmation->account.epoch != m_accountContext->epoch
        || !hasPin(m_resetConfirmation->targetUserId)
        || !m_pendingResetUserId.isEmpty()) {
        m_resetConfirmation.reset();
        notifyStateChanged();
        return false;
    }
    const auto userId = m_resetConfirmation->targetUserId;
    m_resetConfirmation.reset();
    dispatchReset(userId);
    return m_pendingResetUserId == userId;
}

bool TrustModel::retry()
{
    if (m_retryAction == RetryAction::Refresh) {
        return refresh();
    }
    if (m_retryAction == RetryAction::Reset && !m_retryUserId.isEmpty()) {
        const auto userId = m_retryUserId;
        dispatchReset(userId);
        return m_pendingResetUserId == userId;
    }
    return false;
}

void TrustModel::clearError()
{
    if (m_lastError.isEmpty() && m_retryAction == RetryAction::None
        && m_retryUserId.isEmpty()) {
        return;
    }
    m_lastError.clear();
    m_retryAction = RetryAction::None;
    m_retryUserId.clear();
    notifyStateChanged();
}

TrustModel::ResetAvailability TrustModel::resetAvailability(
    const QString& userId) const
{
    if (!m_authenticated || !m_accountContext
        || m_accountContext->userId.isEmpty() || !hasPin(userId)) {
        return ResetAvailability::Unavailable;
    }
    if (!m_pendingResetUserId.isEmpty()) {
        return m_pendingResetUserId == userId
            ? ResetAvailability::Resetting
            : ResetAvailability::Unavailable;
    }
    if (m_resetConfirmation) {
        if (m_resetConfirmation->account.userId != m_accountContext->userId
            || m_resetConfirmation->account.epoch != m_accountContext->epoch) {
            return ResetAvailability::Unavailable;
        }
        return m_resetConfirmation->targetUserId == userId
            ? ResetAvailability::Confirming
            : ResetAvailability::Unavailable;
    }
    return ResetAvailability::Available;
}

void TrustModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = optionalString(object, QStringLiteral("type"));
    if (type != QStringLiteral("auth.ready")
        && type != QStringLiteral("auth.required")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        emit decodeError(QStringLiteral("Authentication context has no exact account epoch."));
        return;
    }
    activateAccount(
        type == QStringLiteral("auth.ready")
            ? optionalString(object, QStringLiteral("userId"))
            : QString {},
        *epoch,
        type == QStringLiteral("auth.ready"));
}

void TrustModel::ingestTrustEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Trust event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(QStringLiteral("Trust event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        emit decodeError(QStringLiteral("Trust event has no exact account epoch."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {
            .userId = optionalString(object, QStringLiteral("accountUserId")),
            .epoch = *epoch,
        },
        std::move(json));
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(QStringLiteral("Future trust event exceeds the ABI frame limit."));
    }
    if (admission != AccountEventAdmission::Current) {
        return;
    }
    applyTrustEvent(object);
}

void TrustModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    clearAccountState();
}

void TrustModel::activateAccount(
    QString userId,
    const quint64 epoch,
    const bool authenticated)
{
    const AccountContext account {.userId = std::move(userId), .epoch = epoch};
    auto activation = m_accountFence.activate(account);
    if (!activation.accepted) {
        return;
    }
    if (activation.changed || m_authenticated != authenticated) {
        clearAccountState();
    }
    m_accountContext = account;
    m_authenticated = authenticated;
    if (activation.changed && authenticated) {
        (void)refresh();
    }
    for (auto& json : activation.pendingEvents) {
        ingestTrustEvent(std::move(json));
    }
    notifyStateChanged();
}

void TrustModel::applyTrustEvent(const QJsonObject& object)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        emit decodeError(QStringLiteral("Trust event has no type."));
        return;
    }
    if (*type == QStringLiteral("trust.snapshot")) {
        const auto requestId = requiredString(object, QStringLiteral("requestId"));
        const auto entries = object.value(QStringLiteral("pins"));
        if (!requestId || !entries.isArray()) {
            emit decodeError(QStringLiteral("Trust snapshot is incomplete."));
            return;
        }
        if (!m_pendingRefresh || m_pendingRefresh->requestId != *requestId) {
            return;
        }
        QVector<Pin> pins;
        QSet<QString> userIds;
        for (const auto& entry : entries.toArray()) {
            if (!entry.isObject()) {
                emit decodeError(QStringLiteral("Trust snapshot contains an invalid row."));
                return;
            }
            auto pin = decodePin(entry.toObject());
            if (!pin || userIds.contains(pin->userId)) {
                emit decodeError(QStringLiteral("Trust snapshot contains an invalid row."));
                return;
            }
            userIds.insert(pin->userId);
            pins.push_back(std::move(*pin));
        }
        const auto refresh = *m_pendingRefresh;
        m_pendingRefresh.reset();
        m_loading = false;
        replacePins(pins);
        reconcilePendingReset(pins, refresh.generation);
        notifyStateChanged();
        return;
    }
    if (*type == QStringLiteral("trust.reset")) {
        const auto requestId = requiredString(object, QStringLiteral("requestId"));
        const auto userId = requiredString(object, QStringLiteral("userId"));
        const auto cleared = object.value(QStringLiteral("cleared"));
        if (!requestId || !userId || !cleared.isBool()) {
            emit decodeError(QStringLiteral("Trust reset result is invalid."));
            return;
        }
        if (*requestId != m_pendingResetRequestId
            || *userId != m_pendingResetUserId) {
            return;
        }
        m_pendingResetRequestId.clear();
        m_pendingResetUserId.clear();
        m_resetStartedAfterRefreshGeneration.reset();
        if (cleared.toBool()) {
            removePin(*userId);
            if (m_resetConfirmation
                && m_resetConfirmation->targetUserId == *userId) {
                m_resetConfirmation.reset();
            }
            clearError();
        } else {
            setError(
                QStringLiteral(
                    "The pin was already absent. Refreshing trust state is required."),
                RetryAction::Refresh);
        }
        notifyStateChanged();
        return;
    }
    if (*type == QStringLiteral("trust.error")) {
        const auto operation = requiredString(object, QStringLiteral("operation"));
        const auto message = requiredString(object, QStringLiteral("message"));
        const auto requestIdValue = object.value(QStringLiteral("requestId"));
        const auto userIdValue = object.value(QStringLiteral("userId"));
        const auto validOptionalString = [](const QJsonValue& value) {
            return value.isUndefined() || value.isNull() || value.isString();
        };
        if (!operation || !message || !validOptionalString(requestIdValue)
            || !validOptionalString(userIdValue)) {
            emit decodeError(QStringLiteral("Trust error event is invalid."));
            return;
        }
        const auto requestId =
            requestIdValue.isString() ? requestIdValue.toString() : QString {};
        const auto userId =
            userIdValue.isString() ? userIdValue.toString() : QString {};
        if (*operation == QStringLiteral("reset")) {
            if (requestId.isEmpty() || userId.isEmpty()
                || requestId != m_pendingResetRequestId
                || userId != m_pendingResetUserId) {
                return;
            }
            m_pendingResetRequestId.clear();
            m_pendingResetUserId.clear();
            m_resetStartedAfterRefreshGeneration.reset();
            setError(
                QStringLiteral("Couldn't update pinned identities. ") + *message,
                RetryAction::Reset,
                userId);
            return;
        }
        if (*operation == QStringLiteral("refresh")) {
            if (requestId.isEmpty() || !m_pendingRefresh
                || requestId != m_pendingRefresh->requestId) {
                return;
            }
            m_pendingRefresh.reset();
        }
        m_loading = false;
        setError(
            QStringLiteral("Couldn't update pinned identities. ") + *message,
            RetryAction::Refresh);
    }
}

void TrustModel::clearAccountState()
{
    m_accountContext.reset();
    m_pendingRefresh.reset();
    m_resetConfirmation.reset();
    m_resetStartedAfterRefreshGeneration.reset();
    m_pendingResetRequestId.clear();
    m_pendingResetUserId.clear();
    m_lastError.clear();
    m_retryUserId.clear();
    m_retryAction = RetryAction::None;
    m_loading = false;
    m_authenticated = false;
    replacePins({});
    notifyStateChanged();
}

void TrustModel::dispatchReset(const QString& userId)
{
    if (!m_authenticated || !m_accountContext
        || m_accountContext->userId.isEmpty()
        || !m_pendingResetUserId.isEmpty() || !hasPin(userId)) {
        return;
    }
    clearError();
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_pendingResetRequestId = requestId;
    m_pendingResetUserId = userId;
    m_resetStartedAfterRefreshGeneration = m_refreshGeneration;
    notifyStateChanged();
    const auto dispatched = sendCommand({
        {QStringLiteral("type"), QStringLiteral("trust.reset")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("userId"), userId},
    });
    if (dispatched) {
        return;
    }
    if (m_pendingResetRequestId == requestId && m_pendingResetUserId == userId) {
        m_pendingResetRequestId.clear();
        m_pendingResetUserId.clear();
        m_resetStartedAfterRefreshGeneration.reset();
        setError(
            QStringLiteral("Couldn't send the trust reset to the runtime."),
            RetryAction::Reset,
            userId);
    }
}

void TrustModel::reconcilePendingReset(
    const QVector<Pin>& pins,
    const quint64 snapshotGeneration)
{
    if (m_pendingResetUserId.isEmpty()) {
        clearError();
        return;
    }
    if (!m_resetStartedAfterRefreshGeneration
        || snapshotGeneration <= *m_resetStartedAfterRefreshGeneration) {
        return;
    }
    const auto userId = m_pendingResetUserId;
    m_pendingResetRequestId.clear();
    m_pendingResetUserId.clear();
    m_resetStartedAfterRefreshGeneration.reset();
    if (std::ranges::find(pins, userId, &Pin::userId) != pins.end()) {
        setError(
            QStringLiteral(
                "Couldn't confirm the trust reset. The pinned identity is still present."),
            RetryAction::Reset,
            userId);
    } else {
        clearError();
    }
}

void TrustModel::replacePins(QVector<Pin> pins)
{
    std::ranges::sort(pins, {}, &Pin::userId);
    beginResetModel();
    const auto changed = m_pins.size() != pins.size();
    m_pins = std::move(pins);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
}

void TrustModel::removePin(const QString& userId)
{
    const auto found = std::ranges::find(m_pins, userId, &Pin::userId);
    if (found == m_pins.end()) {
        return;
    }
    const auto row = static_cast<int>(std::distance(m_pins.begin(), found));
    beginRemoveRows({}, row, row);
    m_pins.erase(found);
    endRemoveRows();
    emit countChanged();
}

void TrustModel::notifyStateChanged()
{
    emit stateChanged();
    if (!m_pins.isEmpty()) {
        emit dataChanged(
            index(0),
            index(m_pins.size() - 1),
            {ResetAvailabilityRole});
    }
}

void TrustModel::setError(
    QString message,
    const RetryAction retryAction,
    QString retryUserId)
{
    m_lastError = std::move(message);
    m_retryAction = retryAction;
    m_retryUserId = std::move(retryUserId);
    notifyStateChanged();
}

bool TrustModel::hasPin(const QString& userId) const
{
    return std::ranges::find(m_pins, userId, &Pin::userId) != m_pins.end();
}

CommandDispatcher::Result TrustModel::sendCommand(const QJsonObject& command)
{
    const auto json = QJsonDocument(command).toJson(QJsonDocument::Compact);
    return m_dispatcher.send(CommandLane::Trust, json);
}

std::optional<TrustModel::Pin> TrustModel::decodePin(const QJsonObject& object)
{
    constexpr quint64 maximumExactJsonInteger = 9'007'199'254'740'991ULL;
    const auto userId = requiredString(object, QStringLiteral("userId"));
    const auto generation =
        unsignedInteger(object.value(QStringLiteral("generation")), maximumExactJsonInteger);
    const auto signerDeviceId =
        requiredString(object, QStringLiteral("signerDeviceId"));
    const auto deviceCount = unsignedInteger(
        object.value(QStringLiteral("deviceCount")),
        std::numeric_limits<quint32>::max());
    const auto pinnedAtMs = signedInteger(object.value(QStringLiteral("pinnedAtMs")));
    if (!userId || !generation || !signerDeviceId || !deviceCount || !pinnedAtMs) {
        return std::nullopt;
    }
    return Pin {
        .userId = *userId,
        .generation = *generation,
        .signerDeviceId = *signerDeviceId,
        .deviceCount = static_cast<quint32>(*deviceCount),
        .pinnedAtMs = *pinnedAtMs,
    };
}

} // namespace kodosi
