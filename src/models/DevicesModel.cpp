#include "models/DevicesModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

std::optional<QString> stringField(
    const QJsonObject& object,
    const QString& key,
    const bool allowEmpty = false)
{
    const auto value = object.value(key);
    if (!value.isString() || (!allowEmpty && value.toString().isEmpty())) {
        return std::nullopt;
    }
    return value.toString();
}

QString optionalString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    return value.isString() ? value.toString() : QString {};
}

} // namespace

DeviceLinkRequestsModel::DeviceLinkRequestsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int DeviceLinkRequestsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_requests.size();
}

QVariant DeviceLinkRequestsModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_requests.size()) {
        return {};
    }
    const auto& request = m_requests[index.row()];
    switch (role) {
    case UserCodeRole:
        return request.userCode;
    case DeviceLabelRole:
        return request.deviceLabel;
    case ExpiresAtRole:
        return request.expiresAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceLinkRequestsModel::roleNames() const
{
    return {
        {UserCodeRole, QByteArrayLiteral("userCode")},
        {DeviceLabelRole, QByteArrayLiteral("deviceLabel")},
        {ExpiresAtRole, QByteArrayLiteral("expiresAt")},
    };
}

quint64 DeviceLinkRequestsModel::revision() const noexcept
{
    return m_revision;
}

QVariantMap DeviceLinkRequestsModel::presentationAt(const int row) const
{
    if (row < 0 || row >= m_requests.size()) {
        return {};
    }
    const auto& request = m_requests.at(row);
    return {
        {QStringLiteral("userCode"), request.userCode},
        {QStringLiteral("deviceLabel"), request.deviceLabel},
        {QStringLiteral("expiresAt"), request.expiresAt},
    };
}

void DeviceLinkRequestsModel::replace(QVector<Request> requests)
{
    std::ranges::sort(requests, [](const Request& lhs, const Request& rhs) {
        return lhs.expiresAt == rhs.expiresAt
            ? lhs.userCode < rhs.userCode
            : lhs.expiresAt < rhs.expiresAt;
    });
    beginResetModel();
    const auto changed = m_requests.size() != requests.size();
    m_requests = std::move(requests);
    endResetModel();
    ++m_revision;
    emit stateChanged();
    if (changed) {
        emit countChanged();
    }
}

void DeviceLinkRequestsModel::upsert(Request request)
{
    const auto found =
        std::ranges::find(m_requests, request.userCode, &Request::userCode);
    if (found != m_requests.end()) {
        auto requests = m_requests;
        *std::ranges::find(requests, request.userCode, &Request::userCode) =
            std::move(request);
        replace(std::move(requests));
    } else {
        const auto insertion = std::ranges::lower_bound(
            m_requests,
            request,
            [](const Request& lhs, const Request& rhs) {
                return lhs.expiresAt == rhs.expiresAt
                    ? lhs.userCode < rhs.userCode
                    : lhs.expiresAt < rhs.expiresAt;
            });
        const auto row = static_cast<int>(std::distance(m_requests.begin(), insertion));
        beginInsertRows({}, row, row);
        m_requests.insert(insertion, std::move(request));
        endInsertRows();
        ++m_revision;
        emit stateChanged();
        emit countChanged();
    }
}

void DeviceLinkRequestsModel::remove(const QString& userCode)
{
    const auto found = std::ranges::find(m_requests, userCode, &Request::userCode);
    if (found == m_requests.end()) {
        return;
    }
    const auto row = static_cast<int>(std::distance(m_requests.begin(), found));
    beginRemoveRows({}, row, row);
    m_requests.erase(found);
    endRemoveRows();
    ++m_revision;
    emit stateChanged();
    emit countChanged();
}

void DeviceLinkRequestsModel::removeExpired(const QDateTime& now)
{
    for (auto row = m_requests.size() - 1; row >= 0; --row) {
        if (m_requests[row].expiresAt <= now) {
            beginRemoveRows({}, row, row);
            m_requests.removeAt(row);
            endRemoveRows();
            ++m_revision;
            emit stateChanged();
            emit countChanged();
        }
    }
}

std::optional<QDateTime> DeviceLinkRequestsModel::nearestExpiry() const
{
    if (m_requests.isEmpty()) {
        return std::nullopt;
    }
    return m_requests.front().expiresAt;
}

DevicesModel::DevicesModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_pendingLinks(this)
{
    m_expiryTimer.setSingleShot(true);
    connect(
        &m_pendingLinks,
        &DeviceLinkRequestsModel::stateChanged,
        this,
        &DevicesModel::stateChanged);
    connect(&m_expiryTimer, &QTimer::timeout, this, [this] {
        pruneExpiredLinks(QDateTime::currentDateTimeUtc());
    });
}

int DevicesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

QVariant DevicesModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size()) {
        return {};
    }
    const auto& device = m_devices[index.row()];
    switch (role) {
    case DeviceIdRole:
        return device.deviceId;
    case LabelRole:
        return device.label;
    case CertSignerDeviceIdRole:
        return device.certSignerDeviceId;
    case CertIssuedAtMsRole:
        return QVariant::fromValue(device.certIssuedAtMs);
    case IsSelfRole:
        return device.deviceId == m_selfDeviceId;
    default:
        return {};
    }
}

