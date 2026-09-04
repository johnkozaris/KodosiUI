#pragma once

#include "bridge/RuntimeBridge.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

namespace kodosi {

class AuthActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString failedOperation READ failedOperation NOTIFY stateChanged)

public:
    explicit AuthActions(
        CommandDispatcher& dispatcher,
        QObject* parent = nullptr);

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString failedOperation() const;

    Q_INVOKABLE [[nodiscard]] bool beginSignIn();
    Q_INVOKABLE [[nodiscard]] bool useAnotherAccount();
    Q_INVOKABLE [[nodiscard]] bool signOut();
    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] bool resetIdentity();
    Q_INVOKABLE [[nodiscard]] bool retry();
    Q_INVOKABLE [[nodiscard]] bool openVerificationUrl(const QUrl& url);
    Q_INVOKABLE [[nodiscard]] bool copyCode(const QString& code);
    Q_INVOKABLE void clearError();

public slots:
    void ingestAuthEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();

private:
    CommandDispatcher& m_dispatcher;
    QString m_lastError;
    QString m_failedOperation;
    bool m_busy = false;
    bool m_switchAccountPending = false;

    [[nodiscard]] bool send(const char* type);
    [[nodiscard]] bool begin(const char* type);
};

} // namespace kodosi
