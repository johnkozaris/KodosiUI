#include "models/AuthActions.hpp"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>

namespace kodosi {

AuthActions::AuthActions(
    CommandDispatcher& dispatcher,
    const qint64 operationTimeoutMs,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_operationTimeoutMs(operationTimeoutMs)
{
    Q_ASSERT(operationTimeoutMs >= 0);
    m_operationTimer.setSingleShot(true);
    connect(&m_operationTimer, &QTimer::timeout, this, [this] {
        if (!m_busy) {
            return;
        }
        m_busy = false;
        m_lastError =
            QStringLiteral("The authentication request did not complete. Try again.");
        emit stateChanged();
    });
}

bool AuthActions::busy() const noexcept
{
    return m_busy;
}

QString AuthActions::lastError() const
{
    return m_lastError;
}

QString AuthActions::failedOperation() const
{
    return m_failedOperation;
}

bool AuthActions::beginSignIn()
{
    return begin("auth.login.start");
}

bool AuthActions::signOut()
{
    return begin("auth.logout");
}

bool AuthActions::refresh()
{
    return begin("auth.refresh");
}

bool AuthActions::resetIdentity()
{
    return begin("auth.identity.reset");
}

bool AuthActions::retry()
{
    if (m_failedOperation == QStringLiteral("logout")) {
        return signOut();
    }
    if (m_failedOperation == QStringLiteral("refresh")) {
        return refresh();
    }
    if (m_failedOperation == QStringLiteral("identity.reset")) {
        return resetIdentity();
    }
    return beginSignIn();
}

bool AuthActions::openVerificationUrl(const QUrl& url)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("https")
        || url.host().isEmpty()) {
        m_lastError =
            QStringLiteral("The sign-in verification address is not trusted HTTPS.");
        emit stateChanged();
        return false;
    }
    if (QDesktopServices::openUrl(url)) {
        return true;
    }
    m_lastError = QStringLiteral("The browser could not be opened.");
    emit stateChanged();
    return false;
}

bool AuthActions::copyCode(const QString& code)
{
    const auto trimmed = code.trimmed();
    if (trimmed.isEmpty() || QGuiApplication::clipboard() == nullptr) {
        m_lastError = QStringLiteral("The sign-in code could not be copied.");
        emit stateChanged();
        return false;
    }
    QGuiApplication::clipboard()->setText(trimmed);
    return true;
}

void AuthActions::clearError()
{
    if (m_lastError.isEmpty()) {
        return;
    }
    m_lastError.clear();
    m_failedOperation.clear();
    emit stateChanged();
}

void AuthActions::ingestAuthEvent(QByteArray json)
{
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type"));
    if (!type.isString()) {
        return;
    }
    if (type.toString() == QStringLiteral("auth.device_code")
        ) {
        const auto code = object.value(QStringLiteral("userCode"));
        const auto uri = object.value(QStringLiteral("verificationUri"));
        const auto url = uri.isString()
            ? QUrl(uri.toString(), QUrl::StrictMode)
            : QUrl {};
        if (!code.isString() || code.toString().isEmpty() || !url.isValid()
            || url.scheme() != QStringLiteral("https") || url.host().isEmpty()) {
            m_operationTimer.stop();
            m_busy = false;
            m_failedOperation = QStringLiteral("login.start");
            m_lastError =
                QStringLiteral("The sign-in code or verification address is invalid.");
            emit stateChanged();
            return;
        }
        m_operationTimer.stop();
        m_busy = true;
        m_lastError.clear();
        emit stateChanged();
    } else if (type.toString() == QStringLiteral("auth.finalizing")) {
        m_busy = true;
        m_lastError.clear();
        m_operationTimer.start(static_cast<int>(m_operationTimeoutMs));
        emit stateChanged();
    } else if (type.toString() == QStringLiteral("auth.ready")) {
        m_busy = false;
        m_operationTimer.stop();
        m_lastError.clear();
        m_failedOperation.clear();
        emit stateChanged();
    } else if (type.toString() == QStringLiteral("auth.required")) {
        m_busy = false;
        m_operationTimer.stop();
        emit stateChanged();
    } else if (type.toString() == QStringLiteral("auth.error")) {
        m_busy = false;
        m_operationTimer.stop();
        const auto operation = object.value(QStringLiteral("operation"));
        if (operation.isString()
            && (operation.toString() == QStringLiteral("login.start")
                || operation.toString() == QStringLiteral("logout")
                || operation.toString() == QStringLiteral("identity.reset")
                || operation.toString() == QStringLiteral("refresh"))) {
            m_failedOperation = operation.toString();
        }
        const auto message = object.value(QStringLiteral("message"));
        m_lastError = message.isString() && !message.toString().isEmpty()
            ? message.toString()
            : QStringLiteral("Authentication failed.");
        emit stateChanged();
    }
}

void AuthActions::resetRuntimeAuthority()
{
    m_busy = false;
    m_operationTimer.stop();
    m_lastError.clear();
    m_failedOperation.clear();
    emit stateChanged();
}

bool AuthActions::send(const char* type)
{
    const auto json = QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QString::fromLatin1(type)},
    }).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::Auth, json)) {
        m_operationTimer.start(static_cast<int>(m_operationTimeoutMs));
        return true;
    }
    m_busy = false;
    m_lastError =
        QStringLiteral("The runtime did not accept the authentication request.");
    emit stateChanged();
    return false;
}

bool AuthActions::begin(const char* type)
{
    m_busy = true;
    m_lastError.clear();
    const auto command = QString::fromLatin1(type);
    m_failedOperation = command == QStringLiteral("auth.login.start")
        ? QStringLiteral("login.start")
        : command == QStringLiteral("auth.identity.reset")
        ? QStringLiteral("identity.reset")
        : command.mid(5);
    emit stateChanged();
    return send(type);
}

} // namespace kodosi
