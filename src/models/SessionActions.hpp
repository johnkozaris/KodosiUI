#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVector>

namespace kodosi {

class SessionAccess;
class DesktopSettings;
class ProviderConversationResumeResolver;

class HiddenSessionsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        SessionIdRole = Qt::UserRole + 1,
        NameRole,
        ProjectRole,
        OwnerRole,
    };
    Q_ENUM(Role)

    explicit HiddenSessionsModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] bool contains(const QString& sessionId) const;

signals:
    void countChanged();

private:
    friend class SessionActions;
    struct Entry {
        QString id;
        QString name;
        QString project;
        QString owner;
    };
    QVector<Entry> m_entries;
    void replace(QVector<Entry> entries);
};

class SessionActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(
        quint64 availabilityRevision
        READ availabilityRevision
        NOTIFY availabilityChanged)
    Q_PROPERTY(
        QString closeConfirmationSessionId
        READ closeConfirmationSessionId
        NOTIFY stateChanged)
    Q_PROPERTY(bool creating READ creating NOTIFY stateChanged)
    Q_PROPERTY(
        QVariantList pendingCreations
        READ pendingCreations
        NOTIFY stateChanged)
    Q_PROPERTY(
        QString lastCreateRequestId
        READ lastCreateRequestId
        NOTIFY stateChanged)
    Q_PROPERTY(
        int inactiveCleanupCount
        READ inactiveCleanupCount
        NOTIFY availabilityChanged)
    Q_PROPERTY(
        kodosi::HiddenSessionsModel* hiddenSessions
        READ hiddenSessions
        CONSTANT)
    Q_PROPERTY(bool loadingHidden READ loadingHidden NOTIFY stateChanged)
    Q_PROPERTY(
        QString defaultWorkingDirectory
        READ defaultWorkingDirectory
        NOTIFY defaultWorkingDirectoryChanged)

