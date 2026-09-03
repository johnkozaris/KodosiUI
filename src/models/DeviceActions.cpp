#include "models/DeviceActions.hpp"

#include <QJsonDocument>

#include <utility>

namespace kodosi {

DeviceActions::DeviceActions(
    CommandDispatcher& dispatcher,
    DevicesModel& devices,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_devices(devices)
{
    connect(
        &devices,
        &DevicesModel::operationError,
        this,
        [this](
            const QString& operation,
            const QString& message,
            const QString& userCode) {
            m_lastOperation = operation;
            m_lastUserCode = userCode;
            m_lastError = message;
            emit stateChanged();
        });
    connect(
        &devices,
        &DevicesModel::authorityChanged,
        this,
        &DeviceActions::clearError);
}

QString DeviceActions::lastError() const
{
    return m_lastError;
}

QString DeviceActions::lastOperation() const
{
    return m_lastOperation;
}

QString DeviceActions::lastUserCode() const
{
    return m_lastUserCode;
}

bool DeviceActions::refresh()
{
    return send({
        {QStringLiteral("type"), QStringLiteral("devices.refresh")},
    });
}

bool DeviceActions::revoke(const QString& deviceId)
{
    if (deviceId.isEmpty() || deviceId == m_devices.selfDeviceId()
        || !m_devices.containsDevice(deviceId)
        || !m_devices.localDeviceEnrolled()
        || m_devices.inventoryState() != DevicesModel::InventoryState::Fresh) {
        m_lastOperation = QStringLiteral("revoke");
        m_lastUserCode.clear();
        m_lastError =
            QStringLiteral("The selected device is not currently revocable.");
        emit stateChanged();
        return false;
    }
    return send({
        {QStringLiteral("type"), QStringLiteral("devices.revoke")},
        {QStringLiteral("deviceId"), deviceId},
    });
}

bool DeviceActions::approveLink(const QString& userCode)
{
    const auto canonical = m_devices.normalizeUserCode(userCode);
    if (!m_devices.isCanonicalUserCode(canonical)
        || !m_devices.localDeviceEnrolled()
        || m_devices.inventoryState() != DevicesModel::InventoryState::Fresh) {
        m_lastOperation = QStringLiteral("link.approve");
        m_lastUserCode = m_devices.isCanonicalUserCode(canonical)
            ? canonical
            : QString {};
        m_lastError =
            QStringLiteral("Enter an eight-character device code like BCDF-2345.");
        emit stateChanged();
        return false;
    }
    return send({
        {QStringLiteral("type"), QStringLiteral("devices.link.approve")},
        {QStringLiteral("userCode"), canonical},
    });
}

bool DeviceActions::startSelfLink()
{
    if (!m_devices.hasEnrollmentState() || m_devices.localDeviceEnrolled()) {
        m_lastOperation = QStringLiteral("link.startSelf");
        m_lastUserCode.clear();
        m_lastError =
            QStringLiteral("This device cannot start a self-link request.");
        emit stateChanged();
        return false;
    }
    return send({
        {QStringLiteral("type"), QStringLiteral("devices.link.startSelf")},
    });
}

bool DeviceActions::cancelSelfLink()
{
    return send({
        {QStringLiteral("type"), QStringLiteral("devices.link.cancelSelf")},
    });
}

void DeviceActions::clearError()
{
    if (m_lastError.isEmpty() && m_lastOperation.isEmpty()
        && m_lastUserCode.isEmpty()) {
        return;
    }
    m_lastError.clear();
    m_lastOperation.clear();
    m_lastUserCode.clear();
    emit stateChanged();
}

bool DeviceActions::send(QJsonObject command)
{
    m_lastError.clear();
    m_lastOperation.clear();
    m_lastUserCode.clear();
    const auto commandType =
        command.value(QStringLiteral("type")).toString();
    const auto json = QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::Devices, json)) {
        emit stateChanged();
        return true;
    }
    m_lastOperation = commandType.startsWith(QStringLiteral("devices."))
        ? commandType.mid(8)
        : commandType;
    m_lastError =
        QStringLiteral("The runtime did not accept the device request.");
    emit stateChanged();
    return false;
}

} // namespace kodosi
