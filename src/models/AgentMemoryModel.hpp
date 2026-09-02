#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/AgentSessionIntelModel.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <optional>

namespace kodosi {

class AgentMemoryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(
        QString selectedFilename
        READ selectedFilename
        NOTIFY selectionChanged)
    Q_PROPERTY(QString content READ content NOTIFY selectionChanged)
    Q_PROPERTY(
        bool contentLoaded
        READ contentLoaded
        NOTIFY selectionChanged)
    Q_PROPERTY(
        bool contentLoading
        READ contentLoading
        NOTIFY selectionChanged)
    Q_PROPERTY(
        QString contentError
        READ contentError
        NOTIFY selectionChanged)

public:
    enum class State {
        Dormant,
        Waiting,
        Ineligible,
        Loading,
        Ready,
        Failed,
    };
    Q_ENUM(State)

    enum Role {
        FilenameRole = Qt::UserRole + 1,
        KindRole,
        DescriptionRole,
    };
    Q_ENUM(Role)

    AgentMemoryModel(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        AgentSessionIntelModel& sessionIntel,
        qint64 replyTimeoutMs = 15'000,
        QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] QString selectedFilename() const;
    [[nodiscard]] QString content() const;
    [[nodiscard]] bool contentLoaded() const noexcept;
    [[nodiscard]] bool contentLoading() const noexcept;
    [[nodiscard]] QString contentError() const;

    Q_INVOKABLE [[nodiscard]] bool inspect(const QString& sessionId);
    Q_INVOKABLE void close();
    Q_INVOKABLE [[nodiscard]] bool retry();
    Q_INVOKABLE [[nodiscard]] bool select(const QString& filename);

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void stateChanged();
    void selectionChanged();
    void decodeError(QString message);

private:
    static constexpr qsizetype maximumItems = 512;
    static constexpr qsizetype maximumContentBytes = 4 * 1024 * 1024;

    struct Item {
        QString filename;
        std::optional<QString> memoryType;
        QString description;
        QString selectionToken;
        QString canonicalCwd;
        QString projectSlug;
        bool tokenAvailable = true;
    };

    enum class Operation {
        List,
        Read,
    };

    struct Pending {
        QString requestId;
        Operation operation;
        quint64 runtimeGeneration;
        quint64 demandGeneration;
        quint64 selectionGeneration;
        QString sessionId;
        QString sessionIncarnationId;
        QString filename;
        QString selectionToken;
    };

    CommandDispatcher& m_dispatcher;
    SessionCatalogModel& m_sessions;
    AgentSessionIntelModel& m_sessionIntel;
    AccountContextFence m_accountFence {256};
    QVector<Item> m_items;
    std::optional<Pending> m_pending;
    std::optional<Operation> m_failedOperation;
    QString m_selectedSessionId;
    QString m_selectedIncarnationId;
    QString m_selectedAgent;
    QString m_workingDirectory;
    QString m_selectedFilename;
    QString m_content;
    QString m_contentError;
    QString m_error;
    QString m_statusMessage;
    QString m_retrySelectionFilename;
    QTimer m_replyTimer;
    qint64 m_replyTimeoutMs;
    quint64 m_runtimeGeneration = 1;
    quint64 m_demandGeneration = 0;
    quint64 m_selectionGeneration = 0;
    State m_state = State::Dormant;
    bool m_contentLoading = false;
    bool m_contentLoaded = false;
    bool m_hasAccountContext = false;
    bool m_demanded = false;

    void activateAccount(QString userId, quint64 epoch);
    void applyAgentIntelEvent(const QByteArray& json);
    void authorityChanged();
    void attemptEligibility();
    void invalidateDemand(QString message);
    void clearAuthority(bool clearDemand);
    void clearRows();
    void clearSelection();
    void setState(State state, QString error = {}, QString statusMessage = {});
    void setContentState(bool loading, QString error = {});
    void setFailure(Operation operation, QString message, bool protocolFault);
    [[nodiscard]] bool beginList();
    [[nodiscard]] bool beginRead(qsizetype index);
    [[nodiscard]] bool sendCommand(QJsonObject command, Pending pending);
    [[nodiscard]] bool pendingIsCurrent(const Pending& pending) const;
    [[nodiscard]] bool decodeListReply(
        const QJsonObject& payload,
        QVector<Item>& items) const;
    [[nodiscard]] bool decodeReadReply(
        const QJsonObject& payload,
        const Pending& pending,
        QString& content) const;
    void applyList(QVector<Item> items);
    [[nodiscard]] static QString describe(
        const std::optional<QString>& memoryType);
};

} // namespace kodosi