public:
    SessionActions(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        QObject* parent = nullptr);
    SessionActions(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        qint64 closeTimeoutMs,
        QObject* parent = nullptr);
    SessionActions(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        DesktopSettings& settings,
        QObject* parent = nullptr);
    SessionActions(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        SessionAccess& access,
        QObject* parent = nullptr);
    SessionActions(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        SessionAccess& access,
        DesktopSettings& settings,
        QObject* parent = nullptr);

    [[nodiscard]] QString lastError() const;
    [[nodiscard]] quint64 availabilityRevision() const noexcept;
    [[nodiscard]] QString closeConfirmationSessionId() const;
    [[nodiscard]] bool creating() const noexcept;
    [[nodiscard]] QVariantList pendingCreations() const;
    [[nodiscard]] QString lastCreateRequestId() const;
    [[nodiscard]] int inactiveCleanupCount() const noexcept;
    [[nodiscard]] QString defaultWorkingDirectory() const;
    [[nodiscard]] HiddenSessionsModel* hiddenSessions() noexcept;
    [[nodiscard]] bool loadingHidden() const noexcept;
    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] bool create(
        const QString& name,
        const QString& workingDirectory);
    Q_INVOKABLE [[nodiscard]] bool createInDirectory(
        const QString& workingDirectory);
    Q_INVOKABLE [[nodiscard]] bool createResumed(
        const QString& conversationPresentationId);
    Q_INVOKABLE [[nodiscard]] bool createDefault();
    Q_INVOKABLE [[nodiscard]] bool isCreatePending(
        const QString& requestId) const;
    Q_INVOKABLE [[nodiscard]] bool canInterrupt(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool canClose(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool canRename(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool canOpenRemote(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool canHide(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool interrupt(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool requestCloseConfirmation(
        const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool confirmClose(const QString& sessionId);
    Q_INVOKABLE void cancelCloseConfirmation();
    Q_INVOKABLE [[nodiscard]] bool rename(
        const QString& sessionId,
        const QString& name);
    Q_INVOKABLE [[nodiscard]] bool openRemote(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool restoreRemote(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool hide(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool unhide(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool refreshHidden();
    Q_INVOKABLE void clearError();
    void setProviderConversationResumeResolver(
        ProviderConversationResumeResolver* resolver);

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestSessionEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void availabilityChanged();
    void sessionCreated(QString sessionId);
    void sessionCreationResolved(QString requestId, QString sessionId);
    void remoteOpenSucceeded(
        QString sessionId,
        QString incarnationId);
    void remoteOpenFailed(
        QString sessionId,
        QString incarnationId);
    void defaultWorkingDirectoryChanged();

private:
    CommandDispatcher& m_dispatcher;
    SessionCatalogModel& m_sessions;
    SessionAccess* m_access = nullptr;
    DesktopSettings* m_settings = nullptr;
    ProviderConversationResumeResolver* m_resumeResolver = nullptr;
    AccountContextFence m_accountFence {256};
    struct PendingReceipt {
        QString operation;
        QString sessionId;
        QString incarnationId;
    };
    struct PendingRename {
        QString incarnationId;
        QString submittedName;
    };
    QHash<QString, PendingReceipt> m_pendingReceipts;
    QHash<QString, PendingRename> m_pendingRenames;
    QHash<QString, QString> m_pendingRemoteOpens;
    QSet<QString> m_pendingHides;
    QSet<QString> m_pendingUnhides;
    HiddenSessionsModel m_hiddenSessions;
    QTimer m_hiddenRetryTimer;
    QString m_closeConfirmationSessionId;
    QString m_closeConfirmationIncarnationId;
    struct PendingCreate {
        QString name;
        QString sessionId;
        QString incarnationId;
    };
    QHash<QString, PendingCreate> m_pendingCreates;
    QStringList m_pendingCreateOrder;
    QString m_lastCreateRequestId;
    QHash<QString, QTimer*> m_closeTimers;
    struct PendingInactiveCleanup {
        QString sessionId;
        QString incarnationId;
    };
    QQueue<PendingInactiveCleanup> m_inactiveCleanupQueue;
    QSet<QString> m_inactiveCleanupIds;
    QHash<QString, QTimer*> m_inactiveCleanupTimers;
    qint64 m_closeTimeoutMs = 30'000;
    QString m_hiddenRequestId;
    QString m_accountUserId;
    quint64 m_accountEpoch = 0;
    QString m_lastError;
    quint64 m_availabilityRevision = 0;
    bool m_hiddenRefreshQueued = false;
    int m_hiddenRetryAttempts = 0;

    [[nodiscard]] bool send(QJsonObject command);
    [[nodiscard]] bool dispatchCreate(
        const QString& name,
        const QString& workingDirectory,
        std::optional<QJsonObject> resume);
    [[nodiscard]] bool dispatchClose(
        const QString& sessionId,
        const QString& incarnationId);
    [[nodiscard]] bool dispatchLifecycle(
        const QString& operation,
        const QString& commandType,
        const QString& sessionId,
        const QString& incarnationId);
    [[nodiscard]] bool hasPendingLifecycle(const QString& sessionId) const;
    void clearCloseTimeout(const QString& requestId);
    void enqueueInactiveCleanup(
        const QString& sessionId,
        const QString& incarnationId);
    void pumpInactiveCleanup();
    void clearInactiveCleanupTracking(
        const QString& requestId,
        const QString& sessionId);
    void activateAccount(QString userId, quint64 epoch);
    void applySessionEvent(const QJsonObject& object);
    void reconcilePendingFromCatalog();
    void applyHiddenList(const QJsonObject& object);
    [[nodiscard]] bool dispatchHiddenRefresh();
    void bumpAvailability();
    [[nodiscard]] static bool interruptStatus(const QString& status);
    [[nodiscard]] static bool closeStatus(const QString& status);
    [[nodiscard]] static QString generatedSessionName();
    [[nodiscard]] static bool stageReady(
        const SessionCatalogModel::ActionContext& context);
};

} // namespace kodosi
