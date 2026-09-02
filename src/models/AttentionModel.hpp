#pragma once

#include "models/AgentSessionIntelModel.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace kodosi {

class AttentionModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY stateChanged)
    Q_PROPERTY(
        AuthorityState authorityState
        READ authorityState
        NOTIFY stateChanged)
    Q_PROPERTY(
        QString authorityError
        READ authorityError
        NOTIFY stateChanged)
    Q_PROPERTY(
        QString operationError
        READ operationError
        NOTIFY operationErrorChanged)

public:
    enum class AuthorityState {
        Loading,
        Loaded,
        Failed,
    };
    Q_ENUM(AuthorityState)

    enum class Category {
        Approval,
        BulkSafe,
        Agent,
        Blocked,
        Waiting,
    };
    Q_ENUM(Category)

    enum class Risk {
        None,
        Safe,
        Unknown,
        Network,
        Credential,
        Destructive,
    };
    Q_ENUM(Risk)

    enum class Tone {
        Muted,
        Accent,
        Warning,
        Danger,
    };
    Q_ENUM(Tone)

    enum class ActionKind {
        None,
        Jump,
        Review,
        Approve,
        BulkApprove,
    };
    Q_ENUM(ActionKind)

    enum Role {
        CategoryRole = Qt::UserRole + 1,
        SessionIdRole,
        SessionNameRole,
        TitleRole,
        SummaryRole,
        RiskRole,
        ToneRole,
        ActionKindRole,
        CanApproveRole,
        CanDenyRole,
        CanJumpRole,
        AttentionTokenRole,
        StableIdRole,
    };
    Q_ENUM(Role)

    AttentionModel(
        PendingPermissionsModel& pendingPermissions,
        AgentSessionIntelModel& agentIntel,
        SessionCatalogModel& sessions,
        SessionActions& sessionActions,
        QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex& index,
        int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int runningCount() const noexcept;
    [[nodiscard]] AuthorityState authorityState() const noexcept;
    [[nodiscard]] QString authorityError() const;
    [[nodiscard]] QString operationError() const;

    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] bool review(const QString& attentionToken);
    Q_INVOKABLE [[nodiscard]] bool jump(const QString& attentionToken);
    Q_INVOKABLE [[nodiscard]] bool approve(const QString& attentionToken);
    Q_INVOKABLE [[nodiscard]] bool deny(const QString& attentionToken);
    Q_INVOKABLE [[nodiscard]] bool approveAll(const QString& attentionToken);
    Q_INVOKABLE void clearOperationError();

signals:
    void countChanged();
    void stateChanged();
    void operationErrorChanged();
    void navigationRequested(
        QString sessionId,
        QString approvalIdentityToken);

private:
    struct Item {
        Category category = Category::Agent;
        QString sessionId;
        QString sessionName;
        QString title;
        QString summary;
        Risk risk = Risk::None;
        Tone tone = Tone::Muted;
        ActionKind actionKind = ActionKind::None;
        bool canApprove = false;
        bool canDeny = false;
        bool canJump = false;
        QString attentionToken;
        QString stableId;
        QString sessionIncarnationId;
        QString authorityIncarnationId;
        QString attentionKind;
        quint64 sourceRevision = 0;
        QDateTime recency;
        QVector<QString> approvalIdentityTokens;

        bool operator==(const Item&) const = default;
    };

    PendingPermissionsModel& m_pendingPermissions;
    AgentSessionIntelModel& m_agentIntel;
    SessionCatalogModel& m_sessions;
    SessionActions& m_sessionActions;
    QVector<Item> m_items;
    QHash<QString, QString> m_tokensByExactKey;
    QString m_operationError;
    QString m_authorityError;
    int m_runningCount = 0;
    AuthorityState m_authorityState = AuthorityState::Loading;

    void rebuild();
    void setOperationError(QString error);
    [[nodiscard]] bool requestAuthorityRefresh(bool clearError);
    [[nodiscard]] bool failStale();
    [[nodiscard]] bool eligibleSession(const QString& sessionId) const;
    [[nodiscard]] bool sourceIsCurrent(const Item& item) const;
    [[nodiscard]] const Item* itemForToken(const QString& token) const;
    [[nodiscard]] QString tokenForExactKey(
        const QString& exactKey,
        QSet<QString>& liveKeys);

    [[nodiscard]] static int riskSeverity(const QString& risk);
    [[nodiscard]] static int attentionSeverity(const QString& kind);
    [[nodiscard]] static bool requestActionable(
        const PendingPermissionsModel::Request& request);
    [[nodiscard]] static Risk riskValue(const QString& risk);
    [[nodiscard]] static Tone approvalTone(const QString& risk);
    [[nodiscard]] static Tone attentionTone(const QString& kind);
    [[nodiscard]] static QString attentionTitle(const QString& kind);
};

} // namespace kodosi
