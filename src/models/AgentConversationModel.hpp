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

class AgentConversationModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(
        QString degradedWarning
        READ degradedWarning
        NOTIFY stateChanged)
    Q_PROPERTY(bool hasEarlier READ hasEarlier NOTIFY stateChanged)

public:
    enum Role {
        RoleRole = Qt::UserRole + 1,
        ContentRole,
        ToolNameRole,
        TimestampRole,
    };
    Q_ENUM(Role)

    AgentConversationModel(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        AgentSessionIntelModel& sessionIntel,
        qint64 replyTimeoutMs = 15'000,
        QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] QString degradedWarning() const;
    [[nodiscard]] bool hasEarlier() const noexcept;

    Q_INVOKABLE [[nodiscard]] bool inspect(const QString& sessionId);
    [[nodiscard]] bool inspect(
        const QString& sessionId,
        const QString& expectedRuntimeIncarnationId);
    Q_INVOKABLE void close();
    Q_INVOKABLE [[nodiscard]] bool retry();
    Q_INVOKABLE [[nodiscard]] bool loadEarlier();

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void stateChanged();
    void decodeError(QString message);

private:
    static constexpr qsizetype maximumPageRecords = 100;
    static constexpr quint64 maximumPageBytes = 262'144;
    static constexpr qsizetype maximumPageEntries = 500;

    struct Entry {
        QString role;
        QString content;
        QString toolName;
        QString timestamp;
    };

    struct TranscriptIdentity {
        QString agent;
        QString workingDirectory;
        QString nativeSessionId;
    };

    enum class Operation {
        Resolve,
        ReadInitial,
        ReadEarlier,
    };

    struct Pending {
        QString requestId;
        Operation operation;
        quint64 runtimeGeneration;
        quint64 selectionGeneration;
        QString sessionId;
        QString sessionIncarnationId;
        std::optional<quint64> beforeByte;
    };

    CommandDispatcher& m_dispatcher;
    SessionCatalogModel& m_sessions;
    AgentSessionIntelModel& m_sessionIntel;
    AccountContextFence m_accountFence {256};
    QVector<Entry> m_entries;
    std::optional<TranscriptIdentity> m_transcript;
    std::optional<Pending> m_pending;
    std::optional<Operation> m_failedOperation;
    std::optional<quint64> m_nextBeforeByte;
    std::optional<quint64> m_sourceFileBytes;
    QString m_selectedSessionId;
    QString m_selectedIncarnationId;
    QString m_selectedAgent;
    QString m_workingDirectory;
    QString m_error;
    QString m_degradedWarning;
    QTimer m_replyTimer;
    qint64 m_replyTimeoutMs;
    quint64 m_runtimeGeneration = 1;
    quint64 m_selectionGeneration = 0;
    bool m_loading = false;
    bool m_hasAccountContext = false;

    void activateAccount(QString userId, quint64 epoch);
    void applyAgentIntelEvent(const QByteArray& json);
    void sessionAuthorityChanged();
    void clearAuthority(bool clearSelection);
    void clearRows();
    void setFailure(Operation operation, QString message, bool protocolFault);
    void setState(
        bool loading,
        QString error,
        QString degradedWarning);
    [[nodiscard]] bool beginResolve();
    [[nodiscard]] bool beginRead(std::optional<quint64> beforeByte);
    [[nodiscard]] bool sendCommand(QJsonObject command, Pending pending);
    [[nodiscard]] bool pendingIsCurrent(const Pending& pending) const;
    [[nodiscard]] bool decodeResolveReply(
        const QJsonObject& payload,
        TranscriptIdentity& identity) const;
    [[nodiscard]] bool decodePageReply(
        const QJsonObject& payload,
        const Pending& pending,
        QVector<Entry>& entries,
        std::optional<quint64>& nextBeforeByte,
        quint64& sourceFileBytes,
        QString& degradedWarning) const;
    void applyPage(
        const Pending& pending,
        QVector<Entry> entries,
        std::optional<quint64> nextBeforeByte,
        quint64 sourceFileBytes,
        QString degradedWarning);
};

} // namespace kodosi
