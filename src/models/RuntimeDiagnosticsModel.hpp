#pragma once

#include "bridge/RuntimeBridge.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace kodosi {

class RuntimeDiagnosticsModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool runtimeRunning READ runtimeRunning NOTIFY stateChanged)
    Q_PROPERTY(bool systemReady READ systemReady NOTIFY stateChanged)
    Q_PROPERTY(quint32 protocolVersion READ protocolVersion CONSTANT)
    Q_PROPERTY(QString runtimeContract READ runtimeContract CONSTANT)
    Q_PROPERTY(QString runtimeError READ runtimeError NOTIFY stateChanged)
    Q_PROPERTY(
        bool hasRuntimeHealth
        READ hasRuntimeHealth
        NOTIFY stateChanged)
    Q_PROPERTY(
        CleanupState cleanupState
        READ cleanupState
        NOTIFY stateChanged)
    Q_PROPERTY(
        quint32 cleanupPendingCount
        READ cleanupPendingCount
        NOTIFY stateChanged)
    Q_PROPERTY(
        quint32 cleanupQuarantinedCount
        READ cleanupQuarantinedCount
        NOTIFY stateChanged)
    Q_PROPERTY(
        QString cleanupMessage
        READ cleanupMessage
        NOTIFY stateChanged)

public:
    enum class CleanupState {
        Waiting,
        Healthy,
        Quarantined,
        Unavailable,
        Unknown,
    };
    Q_ENUM(CleanupState)

    explicit RuntimeDiagnosticsModel(
        RuntimeBridge& runtime,
        QObject* parent = nullptr);

    [[nodiscard]] bool runtimeRunning() const noexcept;
    [[nodiscard]] bool systemReady() const noexcept;
    [[nodiscard]] quint32 protocolVersion() const noexcept;
    [[nodiscard]] QString runtimeContract() const;
    [[nodiscard]] QString runtimeError() const;
    [[nodiscard]] bool hasRuntimeHealth() const noexcept;
    [[nodiscard]] CleanupState cleanupState() const noexcept;
    [[nodiscard]] quint32 cleanupPendingCount() const noexcept;
    [[nodiscard]] quint32 cleanupQuarantinedCount() const noexcept;
    [[nodiscard]] QString cleanupMessage() const;

    Q_INVOKABLE void clearRuntimeError();

public slots:
    void ingestSystemEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void decodeError(QString message);

private:
    RuntimeBridge& m_runtime;
    QString m_runtimeError;
    QString m_cleanupMessage;
    CleanupState m_cleanupState = CleanupState::Waiting;
    quint32 m_cleanupPendingCount = 0;
    quint32 m_cleanupQuarantinedCount = 0;
    quint32 m_protocolVersion = 0;
    bool m_systemReady = false;
    bool m_hasRuntimeHealth = false;
};

} // namespace kodosi