QHash<int, QByteArray> DevicesModel::roleNames() const
{
    return {
        {DeviceIdRole, QByteArrayLiteral("deviceId")},
        {LabelRole, QByteArrayLiteral("label")},
        {CertSignerDeviceIdRole, QByteArrayLiteral("certSignerDeviceId")},
        {CertIssuedAtMsRole, QByteArrayLiteral("certIssuedAtMs")},
        {IsSelfRole, QByteArrayLiteral("isSelf")},
    };
}

QString DevicesModel::selfDeviceId() const
{
    return m_selfDeviceId;
}

QString DevicesModel::selfDeviceLabel() const
{
    const auto* device = selfDevice();
    return device == nullptr ? QString {} : device->label;
}

QString DevicesModel::selfCertSignerDeviceId() const
{
    const auto* device = selfDevice();
    return device == nullptr ? QString {} : device->certSignerDeviceId;
}

QDateTime DevicesModel::selfCertIssuedAt() const
{
    const auto* device = selfDevice();
    return device == nullptr || device->certIssuedAtMs == 0
        ? QDateTime {}
        : QDateTime::fromMSecsSinceEpoch(
              static_cast<qint64>(device->certIssuedAtMs),
              QTimeZone::UTC);
}

bool DevicesModel::hasEnrollmentState() const noexcept
{
    return m_hasEnrollmentState;
}

bool DevicesModel::localDeviceEnrolled() const noexcept
{
    return m_localDeviceEnrolled;
}

DevicesModel::InventoryState DevicesModel::inventoryState() const noexcept
{
    return m_inventoryState;
}

DeviceLinkRequestsModel* DevicesModel::pendingLinks() noexcept
{
    return &m_pendingLinks;
}

QVariantList DevicesModel::pendingLinkPresentations() const
{
    QVariantList presentations;
    presentations.reserve(m_pendingLinks.rowCount());
    for (auto row = 0; row < m_pendingLinks.rowCount(); ++row) {
        presentations.push_back(m_pendingLinks.presentationAt(row));
    }
    return presentations;
}

bool DevicesModel::hasSelfLinkPending() const noexcept
{
    return !m_selfLinkUserCode.isEmpty() && m_selfLinkExpiresAt.isValid();
}

QString DevicesModel::selfLinkUserCode() const
{
    return m_selfLinkUserCode;
}

QDateTime DevicesModel::selfLinkExpiresAt() const
{
    return m_selfLinkExpiresAt;
}

QString DevicesModel::lastResolvedUserCode() const
{
    return m_lastResolvedUserCode;
}

DevicesModel::LinkOutcome DevicesModel::lastLinkOutcome() const noexcept
{
    return m_lastLinkOutcome;
}

