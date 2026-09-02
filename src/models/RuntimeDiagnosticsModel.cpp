#include "models/RuntimeDiagnosticsModel.hpp"

#include <kodosi_runtime.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <limits>
#include <optional>

namespace kodosi {
namespace {

std::optional<quint32> nonNegativeInteger(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (number < 0
        || number > static_cast<double>(std::numeric_limits<quint32>::max())) {
        return std::nullopt;
    }
    const auto integer = static_cast<quint32>(number);
    return static_cast<double>(integer) == number
        ? std::optional<quint32> {integer}
        : std::nullopt;
}

std::optional<RuntimeDiagnosticsModel::CleanupState> decodeCleanupState(
    const QJsonValue& value)
{
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto state = value.toString();
    if (state == QStringLiteral("healthy")) {
        return RuntimeDiagnosticsModel::CleanupState::Healthy;
    }
    if (state == QStringLiteral("quarantined")) {
        return RuntimeDiagnosticsModel::CleanupState::Quarantined;
    }
    if (state == QStringLiteral("unavailable")) {
        return RuntimeDiagnosticsModel::CleanupState::Unavailable;
    }
    return RuntimeDiagnosticsModel::CleanupState::Unknown;
}

std::optional<QString> nullableString(
    const QJsonObject& object,
    const QString& key)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        return QString {};
    }
    if (!value.isString()) {
        return std::nullopt;
    }
    return value.toString();
}

} // namespace

RuntimeDiagnosticsModel::RuntimeDiagnosticsModel(
    RuntimeBridge& runtime,
    QObject* parent)
    : QObject(parent)
    , m_runtime(runtime)
    , m_protocolVersion(kodosi_protocol_version())
{
    connect(
        &m_runtime,
        &RuntimeBridge::runningChanged,
        this,
        [this](const bool running) {
            if (!running) {
                resetRuntimeAuthority();
            } else {
                emit stateChanged();
            }
        });
}

bool RuntimeDiagnosticsModel::runtimeRunning() const noexcept
{
    return m_runtime.isRunning();
}

bool RuntimeDiagnosticsModel::systemReady() const noexcept
{
    return m_systemReady;
}

quint32 RuntimeDiagnosticsModel::protocolVersion() const noexcept
{
    return m_protocolVersion;
}

QString RuntimeDiagnosticsModel::runtimeContract() const
{
    return QStringLiteral("desktop-runtime");
}

QString RuntimeDiagnosticsModel::runtimeError() const
{
    return m_runtimeError;
}

bool RuntimeDiagnosticsModel::hasRuntimeHealth() const noexcept
{
    return m_hasRuntimeHealth;
}

RuntimeDiagnosticsModel::CleanupState
RuntimeDiagnosticsModel::cleanupState() const noexcept
{
    return m_cleanupState;
}

quint32 RuntimeDiagnosticsModel::cleanupPendingCount() const noexcept
{
    return m_cleanupPendingCount;
}

quint32 RuntimeDiagnosticsModel::cleanupQuarantinedCount() const noexcept
{
    return m_cleanupQuarantinedCount;
}

QString RuntimeDiagnosticsModel::cleanupMessage() const
{
    return m_cleanupMessage;
}

void RuntimeDiagnosticsModel::clearRuntimeError()
{
    if (m_runtimeError.isEmpty()) {
        return;
    }
    m_runtimeError.clear();
    emit stateChanged();
}

void RuntimeDiagnosticsModel::ingestSystemEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("System event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type"));
    if (!type.isString() || type.toString().isEmpty()) {
        emit decodeError(QStringLiteral("System event has no type."));
        return;
    }
    if (type.toString() == QStringLiteral("heartbeat")) {
        if (!m_systemReady) {
            m_systemReady = true;
            emit stateChanged();
        }
        return;
    }
    if (type.toString() == QStringLiteral("error")) {
        const auto message = object.value(QStringLiteral("message"));
        if (!message.isString() || message.toString().isEmpty()) {
            emit decodeError(QStringLiteral("Runtime error event is invalid."));
            return;
        }
        m_runtimeError = message.toString();
        emit stateChanged();
        return;
    }
    if (type.toString() != QStringLiteral("runtime.health")) {
        return;
    }

    const auto cleanupValue =
        object.value(QStringLiteral("collaborationCleanup"));
    if (!cleanupValue.isObject()) {
        emit decodeError(QStringLiteral("Runtime health event is invalid."));
        return;
    }
    const auto cleanup = cleanupValue.toObject();
    const auto state =
        decodeCleanupState(cleanup.value(QStringLiteral("state")));
    const auto pending =
        nonNegativeInteger(cleanup.value(QStringLiteral("pendingCount")));
    const auto quarantined =
        nonNegativeInteger(cleanup.value(QStringLiteral("quarantinedCount")));
    const auto message =
        nullableString(cleanup, QStringLiteral("message"));
    if (!state || !pending || !quarantined || !message) {
        emit decodeError(QStringLiteral("Runtime health event is invalid."));
        return;
    }

    m_cleanupState = *state;
    m_cleanupPendingCount = *pending;
    m_cleanupQuarantinedCount = *quarantined;
    m_cleanupMessage = *message;
    m_hasRuntimeHealth = true;
    emit stateChanged();
}

void RuntimeDiagnosticsModel::resetRuntimeAuthority()
{
    m_systemReady = false;
    m_hasRuntimeHealth = false;
    m_runtimeError.clear();
    m_cleanupState = CleanupState::Waiting;
    m_cleanupPendingCount = 0;
    m_cleanupQuarantinedCount = 0;
    m_cleanupMessage.clear();
    emit stateChanged();
}

} // namespace kodosi
