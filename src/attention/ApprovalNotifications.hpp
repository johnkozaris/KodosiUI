#pragma once

#include "models/DesktopSettings.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/SessionCatalogModel.hpp"
#include "platform/DesktopNotificationDriver.hpp"

#include <QObject>
#include <QString>
#include <QStringView>

#include <functional>

namespace kodosi {

class ApprovalNotifications final : public QObject {
    Q_OBJECT

public:
    using ApplicationActive = std::function<bool()>;

    ApprovalNotifications(
        PendingPermissionsModel& pendingPermissions,
        SessionCatalogModel& sessions,
        DesktopSettings& settings,
        DesktopNotificationDriver& driver,
        ApplicationActive applicationActive = {},
        QObject* parent = nullptr);

    [[nodiscard]] static bool oneTapDecidable(QStringView risk);
    [[nodiscard]] static QString notificationBody(
        QStringView risk,
        QStringView toolInputSummary);

signals:
    void reviewRequested(
        QString identityToken,
        QString sessionId,
        QString activationToken);
    void deliveryError(QString message);

private:
    PendingPermissionsModel& m_pendingPermissions;
    SessionCatalogModel& m_sessions;
    DesktopSettings& m_settings;
    DesktopNotificationDriver& m_driver;
    ApplicationActive m_applicationActive;

    void post(const QString& identityToken);
    void handleAction(
        const QString& notificationKey,
        const QString& action,
        const QString& activationToken);
    [[nodiscard]] QString sessionName(const QString& sessionId) const;
    [[nodiscard]] static QString notificationKey(
        const QString& identityToken);
    [[nodiscard]] static QString identityForNotificationKey(
        const QString& key);
};

} // namespace kodosi