QString DevicesModel::lastLinkOutcomeMessage() const
{
    switch (m_lastLinkOutcome) {
    case LinkOutcome::LinkNone:
        return {};
    case LinkOutcome::LinkApproved:
        return QStringLiteral("Device linked successfully.");
    case LinkOutcome::LinkCancelled:
        return QStringLiteral("The device link was cancelled.");
    case LinkOutcome::LinkUnknown:
        return QStringLiteral("The device link finished with an unknown status.");
    }
    return {};
}

bool DevicesModel::lastLinkApproved() const noexcept
{
    return m_lastLinkOutcome == LinkOutcome::LinkApproved;
}

DevicesModel::SelfLinkOutcome DevicesModel::selfLinkOutcome() const noexcept
{
    return m_selfLinkOutcome;
}

QString DevicesModel::selfLinkOutcomeMessage() const
{
    switch (m_selfLinkOutcome) {
    case SelfLinkOutcome::SelfLinkNone:
        return {};
    case SelfLinkOutcome::SelfLinkApproved:
        return QStringLiteral("Device linked successfully.");
    case SelfLinkOutcome::SelfLinkCancelled:
        return QStringLiteral("The device link was cancelled.");
    case SelfLinkOutcome::SelfLinkExpired:
        return QStringLiteral("The link code expired. Generate a new code and try again.");
    case SelfLinkOutcome::SelfLinkFailed:
        return QStringLiteral("The device could not be linked.");
    case SelfLinkOutcome::SelfLinkUnknown:
        return QStringLiteral("The device link finished with an unknown status.");
    }
    return {};
}

bool DevicesModel::selfLinkApproved() const noexcept
{
    return m_selfLinkOutcome == SelfLinkOutcome::SelfLinkApproved;
}

QString DevicesModel::normalizeUserCode(const QString& value) const
{
    return normalizeCode(value);
}

bool DevicesModel::isCanonicalUserCode(const QString& value) const
{
    return isCanonicalCode(value);
}

bool DevicesModel::containsDevice(const QString& deviceId) const
{
    return std::ranges::find(m_devices, deviceId, &Device::deviceId)
        != m_devices.end();
}

void DevicesModel::dismissLinkOutcome()
{
    if (m_lastLinkOutcome == LinkOutcome::LinkNone
        && m_lastResolvedUserCode.isEmpty()) {
        return;
    }
    m_lastLinkOutcome = LinkOutcome::LinkNone;
    m_lastResolvedUserCode.clear();
    emit stateChanged();
}

void DevicesModel::dismissSelfLinkOutcome()
{
    if (m_selfLinkOutcome == SelfLinkOutcome::SelfLinkNone) {
        return;
    }
    m_selfLinkOutcome = SelfLinkOutcome::SelfLinkNone;
    emit stateChanged();
}

void DevicesModel::ingestAuthEvent(QByteArray json)
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
        *epoch);
}

void DevicesModel::ingestDevicesEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Devices event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(QStringLiteral("Devices event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        emit decodeError(QStringLiteral("Devices event has no exact account epoch."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {
            .userId = optionalString(object, QStringLiteral("accountUserId")),
            .epoch = *epoch,
        },
        std::move(json));
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(QStringLiteral("Future devices event exceeds the ABI frame limit."));
    }
    if (admission != AccountEventAdmission::Current) {
        return;
    }
    applyDevicesEvent(object);
}

void DevicesModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    clearAccountState();
}

