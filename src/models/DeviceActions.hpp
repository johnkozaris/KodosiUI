#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/DevicesModel.hpp"

#include <QObject>
#include <QJsonObject>
#include <QString>

namespace kodosi {

class DeviceActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    DeviceActions(
        CommandDispatcher& dispatcher,
        DevicesModel& devices,
        QObject* parent = nullptr);

    [[nodiscard]] QString lastError() const;

    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] bool revoke(const QString& deviceId);
    Q_INVOKABLE [[nodiscard]] bool approveLink(const QString& userCode);
    Q_INVOKABLE [[nodiscard]] bool startSelfLink();
    Q_INVOKABLE [[nodiscard]] bool cancelSelfLink();
    Q_INVOKABLE void clearError();

signals:
    void stateChanged();

private:
    CommandDispatcher& m_dispatcher;
    DevicesModel& m_devices;
    QString m_lastError;

    [[nodiscard]] bool send(QJsonObject command);
};

} // namespace kodosi
