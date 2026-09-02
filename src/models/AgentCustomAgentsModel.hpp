#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/AgentSessionIntelModel.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <optional>

namespace kodosi {

class AgentCustomAgentsModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(
        QString selectedItemToken
        READ selectedItemToken
        NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(
        QString selectedDescription
        READ selectedDescription
        NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel NOTIFY selectionChanged)
    Q_PROPERTY(
        QStringList selectedTools
        READ selectedTools
        NOTIFY selectionChanged)
    Q_PROPERTY(
        QStringList selectedDisallowedTools
        READ selectedDisallowedTools
        NOTIFY selectionChanged)
    Q_PROPERTY(
        QString frontmatter
        READ frontmatter
        NOTIFY selectionChanged)
    Q_PROPERTY(
        QString systemPrompt
        READ systemPrompt
        NOTIFY selectionChanged)
    Q_PROPERTY(
        QString detailErrors
        READ detailErrors
        NOTIFY selectionChanged)
    Q_PROPERTY(
        bool detailLoading
        READ detailLoading
        NOTIFY selectionChanged)
    Q_PROPERTY(
        bool detailLoaded
        READ detailLoaded
        NOTIFY selectionChanged)
    Q_PROPERTY(
        QString detailError
        READ detailError
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
        ItemTokenRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        ModelRole,
        ToolsRole,
        ParseErrorSummaryRole,
        AccessibleIdRole,
    };
    Q_ENUM(Role)

    AgentCustomAgentsModel(
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
    [[nodiscard]] QString selectedItemToken() const;
    [[nodiscard]] QString selectedName() const;
    [[nodiscard]] QString selectedDescription() const;
    [[nodiscard]] QString selectedModel() const;
    [[nodiscard]] QStringList selectedTools() const;
    [[nodiscard]] QStringList selectedDisallowedTools() const;
    [[nodiscard]] QString frontmatter() const;
    [[nodiscard]] QString systemPrompt() const;
    [[nodiscard]] QString detailErrors() const;
    [[nodiscard]] bool detailLoading() const noexcept;
    [[nodiscard]] bool detailLoaded() const noexcept;
    [[nodiscard]] QString detailError() const;

    Q_INVOKABLE [[nodiscard]] bool inspect(const QString& sessionId);
    Q_INVOKABLE void close();
    Q_INVOKABLE [[nodiscard]] bool retry();
    Q_INVOKABLE [[nodiscard]] bool select(const QString& itemToken);

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
    static constexpr qsizetype maximumItems = 128;
    static constexpr qsizetype maximumSummaryBytes = 256 * 1024;
    static constexpr qsizetype maximumDetailBytes = 7 * 1024 * 1024;
    static constexpr qsizetype maximumSourceBytes = 1024 * 1024;
    static constexpr qsizetype maximumFrontmatterBytes = 64 * 1024;
    static constexpr qsizetype maximumTools = 64;
    static constexpr qsizetype maximumErrors = 64;

    struct Item {
        QString itemToken;
        QString selectionToken;
        QString target;
        QString name;
        QString description;
        std::optional<QString> model;
        QStringList tools;
        int errorCount = 0;
        bool tokenAvailable = true;
        QString detailName;
        QString detailDescription;
        std::optional<QString> detailModel;
        QStringList detailTools;
        QStringList disallowedTools;
        QString frontmatter;
        QString systemPrompt;
        QStringList errors;
        QString parseErrorSummary;
        QString accessibleId;
        QString detailError;
        bool detailLoading = false;
        bool detailLoaded = false;
        QByteArray summaryKey;
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
        QString agentsDirectory;
        QString itemToken;
        QString selectionToken;
        QString target;
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
    QString m_agentsDirectory;
    QString m_selectedItemToken;
    QString m_error;
    QString m_statusMessage;
    QString m_retryItemToken;
    QTimer m_replyTimer;
    qint64 m_replyTimeoutMs;
    quint64 m_runtimeGeneration = 1;
    quint64 m_demandGeneration = 0;
    quint64 m_selectionGeneration = 0;
    State m_state = State::Dormant;
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
        Item& detail) const;
    void applyList(QVector<Item> items);
    [[nodiscard]] const Item* selectedItem() const;
    [[nodiscard]] Item* selectedItem();
};

} // namespace kodosi
