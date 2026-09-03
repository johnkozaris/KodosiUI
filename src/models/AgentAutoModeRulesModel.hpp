#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <optional>

namespace kodosi {

class AgentAutoModeRulesModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString environmentText READ environmentText WRITE setEnvironmentText NOTIFY changed)
    Q_PROPERTY(QString allowText READ allowText WRITE setAllowText NOTIFY changed)
    Q_PROPERTY(QString softDenyText READ softDenyText WRITE setSoftDenyText NOTIFY changed)
    Q_PROPERTY(QString hardDenyText READ hardDenyText WRITE setHardDenyText NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool saving READ saving NOTIFY changed)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)

public:
    enum class State {
        Dormant,
        Loading,
        Ready,
        Failed,
    };
    Q_ENUM(State)

    explicit AgentAutoModeRulesModel(
        CommandDispatcher& dispatcher,
        qint64 replyTimeoutMs = 15'000,
        QObject* parent = nullptr);

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] QString environmentText() const;
    void setEnvironmentText(const QString& value);
    [[nodiscard]] QString allowText() const;
    void setAllowText(const QString& value);
    [[nodiscard]] QString softDenyText() const;
    void setSoftDenyText(const QString& value);
    [[nodiscard]] QString hardDenyText() const;
    void setHardDenyText(const QString& value);
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] bool saving() const noexcept;
    [[nodiscard]] QString statusMessage() const;

    Q_INVOKABLE [[nodiscard]] bool refresh(bool force = false);
    Q_INVOKABLE [[nodiscard]] bool save();
    Q_INVOKABLE void close();
    void installSyntheticFixture();

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void changed();
    void decodeError(QString message);

private:
    enum class Operation { Read, Write, Reconcile };
    struct Pending {
        QString requestId {};
        QString mutationId {};
        Operation operation = Operation::Read;
        quint64 runtimeGeneration = 0;
        quint64 demandGeneration = 0;
        quint8 reconcileAttempts = 0;
        bool preserveDraft = false;
        QStringList expectedEnvironment {};
        QStringList expectedAllow {};
        QStringList expectedSoftDeny {};
        QStringList expectedHardDeny {};
    };

    CommandDispatcher& m_dispatcher;
    AccountContextFence m_accountFence {64};
    std::optional<Pending> m_pending;
    std::optional<Pending> m_delivery;
    QString m_targetToken;
    QString m_revision;
    QString m_environment;
    QString m_allow;
    QString m_softDeny;
    QString m_hardDeny;
    QString m_baselineEnvironment;
    QString m_baselineAllow;
    QString m_baselineSoftDeny;
    QString m_baselineHardDeny;
    QString m_error;
    QString m_status;
    QString m_statusAfterRead;
    QTimer m_replyTimer;
    QTimer m_deliveryTimer;
    qint64 m_replyTimeoutMs;
    quint64 m_runtimeGeneration = 1;
    quint64 m_demandGeneration = 0;
    State m_state = State::Dormant;
    bool m_hasAccountContext = false;
    bool m_demanded = false;

    [[nodiscard]] bool dispatchRead(bool preserveDraft);
    [[nodiscard]] bool dispatchWrite(const QString& mutationId);
    [[nodiscard]] bool dispatchReconcile(Pending mutation);
    [[nodiscard]] bool send(QJsonObject command, Pending pending);
    [[nodiscard]] bool sendDelivery(QJsonObject command, Pending pending);
    void settleDeliveryFailure(QString message, bool malformed = false);
    void applyAgentIntelEvent(const QByteArray& json);
    void activateAccount(QString userId, quint64 epoch);
    void applyRead(const QJsonObject& payload, const Pending& pending);
    void applyReceipt(const QJsonObject& payload, const Pending& pending);
    void applyError(const QJsonObject& object, const Pending& pending);
    void clearRules();
    void setText(QString& field, const QString& value);
    [[nodiscard]] static QStringList normalizedLines(const QString& value);
    [[nodiscard]] bool pendingCurrent(const Pending& pending) const;
    [[nodiscard]] bool deliveryCurrent(const Pending& pending) const;
};

} // namespace kodosi
