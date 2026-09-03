#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/ProjectIntelligenceModel.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include <optional>

namespace kodosi {

class DesktopFileIntegration;

class ExternalDiscoveryModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(quint64 capabilityRevision READ capabilityRevision NOTIFY changed)
    Q_PROPERTY(kodosi::PresentationListModel* servers READ servers CONSTANT)
    Q_PROPERTY(kodosi::PresentationListModel* sessions READ sessions CONSTANT)

public:
    enum class State { Dormant, Loading, Ready, Failed };
    Q_ENUM(State)

    ExternalDiscoveryModel(
        CommandDispatcher& dispatcher,
        DesktopFileIntegration& desktopFiles,
        qint64 replyTimeoutMs = 15'000,
        QObject* parent = nullptr);
    ~ExternalDiscoveryModel() override;

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] quint64 capabilityRevision() const noexcept;
    [[nodiscard]] PresentationListModel* servers() noexcept;
    [[nodiscard]] PresentationListModel* sessions() noexcept;

    Q_INVOKABLE [[nodiscard]] bool refresh(bool force = false);
    Q_INVOKABLE [[nodiscard]] bool copySourcePath(const QString& itemId);
    Q_INVOKABLE [[nodiscard]] bool openSource(const QString& itemId);
    Q_INVOKABLE [[nodiscard]] bool revealSource(const QString& itemId);
    Q_INVOKABLE [[nodiscard]] bool canCopySourcePath(const QString& itemId) const;
    Q_INVOKABLE [[nodiscard]] bool canOpenSource(const QString& itemId) const;
    Q_INVOKABLE [[nodiscard]] bool canRevealSource(const QString& itemId) const;
    Q_INVOKABLE void close();
    void installSyntheticFixture();

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void changed();
    void actionMessage(QString message, bool error);
    void decodeError(QString message);

private:
    enum class Operation { Discover, CopyPath, Open, Reveal };
    struct Item {
        QString id {};
        QString token {};
        QString identity {};
        QString title {};
        QString subtitle {};
        QString kind {};
        QString agent {};
        QString status {};
        QString metadata {};
        bool canCopy = false;
        bool canOpen = false;
        bool canReveal = false;
        bool server = false;
    };
    struct Pending {
        QString requestId {};
        QString itemId {};
        Operation operation = Operation::Discover;
        quint64 runtimeGeneration = 0;
        quint64 demandGeneration = 0;
        QString consumedToken {};
        QString itemIdentity {};
    };

    CommandDispatcher& m_dispatcher;
    DesktopFileIntegration& m_desktopFiles;
    AccountContextFence m_accountFence {128};
    PresentationListModel m_servers;
    PresentationListModel m_sessions;
    QVector<Item> m_items;
    std::optional<Pending> m_pending;
    QHash<QString, QString> m_stableIds;
    QHash<QString, QString> m_releaseRequests;
    QSet<QString> m_ownedHandoffs;
    QHash<QString, QString> m_cancelledOpenRequests;
    QString m_error;
    QTimer m_replyTimer;
    qint64 m_replyTimeoutMs;
    quint64 m_runtimeGeneration = 1;
    quint64 m_demandGeneration = 0;
    quint64 m_capabilityRevision = 0;
    State m_state = State::Dormant;
    bool m_hasAccountContext = false;
    bool m_demanded = false;
    bool m_syntheticFixture = false;

    [[nodiscard]] bool action(const QString& itemId, Operation operation);
    [[nodiscard]] bool send(QJsonObject command, Pending pending);
    void cancelPending();
    void refreshAfterConsumedAction(QString message, bool error);
    void activateAccount(QString userId, quint64 epoch);
    void applyAgentIntelEvent(const QByteArray& json);
    void applyDiscovery(const QJsonObject& payload);
    void applyAction(const QJsonObject& payload, const Pending& pending);
    void applyError(const QJsonObject& object, const Pending& pending);
    [[nodiscard]] bool handleReleaseEvent(const QJsonObject& object);
    [[nodiscard]] bool handleCancelledOpenEvent(const QJsonObject& object);
    void dispatchRelease(const QString& handoffId);
    void releaseOwnedHandoffsBestEffort();
    [[nodiscard]] QString stableId(const QString& key);
    [[nodiscard]] Item* itemForId(const QString& itemId);
    [[nodiscard]] const Item* itemForId(const QString& itemId) const;
    [[nodiscard]] bool pendingCurrent(const Pending& pending) const;
};

} // namespace kodosi