void DevicesModel::activateAccount(QString userId, const quint64 epoch)
{
    auto activation =
        m_accountFence.activate({.userId = std::move(userId), .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    if (activation.changed) {
        clearAccountState();
    }
    for (auto& json : activation.pendingEvents) {
        ingestDevicesEvent(std::move(json));
    }
}

void DevicesModel::applyDevicesEvent(const QJsonObject& object)
{
    const auto type = stringField(object, QStringLiteral("type"));
    if (!type) {
        emit decodeError(QStringLiteral("Devices event has no type."));
        return;
    }
    if (*type == QStringLiteral("devices.list")) {
        const auto selfDeviceId =
            stringField(object, QStringLiteral("selfDeviceId"));
        const auto enrolled = object.value(QStringLiteral("localDeviceEnrolled"));
        const auto entries = object.value(QStringLiteral("devices"));
        if (!selfDeviceId || !enrolled.isBool() || !entries.isArray()) {
            emit decodeError(QStringLiteral("Device inventory is incomplete."));
            return;
        }
        QVector<Device> devices;
        QSet<QString> deviceIds;
        for (const auto& entry : entries.toArray()) {
            if (!entry.isObject()) {
                emit decodeError(QStringLiteral("Device inventory contains an invalid row."));
                return;
            }
            auto device = decodeDevice(entry.toObject());
            if (!device || deviceIds.contains(device->deviceId)) {
                emit decodeError(QStringLiteral("Device inventory contains an invalid row."));
                return;
            }
            deviceIds.insert(device->deviceId);
            devices.push_back(std::move(*device));
        }
        m_selfDeviceId = *selfDeviceId;
        m_hasEnrollmentState = true;
        m_localDeviceEnrolled = enrolled.toBool();
        m_inventoryState = InventoryState::Fresh;
        replaceDevices(std::move(devices));
        emit stateChanged();
        return;
    }
    if (*type == QStringLiteral("devices.link.snapshot")) {
        const auto entries = object.value(QStringLiteral("requests"));
        if (!entries.isArray()) {
            emit decodeError(QStringLiteral("Device-link snapshot is incomplete."));
            return;
        }
        QVector<DeviceLinkRequestsModel::Request> requests;
        QSet<QString> userCodes;
        for (const auto& entry : entries.toArray()) {
            if (!entry.isObject()) {
                emit decodeError(QStringLiteral("Device-link snapshot contains an invalid row."));
                return;
            }
            auto request = decodeRequest(entry.toObject());
            if (!request || userCodes.contains(request->userCode)) {
                emit decodeError(QStringLiteral("Device-link snapshot contains an invalid row."));
                return;
            }
            userCodes.insert(request->userCode);
            requests.push_back(std::move(*request));
        }
        m_pendingLinks.replace(std::move(requests));
        pruneExpiredLinks(QDateTime::currentDateTimeUtc());
        return;
    }
    if (*type == QStringLiteral("devices.link.requested")) {
        auto request = decodeRequest(object);
        if (!request) {
            emit decodeError(QStringLiteral("Device-link request is invalid."));
            return;
        }
        m_pendingLinks.upsert(std::move(*request));
        pruneExpiredLinks(QDateTime::currentDateTimeUtc());
        return;
    }
    if (*type == QStringLiteral("devices.link.resolved")) {
        const auto rawCode = stringField(object, QStringLiteral("userCode"));
        const auto outcome = stringField(object, QStringLiteral("outcome"));
        if (!rawCode || !outcome) {
            emit decodeError(QStringLiteral("Device-link resolution is invalid."));
            return;
        }
        const auto userCode = normalizeCode(*rawCode);
        if (!isCanonicalCode(userCode)) {
            emit decodeError(QStringLiteral("Device-link resolution has an invalid code."));
            return;
        }
        m_pendingLinks.remove(userCode);
        m_lastResolvedUserCode = userCode;
        m_lastLinkOutcome = decodeLinkOutcome(*outcome);
        scheduleExpiry();
        emit stateChanged();
        return;
    }
    if (*type == QStringLiteral("devices.link.selfPending")) {
        const auto rawCode = stringField(object, QStringLiteral("userCode"));
        const auto rawExpiry = stringField(object, QStringLiteral("expiresAt"));
        const auto expiry = rawExpiry ? decodeTimestamp(*rawExpiry) : std::nullopt;
        const auto userCode = rawCode ? normalizeCode(*rawCode) : QString {};
        if (!isCanonicalCode(userCode) || !expiry) {
            emit decodeError(QStringLiteral("Self-link request is invalid."));
            return;
        }
        m_selfLinkUserCode = userCode;
        m_selfLinkExpiresAt = *expiry;
        m_selfLinkOutcome = SelfLinkOutcome::SelfLinkNone;
        pruneExpiredLinks(QDateTime::currentDateTimeUtc());
        emit stateChanged();
        return;
    }
    if (*type == QStringLiteral("devices.link.selfResolved")) {
        const auto outcome = stringField(object, QStringLiteral("outcome"));
        if (!outcome) {
            emit decodeError(QStringLiteral("Self-link resolution is invalid."));
            return;
        }
        m_selfLinkUserCode.clear();
        m_selfLinkExpiresAt = {};
        m_selfLinkOutcome = decodeSelfLinkOutcome(*outcome);
        scheduleExpiry();
        emit stateChanged();
        return;
    }
    if (*type == QStringLiteral("devices.error")) {
        const auto operation = stringField(object, QStringLiteral("operation"));
        const auto message = stringField(object, QStringLiteral("message"));
        if (!operation || !message) {
            emit decodeError(QStringLiteral("Devices error event is invalid."));
            return;
        }
        const auto userCodeValue = object.value(QStringLiteral("userCode"));
        if (!userCodeValue.isUndefined() && !userCodeValue.isNull()
            && !userCodeValue.isString()) {
            emit decodeError(QStringLiteral("Devices error has an invalid correlation code."));
            return;
        }
        auto userCode = userCodeValue.isString()
            ? userCodeValue.toString()
            : QString {};
        if (!userCode.isEmpty()) {
            userCode = normalizeCode(userCode);
            if (!isCanonicalCode(userCode)) {
                emit decodeError(QStringLiteral("Devices error has an invalid correlation code."));
                return;
            }
        }
        if (*operation == QStringLiteral("refresh")
            || *operation == QStringLiteral("discovery.invalidated")) {
            m_inventoryState = InventoryState::Stale;
            emit stateChanged();
        }
        emit operationError(*operation, *message, userCode);
    }
}

void DevicesModel::clearAccountState()
{
    m_expiryTimer.stop();
    m_pendingLinks.replace({});
    m_selfDeviceId.clear();
    m_selfLinkUserCode.clear();
    m_selfLinkExpiresAt = {};
    m_lastResolvedUserCode.clear();
    m_hasEnrollmentState = false;
    m_localDeviceEnrolled = false;
    m_inventoryState = InventoryState::Idle;
    m_lastLinkOutcome = LinkOutcome::LinkNone;
    m_selfLinkOutcome = SelfLinkOutcome::SelfLinkNone;
    replaceDevices({});
    emit authorityChanged();
    emit stateChanged();
}

void DevicesModel::replaceDevices(QVector<Device> devices)
{
    std::ranges::sort(devices, [](const Device& lhs, const Device& rhs) {
        return lhs.label == rhs.label
            ? lhs.deviceId < rhs.deviceId
            : lhs.label < rhs.label;
    });
    beginResetModel();
    const auto changed = m_devices.size() != devices.size();
    m_devices = std::move(devices);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
}

std::optional<DevicesModel::Device> DevicesModel::decodeDevice(
    const QJsonObject& object)
{
    const auto deviceId = stringField(object, QStringLiteral("deviceId"));
    const auto label = stringField(object, QStringLiteral("label"), true);
    const auto signer = stringField(object, QStringLiteral("certSignerDeviceId"));
    const auto issuedAt = object.value(QStringLiteral("certIssuedAtMs"));
    constexpr auto maximumExactJsonInteger = 9'007'199'254'740'991.0;
    if (!deviceId || !label || !signer || !issuedAt.isDouble()) {
        return std::nullopt;
    }
    const auto issuedAtValue = issuedAt.toDouble();
    if (!std::isfinite(issuedAtValue) || issuedAtValue < 0
        || issuedAtValue > maximumExactJsonInteger
        || std::floor(issuedAtValue) != issuedAtValue) {
        return std::nullopt;
    }
    return Device {
        .deviceId = *deviceId,
        .label = *label,
        .certSignerDeviceId = *signer,
        .certIssuedAtMs = static_cast<quint64>(issuedAtValue),
    };
}

std::optional<DeviceLinkRequestsModel::Request> DevicesModel::decodeRequest(
    const QJsonObject& object)
{
    const auto rawCode = stringField(object, QStringLiteral("userCode"));
    const auto label = stringField(object, QStringLiteral("deviceLabel"), true);
    const auto rawExpiry = stringField(object, QStringLiteral("expiresAt"));
    if (!rawCode || !label || !rawExpiry) {
        return std::nullopt;
    }
    const auto userCode = normalizeCode(*rawCode);
    const auto expiresAt = decodeTimestamp(*rawExpiry);
    if (!isCanonicalCode(userCode) || !expiresAt) {
        return std::nullopt;
    }
    return DeviceLinkRequestsModel::Request {
        .userCode = userCode,
        .deviceLabel = *label,
        .expiresAt = *expiresAt,
    };
}

std::optional<QDateTime> DevicesModel::decodeTimestamp(const QString& value)
{
    auto timestamp = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(value, Qt::ISODate);
    }
    return timestamp.isValid() && timestamp.timeSpec() != Qt::LocalTime
        ? std::optional<QDateTime> {timestamp.toUTC()}
        : std::nullopt;
}

DevicesModel::LinkOutcome DevicesModel::decodeLinkOutcome(const QString& value)
{
    if (value == QStringLiteral("approved")) {
        return LinkOutcome::LinkApproved;
    }
    if (value == QStringLiteral("cancelled")) {
        return LinkOutcome::LinkCancelled;
    }
    return LinkOutcome::LinkUnknown;
}

DevicesModel::SelfLinkOutcome DevicesModel::decodeSelfLinkOutcome(
    const QString& value)
{
    if (value == QStringLiteral("approved")) {
        return SelfLinkOutcome::SelfLinkApproved;
    }
    if (value == QStringLiteral("cancelled")) {
        return SelfLinkOutcome::SelfLinkCancelled;
    }
    if (value == QStringLiteral("expired")) {
        return SelfLinkOutcome::SelfLinkExpired;
    }
    if (value == QStringLiteral("failed")) {
        return SelfLinkOutcome::SelfLinkFailed;
    }
    return SelfLinkOutcome::SelfLinkUnknown;
}

QString DevicesModel::normalizeCode(const QString& value)
{
    QString stripped;
    stripped.reserve(value.size());
    for (const auto character : value) {
        if (character.isSpace() || character == QLatin1Char('-')) {
            continue;
        }
        stripped.append(character.toUpper());
    }
    if (stripped.size() == 8) {
        stripped.insert(4, QLatin1Char('-'));
    }
    return stripped;
}

bool DevicesModel::isCanonicalCode(const QString& value)
{
    constexpr auto alphabet = QLatin1StringView("BCDFGHJKMNPQRSTVWXZ23456789");
    if (value.size() != 9 || value.at(4) != QLatin1Char('-')) {
        return false;
    }

    for (auto index = 0; index < value.size(); ++index) {
        if (index == 4) {
            continue;
        }
        if (!alphabet.contains(value.at(index))) {
            return false;
        }
    }
    return true;
}

const DevicesModel::Device* DevicesModel::selfDevice() const
{
    const auto found =
        std::ranges::find(m_devices, m_selfDeviceId, &Device::deviceId);
    return found == m_devices.end() ? nullptr : &*found;
}

void DevicesModel::pruneExpiredLinks(const QDateTime& now)
{
    m_pendingLinks.removeExpired(now);
    if (hasSelfLinkPending() && m_selfLinkExpiresAt <= now) {
        m_selfLinkUserCode.clear();
        m_selfLinkExpiresAt = {};
        m_selfLinkOutcome = SelfLinkOutcome::SelfLinkExpired;
        emit stateChanged();
    }
    scheduleExpiry();
}

void DevicesModel::scheduleExpiry()
{
    auto nearest = m_pendingLinks.nearestExpiry();
    if (hasSelfLinkPending()
        && (!nearest || m_selfLinkExpiresAt < *nearest)) {
        nearest = m_selfLinkExpiresAt;
    }
    if (!nearest) {
        m_expiryTimer.stop();
        return;
    }
    const auto delay = std::clamp<qint64>(
        QDateTime::currentDateTimeUtc().msecsTo(*nearest),
        0,
        std::numeric_limits<int>::max());
    m_expiryTimer.start(static_cast<int>(delay));
}

} // namespace kodosi
