#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

namespace kodosi {

class AuthStateModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(Phase phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY stateChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY stateChanged)
    Q_PROPERTY(QUrl verificationUrl READ verificationUrl NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    Q_PROPERTY(QString identityHealth READ identityHealth NOTIFY stateChanged)
    Q_PROPERTY(bool recoveryRequired READ recoveryRequired NOTIFY stateChanged)
    Q_PROPERTY(QString recoveryMessage READ recoveryMessage NOTIFY stateChanged)

public:
    enum class Phase {
        Initializing,
        Ready,
        SignInRequired,
        DeviceCode,
        Finalizing,
        Error,
    };
    Q_ENUM(Phase)

    explicit AuthStateModel(QObject* parent = nullptr);

    [[nodiscard]] Phase phase() const noexcept;
    [[nodiscard]] bool signedIn() const noexcept;
    [[nodiscard]] QString userId() const;
    [[nodiscard]] QString userCode() const;
    [[nodiscard]] QUrl verificationUrl() const;
    [[nodiscard]] QString notice() const;
    [[nodiscard]] QString identityHealth() const;
    [[nodiscard]] bool recoveryRequired() const noexcept;
    [[nodiscard]] QString recoveryMessage() const;
    Q_INVOKABLE void clearRecoveryMessage();

public slots:
    void ingestAuthEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void decodeError(QString message);

private:
    Phase m_phase = Phase::Initializing;
    QString m_userId;
    QString m_userCode;
    QUrl m_verificationUrl;
    QString m_notice;
    QString m_identityHealth = QStringLiteral("healthy");
    QString m_recoveryMessage;
    quint64 m_accountEpoch = 0;
    bool m_hasAccountEpoch = false;
    bool m_authenticated = false;

    void clearTransientFlow();
    void clearAccountState();
};

} // namespace kodosi
