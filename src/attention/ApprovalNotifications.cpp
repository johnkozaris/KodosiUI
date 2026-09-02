#include "attention/ApprovalNotifications.hpp"

#include "platform/NotificationText.hpp"

#include <QGuiApplication>
#include <QVariantMap>

#include <utility>

namespace kodosi {

ApprovalNotifications::ApprovalNotifications(
    PendingPermissionsModel& pendingPermissions,
    SessionCatalogModel& sessions,
    DesktopSettings& settings,
    DesktopNotificationDriver& driver,
    ApplicationActive applicationActive,
    QObject* parent)
    : QObject(parent)
    , m_pendingPermissions(pendingPermissions)
    , m_sessions(sessions)
    , m_settings(settings)
    , m_driver(driver)
    , m_applicationActive(std::move(applicationActive))
{
    if (!m_applicationActive) {
        m_applicationActive = [] {
            return QGuiApplication::applicationState() == Qt::ApplicationActive;
        };
    }
    connect(
        &m_pendingPermissions,
        &PendingPermissionsModel::requestAdded,
        this,
        [this](const QString& identityToken) { post(identityToken); });
    connect(
        &m_pendingPermissions,
        &PendingPermissionsModel::requestRemoved,
        this,
        [this](const QString& identityToken) {
            m_driver.withdraw(notificationKey(identityToken));
        });
    connect(
        &m_driver,
        &DesktopNotificationDriver::actionInvoked,
        this,
        [this](
            const QString& key,
            const QString& action,
            const QString& activationToken) {
            handleAction(key, action, activationToken);
        });
    connect(
        &m_driver,
        &DesktopNotificationDriver::deliveryError,
        this,
        [this](const QString& key, const QString& message) {
            if (!identityForNotificationKey(key).isEmpty()) {
                emit deliveryError(message);
            }
        });
}

bool ApprovalNotifications::oneTapDecidable(const QStringView risk)
{
    return risk == QStringLiteral("safe") || risk == QStringLiteral("network");
}

QString ApprovalNotifications::notificationBody(
    const QStringView risk,
    const QStringView toolInputSummary)
{
    if (risk == QStringLiteral("credential")
        || risk == QStringLiteral("unknown")) {
        return tr("Open Kodosi to review this request.");
    }
    const auto summary = notificationPlainText(toolInputSummary, 240);
    return summary.isEmpty() ? tr("Permission request waiting") : summary;
}

void ApprovalNotifications::post(const QString& identityToken)
{
    if (!m_settings.toolApprovalAlerts() || m_applicationActive()) {
        return;
    }
    const auto request =
        m_pendingPermissions.notificationRequest(identityToken);
    if (!request || !request->actionable) {
        return;
    }

    const auto tool = notificationPlainText(request->toolName, 80);
    const auto session = sessionName(request->sessionId);
    QStringList actions {
        QStringLiteral("default"),
        tr("Open Kodosi"),
    };
    if (oneTapDecidable(request->risk)) {
        actions.append({
            QStringLiteral("approve"),
            tr("Approve"),
        });
    }
    actions.append({
        QStringLiteral("deny"),
        tr("Deny"),
    });
    m_driver.post({
        .key = notificationKey(request->identityToken),
        .title = QStringLiteral("%1 \u2014 %2")
                     .arg(
                         tool.isEmpty() ? tr("Tool") : tool,
                         session),
        .body = notificationBody(request->risk, request->toolInputSummary),
        .actions = std::move(actions),
    });
}

void ApprovalNotifications::handleAction(
    const QString& key,
    const QString& action,
    const QString& activationToken)
{
    const auto identityToken = identityForNotificationKey(key);
    if (identityToken.isEmpty()) {
        return;
    }
    const auto request =
        m_pendingPermissions.notificationRequest(identityToken);
    if (!request) {
        m_driver.withdraw(key);
        return;
    }
    if (action == QStringLiteral("default")) {
        m_driver.withdraw(key);
        emit reviewRequested(
            request->identityToken,
            request->sessionId,
            activationToken);
        return;
    }
    if (!request->actionable) {
        return;
    }
    if (action == QStringLiteral("approve")) {
        if (oneTapDecidable(request->risk)
            && m_pendingPermissions.approve(identityToken)) {
            m_driver.withdraw(key);
        }
        return;
    }
    if (action == QStringLiteral("deny")
        && m_pendingPermissions.deny(identityToken)) {
        m_driver.withdraw(key);
    }
}

QString ApprovalNotifications::sessionName(const QString& sessionId) const
{
    const auto presentation = m_sessions.presentationForSession(sessionId);
    const auto name = notificationPlainText(
        presentation.value(QStringLiteral("name")).toString(),
        80);
    if (!name.isEmpty()) {
        return name;
    }
    const auto fallback = notificationPlainText(sessionId, 8);
    return fallback.isEmpty() ? tr("Session") : fallback;
}

QString ApprovalNotifications::notificationKey(
    const QString& identityToken)
{
    return QStringLiteral("approval:") + identityToken;
}

QString ApprovalNotifications::identityForNotificationKey(
    const QString& key)
{
    constexpr auto prefixLength = 9;
    return key.startsWith(QStringLiteral("approval:"))
        && key.size() > prefixLength
        ? key.sliced(prefixLength)
        : QString {};
}

} // namespace kodosi
