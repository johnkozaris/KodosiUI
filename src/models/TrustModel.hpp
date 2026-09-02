#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace kodosi {

class TrustModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(RetryAction retryAction READ retryAction NOTIFY stateChanged)
    Q_PROPERTY(QString confirmationUserId READ confirmationUserId NOTIFY stateChanged)
    Q_PROPERTY(QString pendingResetUserId READ pendingResetUserId NOTIFY stateChanged)

public:
    enum class RetryAction {
        None,
        Refresh,
        Reset,
    };
    Q_ENUM(RetryAction)

    enum class ResetAvailability {
        Available,
        Confirming,
        Resetting,
        Unavailable,
    };
    Q_ENUM(ResetAvailability)

    enum Role {
        UserIdRole = Qt::UserRole + 1,
        GenerationRole,
        SignerDeviceIdRole,
        DeviceCountRole,
        PinnedAtMsRole,
        ResetAvailabilityRole,
    };
    Q_ENUM(Role)

    explicit TrustModel(CommandDispatcher& dispatcher, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] RetryAction retryAction() const noexcept;
    [[nodiscard]] QString confirmationUserId() const;
    [[nodiscard]] QString pendingResetUserId() const;

    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] bool requestResetConfirmation(const QString& userId);
    Q_INVOKABLE void cancelResetConfirmation();
    Q_INVOKABLE [[nodiscard]] bool confirmReset();
    Q_INVOKABLE [[nodiscard]] bool retry();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE [[nodiscard]] ResetAvailability resetAvailability(
        const QString& userId) const;

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestTrustEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void stateChanged();
    void decodeError(QString message);

private:
    struct Pin {
        QString userId;
        quint64 generation;
        QString signerDeviceId;
        quint32 deviceCount;
        qint64 pinnedAtMs;
    };

    struct PendingRefresh {
        QString requestId;
        quint64 generation;
    };

    struct ResetConfirmation {
        AccountContext account;
        QString targetUserId;
    };

    CommandDispatcher& m_dispatcher;
    AccountContextFence m_accountFence {128};
    QVector<Pin> m_pins;
    std::optional<AccountContext> m_accountContext;
    std::optional<PendingRefresh> m_pendingRefresh;
    std::optional<ResetConfirmation> m_resetConfirmation;
    std::optional<quint64> m_resetStartedAfterRefreshGeneration;
    QString m_pendingResetRequestId;
    QString m_pendingResetUserId;
    QString m_lastError;
    QString m_retryUserId;
    quint64 m_refreshGeneration = 0;
    RetryAction m_retryAction = RetryAction::None;
    bool m_loading = false;
    bool m_authenticated = false;

    void activateAccount(QString userId, quint64 epoch, bool authenticated);
    void applyTrustEvent(const QJsonObject& object);
    void clearAccountState();
    void dispatchReset(const QString& userId);
    void reconcilePendingReset(const QVector<Pin>& pins, quint64 snapshotGeneration);
    void replacePins(QVector<Pin> pins);
    void removePin(const QString& userId);
    void notifyStateChanged();
    void setError(QString message, RetryAction retryAction, QString retryUserId = {});

    [[nodiscard]] bool hasPin(const QString& userId) const;
    [[nodiscard]] CommandDispatcher::Result sendCommand(const QJsonObject& command);
    [[nodiscard]] static std::optional<Pin> decodePin(const QJsonObject& object);
};

} // namespace kodosi
