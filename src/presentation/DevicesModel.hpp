#pragma once

#include <QObject>
#include <QVariantList>

namespace kodosi {
class Workspace;

class DevicesModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList requests READ requests NOTIFY changed)
    Q_PROPERTY(QString approvalCode READ approvalCode NOTIFY changed)
    Q_PROPERTY(bool localDeviceEnrolled READ localDeviceEnrolled NOTIFY changed)
    Q_PROPERTY(QString selfDeviceId READ selfDeviceId NOTIFY changed)

public:
    explicit DevicesModel(Workspace& workspace);

    QVariantList devices() const { return m_devices; }
    QVariantList requests() const { return m_requests; }
    QString approvalCode() const { return m_approvalCode; }
    bool localDeviceEnrolled() const { return m_enrolled; }
    QString selfDeviceId() const { return m_selfDeviceId; }

    Q_INVOKABLE void revoke(const QString& deviceId);
    Q_INVOKABLE void approve(const QString& code);
    Q_INVOKABLE void requestApproval();
    Q_INVOKABLE void cancelApproval();

signals:
    void changed();

private:
    friend class Workspace;

    Workspace& m_workspace;
    QVariantList m_devices;
    QVariantList m_requests;
    QString m_approvalCode;
    QString m_selfDeviceId;
    bool m_enrolled = false;

    void reset();
};
}
