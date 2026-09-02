#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>

class QEvent;
class QScreen;
class QWindow;

namespace kodosi {

class SessionCatalogModel;

class DesktopScreenLayoutNotifier : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

signals:
    void layoutChanged();
    void attachedWindowScreenChanged(QRect availableGeometry);
};

class DesktopStateModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int activeView READ activeView WRITE setActiveView NOTIFY activeViewChanged)
    Q_PROPERTY(bool sidebarOpen READ sidebarOpen WRITE setSidebarOpen NOTIFY sidebarOpenChanged)
    Q_PROPERTY(
        QString selectedSessionId
        READ selectedSessionId
        NOTIFY selectedSessionIdChanged)
    Q_PROPERTY(
        QStringList stagedSessionIds
        READ stagedSessionIds
        NOTIFY stagedSessionIdsChanged)
    Q_PROPERTY(
        StageLayoutMode stageLayoutMode
        READ stageLayoutMode
        NOTIFY stageLayoutModeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum class StageLayoutMode {
        Grid,
        Focus,
    };
    Q_ENUM(StageLayoutMode)

    struct ScreenLayout {
        QList<QRect> availableGeometries;
        qsizetype primaryIndex = 0;
    };
    using ScreenGeometryProvider = std::function<ScreenLayout()>;

    explicit DesktopStateModel(QObject* parent = nullptr);
    explicit DesktopStateModel(
        std::unique_ptr<QSettings> settings,
        bool persistenceEnabled,
        QObject* parent = nullptr);
    explicit DesktopStateModel(
        std::unique_ptr<QSettings> settings,
        ScreenGeometryProvider screenGeometryProvider,
        bool persistenceEnabled = true,
        DesktopScreenLayoutNotifier* screenLayoutNotifier = nullptr,
        QObject* parent = nullptr);
    explicit DesktopStateModel(
        std::unique_ptr<QSettings> settings,
        QList<QRect> availableGeometries,
        qsizetype primaryIndex = 0,
        bool persistenceEnabled = true,
        DesktopScreenLayoutNotifier* screenLayoutNotifier = nullptr,
        QObject* parent = nullptr);
    ~DesktopStateModel() override;

    [[nodiscard]] int activeView() const noexcept;
    [[nodiscard]] bool sidebarOpen() const noexcept;
    [[nodiscard]] QString selectedSessionId() const;
    [[nodiscard]] QStringList stagedSessionIds() const;
    [[nodiscard]] StageLayoutMode stageLayoutMode() const noexcept;
    [[nodiscard]] QString lastError() const;

    void setActiveView(int activeView);
    void setSidebarOpen(bool sidebarOpen);
    void setSelectedSessionId(const QString& selectedSessionId);
    void attachSessionCatalog(SessionCatalogModel* sessions);
    void attachWindow(
        QWindow* window,
        std::optional<QSize> sizeOverride = std::nullopt);
    Q_INVOKABLE [[nodiscard]] bool selectSession(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool stageSession(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool unstageSession(const QString& sessionId);
    Q_INVOKABLE void enterFocusMode(const QString& sessionId = {});
    Q_INVOKABLE void exitFocusMode();
    Q_INVOKABLE void toggleFocusForSelectedSession();
    Q_INVOKABLE void selectAdjacentSession(int offset);
    Q_INVOKABLE void clearError();

public slots:
    void ingestAuthEvent(QByteArray json);
    void resetRuntimeAuthority();
    void reportRemoteRestoreDispatch(
        const QString& sessionId,
        const QString& incarnationId,
        bool accepted);
    void remoteOpenSucceeded(
        const QString& sessionId,
        const QString& incarnationId);
    void remoteOpenFailed(
        const QString& sessionId,
        const QString& incarnationId);

signals:
    void activeViewChanged();
    void sidebarOpenChanged();
    void selectedSessionIdChanged();
    void stagedSessionIdsChanged();
    void stageLayoutModeChanged();
    void lastErrorChanged();
    void remoteRestoreRequested(
        QString sessionId,
        QString incarnationId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct DeferredStageState {
        QStringList sessionIds;
        QString selectedSessionId;

        bool operator==(const DeferredStageState&) const = default;
    };

    struct State {
        int activeView = 0;
        bool sidebarOpen = true;
        QString selectedSessionId;
        QStringList stagedSessionIds;
        StageLayoutMode stageLayoutMode = StageLayoutMode::Grid;
        QHash<QString, DeferredStageState> deferredRemoteSelections;
        QString activeRemoteStageAccountKey;
        bool stageStateEstablished = false;
        QRect normalGeometry;
        bool maximized = false;

        bool operator==(const State&) const = default;
    };

    struct DecodedState {
        State state;
        bool hadPersistedStageState = false;
        bool needsMigration = false;
    };

    std::unique_ptr<QSettings> m_settings;
    ScreenGeometryProvider m_screenGeometryProvider;
    QPointer<QWindow> m_window;
    QTimer m_settleTimer;
    QTimer m_maximizeRestoreTimer;
    State m_state;
    std::optional<QRect> m_candidateNormalGeometry;
    std::optional<quint64> m_maximizeRestoreGeneration;
    std::optional<quint64> m_restoreDownGeneration;
    QString m_lastError;
    QList<QMetaObject::Connection> m_applicationScreenConnections;
    QList<QMetaObject::Connection> m_screenGeometryConnections;
    QMetaObject::Connection m_maximizeRestoreTimerConnection;
    quint64 m_windowGeneration = 0;
    quint64 m_windowStateGeneration = 0;
    int m_maximizeRestoreAttempts = 0;
    bool m_persistenceEnabled = true;
    bool m_stateDirty = false;
    bool m_restoringWindow = false;
    bool m_applyNormalGeometryOnRestore = false;
    bool m_hadPersistedStageState = false;
    bool m_didApplyInitialDefaultStage = false;
    bool m_accountSwitchPendingReconcile = false;
    bool m_hasAccountEpoch = false;
    quint64 m_accountEpoch = 0;
    QString m_activeAccountUserId;
    QSet<QString> m_knownRemoteSessionIds;
    QSet<QString> m_accountSwitchCarriedStageIds;
    QSet<QString> m_restoredRemoteIncarnations;
    QSet<QString> m_pendingRemoteRestores;
    QSet<QString> m_suppressedRemoteRestores;
    QStringList m_remoteRestoreDedupOrder;
    QPointer<SessionCatalogModel> m_sessions;
    QList<QMetaObject::Connection> m_catalogConnections;

    [[nodiscard]] static ScreenLayout applicationScreenLayout();
    [[nodiscard]] ScreenLayout screenLayout() const;
    [[nodiscard]] QRect targetAvailableGeometry(const QRect& geometry) const;
    [[nodiscard]] State defaultState() const;
    [[nodiscard]] DecodedState loadState(const State& defaults);
    [[nodiscard]] State normalizedState(State state) const;
    [[nodiscard]] static QByteArray encode(const State& state);
    [[nodiscard]] static std::optional<DecodedState> decodeV2(
        const QByteArray& json);
    [[nodiscard]] static std::optional<DecodedState> decodeV1(
        const QByteArray& json);
    [[nodiscard]] bool persist(const State& state);
    [[nodiscard]] DeferredStageState& activeDeferredState();
    [[nodiscard]] const DeferredStageState& activeDeferredState() const;
    [[nodiscard]] static QString accountStorageKey(const QString& userId);
    void preserveOutgoingAccountStage();
    void isolatePersistedRemoteStageForAccount(
        const QString& incomingAccountKey);
    void refreshActiveRemoteStageAccountKey();
    void reconcileAuthoritativeSnapshot();
    void reconcileCatalogMutation();
    void requestRemoteRestores();
    [[nodiscard]] static QString remoteRestoreIdentity(
        const QString& sessionId,
        const QString& incarnationId);
    [[nodiscard]] bool remoteRestoreSuppressed(
        const QString& sessionId);
    void unstageAutomaticRemoteRestore(const QString& sessionId);
    [[nodiscard]] bool isCurrentRemoteSession(
        const QString& sessionId) const;
    void clearRemoteRestoreGeneration();
    void publishStageChanges(
        const QString& previousSelected,
        const QStringList& previousStaged,
        StageLayoutMode previousLayout);
    void persistStageState();
    [[nodiscard]] bool isAttached(
        const QPointer<QWindow>& window,
        quint64 generation) const;
    [[nodiscard]] bool maximizeRestorePending() const;
    [[nodiscard]] bool restoreDownPending() const;
    [[nodiscard]] bool isScreenSized(const QRect& geometry) const;
    [[nodiscard]] bool isPendingRestoreDownFrame(
        const QRect& geometry) const;
    void initializeState();
    [[nodiscard]] bool writeAuthoritativeState();
    void flushDirtyState();
    void markStateDirty();
    void settleAndFlush();
    void observeNormalGeometry();
    [[nodiscard]] bool preserveConfirmedMaximizedState();
    void confirmCurrentNormalGeometry();
    [[nodiscard]] bool acceptObservedNormalGeometry(const QRect& geometry);
    void cancelCandidateGeometry();
    void handleWindowStateChanged(Qt::WindowState windowState);
    void restoreAfterSpecialState(
        const QPointer<QWindow>& window,
        quint64 windowGeneration,
        quint64 stateGeneration);
    void applyMinimumSize();
    void applyMinimumSize(const QRect& availableGeometry);
    void queueMaximizedRestore(
        const QPointer<QWindow>& window,
        quint64 generation);
    void dispatchMaximizedRestore(
        const QPointer<QWindow>& window,
        quint64 generation);
    void handleMaximizedRestoreTimeout(
        const QPointer<QWindow>& window,
        quint64 generation);
    void cancelMaximizedRestore();
    void failMaximizedRestore(
        const QPointer<QWindow>& window,
        quint64 generation);
    void clearRestoreDownPending();
    void handleScreenLayoutChanged();
    void handleAttachedWindowScreenChanged(const QRect& availableGeometry);
    void subscribeToApplicationScreens();
    void refreshScreenGeometryConnections();
    void detachWindow();
    void setError(QString error);
};

} // namespace kodosi
