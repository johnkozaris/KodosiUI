#pragma once

#include "models/AccountContextFence.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace kodosi {

class AttentionModel;
class SessionActions;

class SessionCatalogModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(
        AuthorityState authorityState
        READ authorityState
        NOTIFY authorityStateChanged)
    Q_PROPERTY(
        QString authorityError
        READ authorityError
        NOTIFY authorityStateChanged)

public:
    enum class AuthorityState {
        Loading,
        Loaded,
        Failed,
    };
    Q_ENUM(AuthorityState)

    struct ActionContext {
        QString incarnationId;
        QString kind;
        QString mode;
        QString status;
        QString recovery;
        QString owner;
        QString ownerUserId;
        QString scope;
        QString roomId;
        QString assignmentSessionId;
        QString assignmentIncarnationId;
        QString connectionState;
        QString accessState;
        quint32 permissions;
        bool commandable;
        bool canQueue;
        bool canSteer;
        bool canStopAndSend;
    };

    struct ConversationContext {
        QString incarnationId;
        QString kind;
        QString workingDirectory;
    };

    struct PresentationSession {
        QString id;
        QString kind;
        bool canRetainPresentation = false;
        bool isStageReady = false;
        bool isRemoteConnectable = false;
        bool canSendInput = false;
        bool canRetainFocus = false;
        bool canSendFocus = false;
        bool canResize = false;
    };

    enum Role {
        SessionIdRole = Qt::UserRole + 1,
        IncarnationIdRole,
        KindRole,
        NameRole,
        ProjectRole,
        ModeRole,
        StatusRole,
        RecoveryRole,
        ScopeRole,
        AccessRole,
        RoomNameRole,
        OwnerRole,
        PermissionsRole,
        CommandableRole,
        CanQueueRole,
        CanSteerRole,
        CanStopAndSendRole,
        CanRetainPresentationRole,
        IsStageReadyRole,
        IsRemoteConnectableRole,
        CanSendInputRole,
        CanRetainFocusRole,
        CanSendFocusRole,
        CanResizeRole,
    };
    Q_ENUM(Role)

    explicit SessionCatalogModel(QObject* parent = nullptr);
    SessionCatalogModel(qint64 refreshTimeoutMs, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] AuthorityState authorityState() const noexcept;
    [[nodiscard]] QString authorityError() const;
    [[nodiscard]] bool hasAuthoritativeSnapshot() const noexcept;
    Q_INVOKABLE [[nodiscard]] bool containsSession(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap presentationForSession(
        const QString& sessionId) const;
    [[nodiscard]] std::optional<PresentationSession> presentationSession(
        const QString& sessionId) const;
    [[nodiscard]] std::optional<QString> incarnationForSession(
        const QString& sessionId) const;
    [[nodiscard]] std::optional<ActionContext> actionContext(
        const QString& sessionId) const;
    [[nodiscard]] std::optional<ConversationContext> conversationContext(
        const QString& sessionId) const;
    [[nodiscard]] QVector<PresentationSession> presentationSessions() const;

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestSessionEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void authorityStateChanged();
    void decodeError(QString message);
    void authoritativeSnapshotApplied();
    void inactiveLocalObserved(QString sessionId, QString incarnationId);

private:
    friend class AttentionModel;
    friend class SessionActions;

    struct Session {
        QString id;
        QString incarnationId;
        QString createRequestId;
        QString kind;
        QString name;
        QString project;
        QString mode;
        QString status;
        QString recovery;
        QString scope;
        QString access;
        QString roomName;
        QString roomId;
        QString assignmentSessionId;
        QString assignmentIncarnationId;
        QString owner;
        QString ownerUserId;
        QString connectionState;
        QString accessState;
        QString accessIssue;
        quint32 permissions = 0;
        bool commandable = false;
        bool canQueue = false;
        bool canSteer = false;
        bool canStopAndSend = false;
    };

    QVector<Session> m_sessions;
    QSet<QString> m_runtimeSessionIds;
    AccountContextFence m_accountFence {256};
    QTimer m_refreshTimer;
    QString m_authorityError;
    qint64 m_refreshTimeoutMs;
    AuthorityState m_authorityState = AuthorityState::Loading;
    bool m_hasAuthoritativeSnapshot = false;
    bool m_refreshPending = false;

    static std::optional<Session> decodeSession(const QJsonObject& object);
    [[nodiscard]] static bool belongsToLiveCatalog(const Session& session);
    [[nodiscard]] bool containsRuntimeSession(const QString& sessionId) const;
    [[nodiscard]] static PresentationSession projectPresentation(
        const Session& session);
    void applySessionEvent(const QJsonObject& object);
    void activateAccount(QString userId, quint64 epoch);
    [[nodiscard]] bool beginRefresh();
    void refreshDispatched();
    void refreshDispatchFailed();
    void settleRefreshFailure(QString error);
    void setAuthorityState(AuthorityState state, QString error = {});
    void replaceSnapshot(QVector<Session> sessions);
    void upsert(Session session);
    void remove(const QString& sessionId);
};

} // namespace kodosi
