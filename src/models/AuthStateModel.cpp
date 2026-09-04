#include "models/AuthStateModel.hpp"
#include "models/AccountContextFence.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace kodosi {
namespace {

QString optionalString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    return value.isString() ? value.toString() : QString {};
}

} // namespace

AuthStateModel::AuthStateModel(QObject* parent)
    : QObject(parent)
{
}

AuthStateModel::Phase AuthStateModel::phase() const noexcept { return m_phase; }
bool AuthStateModel::signedIn() const noexcept { return m_authenticated; }
QString AuthStateModel::userId() const { return m_userId; }
QString AuthStateModel::userCode() const { return m_userCode; }
QUrl AuthStateModel::verificationUrl() const { return m_verificationUrl; }
QString AuthStateModel::notice() const { return m_notice; }
QString AuthStateModel::identityHealth() const { return m_identityHealth; }
bool AuthStateModel::recoveryRequired() const noexcept
{
    return m_identityHealth != QStringLiteral("healthy");
}
QString AuthStateModel::recoveryMessage() const { return m_recoveryMessage; }

void AuthStateModel::clearRecoveryMessage()
{
    if (m_recoveryMessage.isEmpty()) {
        return;
    }
    m_recoveryMessage.clear();
    emit stateChanged();
}

void AuthStateModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Authentication event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    const auto typeValue = object.value(QStringLiteral("type"));
    if (!typeValue.isString()) {
        emit decodeError(QStringLiteral("Authentication event has no valid type."));
        return;
    }
    const auto type = typeValue.toString();

    if (type == QStringLiteral("auth.ready")
        || type == QStringLiteral("auth.required")) {
        const auto epoch =
            exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
        if (!epoch) {
            emit decodeError(QStringLiteral("Authentication event has no exact account epoch."));
            return;
        }
        if (m_hasAccountEpoch && *epoch < m_accountEpoch) {
            return;
        }
        const auto userId = type == QStringLiteral("auth.ready")
            ? optionalString(object, QStringLiteral("userId"))
            : QString {};
        const auto accountChanged = !m_hasAccountEpoch || *epoch != m_accountEpoch
            || (type == QStringLiteral("auth.ready") && userId != m_userId);
        m_hasAccountEpoch = true;
        m_accountEpoch = *epoch;
        if (type == QStringLiteral("auth.ready")) {
            clearTransientFlow();
            if (accountChanged) {
                m_identityHealth = QStringLiteral("healthy");
                m_recoveryMessage.clear();
            }
            m_phase = Phase::Ready;
            m_authenticated = true;
            m_userId = userId;
        } else {
            clearAccountState();
            m_phase = Phase::SignInRequired;
        }
    } else if (type == QStringLiteral("auth.device_code")) {
        const auto code = object.value(QStringLiteral("userCode"));
        const auto uri = object.value(QStringLiteral("verificationUri"));
        if (!code.isString() || code.toString().isEmpty() || !uri.isString()) {
            emit decodeError(QStringLiteral("Device-code event fields are invalid."));
            return;
        }
        const QUrl verificationUrl(uri.toString(), QUrl::StrictMode);
        if (!verificationUrl.isValid() || verificationUrl.scheme() != QStringLiteral("https")) {
            emit decodeError(QStringLiteral("Device-code verification URL is not trusted HTTPS."));
            return;
        }
        m_phase = Phase::DeviceCode;
        m_userCode = code.toString();
        m_verificationUrl = verificationUrl;
    } else if (type == QStringLiteral("auth.finalizing")) {
        m_phase = Phase::Finalizing;
    } else if (type == QStringLiteral("auth.notice")) {
        m_notice = optionalString(object, QStringLiteral("message"));
    } else if (type == QStringLiteral("auth.identity_health")) {
        const auto state = object.value(QStringLiteral("state"));
        if (!state.isString() || state.toString().isEmpty()
            || state.toString().size() > 64) {
            emit decodeError(QStringLiteral("Identity-health state is invalid."));
            return;
        }
        m_identityHealth = state.toString();
        if (recoveryRequired()) {
            m_recoveryMessage =
                optionalString(object, QStringLiteral("message"));
            if (m_recoveryMessage.isEmpty()) {
                m_recoveryMessage = QStringLiteral(
                    "This device's collaboration identity needs recovery.");
            }
        } else {
            m_recoveryMessage.clear();
        }
    } else if (type == QStringLiteral("auth.error")) {
        m_phase = Phase::Error;
        m_notice = optionalString(object, QStringLiteral("message"));
    } else {
        return;
    }
    emit stateChanged();
}

void AuthStateModel::resetRuntimeAuthority()
{
    clearAccountState();
    m_phase = Phase::Initializing;
    m_accountEpoch = 0;
    m_hasAccountEpoch = false;
    emit stateChanged();
}

void AuthStateModel::clearTransientFlow()
{
    m_userCode.clear();
    m_verificationUrl.clear();
    m_notice.clear();
}

void AuthStateModel::clearAccountState()
{
    clearTransientFlow();
    m_authenticated = false;
    m_userId.clear();
    m_identityHealth = QStringLiteral("healthy");
    m_recoveryMessage.clear();
}

} // namespace kodosi
