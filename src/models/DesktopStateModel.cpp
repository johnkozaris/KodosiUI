#include "models/DesktopStateModel.hpp"
#include "models/AccountContextFence.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QScreen>
#include <QScopedValueRollback>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

constexpr auto settingsKey = "desktop/state.v2";
constexpr auto legacySettingsKey = "desktop/state.v1";
constexpr auto currentVersion = 2;
constexpr auto minimumWindowWidth = 820;
constexpr auto minimumWindowHeight = 560;
constexpr auto defaultWindowWidth = 1'240;
constexpr auto defaultWindowHeight = 800;
constexpr auto maximumStoredDimension = 100'000;
constexpr auto maximumStoredCoordinate = 1'000'000;
constexpr auto maximumSessionIdLength = 1'024;
constexpr auto maximumRestoredSessions = 6;
constexpr auto signedOutAccountKey = "signed-out";
constexpr auto geometrySettleDelayMs = 180;
constexpr auto maximizeAcknowledgementDelayMs = 60;
constexpr auto maximumMaximizeRestoreAttempts = 3;

std::optional<int> exactInteger(
    const QJsonObject& object,
    const QString& key)
{
    const auto value = object.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < static_cast<double>(std::numeric_limits<int>::min())
        || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

bool exactVariantType(const QVariant& value, const QMetaType type)
{
    return value.isValid() && value.metaType() == type;
}

bool validSessionId(const QString& sessionId)
{
    return sessionId.size() <= maximumSessionIdLength
        && !sessionId.contains(QChar::Null);
}

std::optional<QStringList> sessionIdList(const QJsonValue& value)
{
    if (!value.isArray()) {
        return std::nullopt;
    }
    QStringList result;
    QSet<QString> seen;
    for (const auto& entry : value.toArray()) {
        if (!entry.isString()) {
            return std::nullopt;
        }
        const auto sessionId = entry.toString();
        if (sessionId.isEmpty() || !validSessionId(sessionId)) {
            return std::nullopt;
        }
        if (!seen.contains(sessionId)) {
            seen.insert(sessionId);
            result.append(sessionId);
        }
    }
    return result;
}

QJsonArray encodeSessionIds(const QStringList& sessionIds)
{
    QJsonArray result;
    for (const auto& sessionId : sessionIds) {
        result.append(sessionId);
    }
    return result;
}

qint64 intersectionArea(const QRect& first, const QRect& second)
{
    const auto intersection = first.intersected(second);
    return intersection.isValid()
        ? static_cast<qint64>(intersection.width()) * intersection.height()
        : 0;
}

QRect centeredGeometry(const QRect& available, const QSize requestedSize)
{
    const auto minimumWidth = std::min(minimumWindowWidth, available.width());
    const auto minimumHeight =
        std::min(minimumWindowHeight, available.height());
    const auto width =
        std::clamp(requestedSize.width(), minimumWidth, available.width());
    const auto height =
        std::clamp(requestedSize.height(), minimumHeight, available.height());
    return QRect(
        available.x() + (available.width() - width) / 2,
        available.y() + (available.height() - height) / 2,
        width,
        height);
}

QRect centeredGeometry(const QRect& available)
{
    return centeredGeometry(
        available,
        QSize(defaultWindowWidth, defaultWindowHeight));
}

QRect clampGeometry(const QRect& geometry, const QRect& available)
{
    const auto minimumWidth = std::min(minimumWindowWidth, available.width());
    const auto minimumHeight =
        std::min(minimumWindowHeight, available.height());
    const auto width =
        std::clamp(geometry.width(), minimumWidth, available.width());
    const auto height =
        std::clamp(geometry.height(), minimumHeight, available.height());
    const auto maximumX = available.right() - width + 1;
    const auto maximumY = available.bottom() - height + 1;
    return QRect(
        std::clamp(geometry.x(), available.left(), maximumX),
        std::clamp(geometry.y(), available.top(), maximumY),
        width,
        height);
}

} // namespace

DesktopStateModel::DesktopStateModel(QObject* parent)
    : DesktopStateModel(std::make_unique<QSettings>(), true, parent)
{
}

DesktopStateModel::DesktopStateModel(
    std::unique_ptr<QSettings> settings,
    const bool persistenceEnabled,
    QObject* parent)
    : DesktopStateModel(
          std::move(settings),
          &DesktopStateModel::applicationScreenLayout,
          persistenceEnabled,
          nullptr,
          parent)
{
    subscribeToApplicationScreens();
}

DesktopStateModel::DesktopStateModel(
    std::unique_ptr<QSettings> settings,
    ScreenGeometryProvider screenGeometryProvider,
    const bool persistenceEnabled,
    DesktopScreenLayoutNotifier* screenLayoutNotifier,
    QObject* parent)
    : QObject(parent)
    , m_settings(std::move(settings))
    , m_screenGeometryProvider(std::move(screenGeometryProvider))
    , m_persistenceEnabled(persistenceEnabled)
{
    Q_ASSERT(m_settings != nullptr);
    Q_ASSERT(m_screenGeometryProvider);
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(geometrySettleDelayMs);
    connect(
        &m_settleTimer,
        &QTimer::timeout,
        this,
        &DesktopStateModel::settleAndFlush);
    m_maximizeRestoreTimer.setSingleShot(true);
    m_maximizeRestoreTimer.setInterval(
        maximizeAcknowledgementDelayMs);
    if (screenLayoutNotifier != nullptr) {
        connect(
            screenLayoutNotifier,
            &DesktopScreenLayoutNotifier::layoutChanged,
            this,
            &DesktopStateModel::handleScreenLayoutChanged);
        connect(
            screenLayoutNotifier,
            &DesktopScreenLayoutNotifier::attachedWindowScreenChanged,
            this,
            &DesktopStateModel::handleAttachedWindowScreenChanged);
    }
    initializeState();
}

DesktopStateModel::DesktopStateModel(
    std::unique_ptr<QSettings> settings,
    QList<QRect> availableGeometries,
    const qsizetype primaryIndex,
    const bool persistenceEnabled,
    DesktopScreenLayoutNotifier* screenLayoutNotifier,
    QObject* parent)
    : DesktopStateModel(
          std::move(settings),
          [geometries = std::move(availableGeometries), primaryIndex] {
              return ScreenLayout {
                  .availableGeometries = geometries,
                  .primaryIndex = primaryIndex,
              };
          },
          persistenceEnabled,
          screenLayoutNotifier,
          parent)
{
}

DesktopStateModel::~DesktopStateModel()
{
    detachWindow();
}

int DesktopStateModel::activeView() const noexcept
{
    return m_state.activeView;
}

bool DesktopStateModel::sidebarOpen() const noexcept
{
    return m_state.sidebarOpen;
}

QString DesktopStateModel::selectedSessionId() const
{
    return m_state.selectedSessionId;
}

QStringList DesktopStateModel::stagedSessionIds() const
{
    return m_state.stagedSessionIds;
}

DesktopStateModel::StageLayoutMode
DesktopStateModel::stageLayoutMode() const noexcept
{
    return m_state.stageLayoutMode;
}

QString DesktopStateModel::lastError() const
{
    return m_lastError;
}

void DesktopStateModel::setActiveView(const int activeView)
{
    if (activeView < 0 || activeView > 1) {
        setError(QStringLiteral(
            "Desktop state was not saved because the active view is invalid."));
        return;
    }
    if (m_state.activeView == activeView) {
        return;
    }
    m_state.activeView = activeView;
    emit activeViewChanged();
    m_stateDirty = true;
    flushDirtyState();
}

void DesktopStateModel::setSidebarOpen(const bool sidebarOpen)
{
    if (m_state.sidebarOpen == sidebarOpen) {
        return;
    }
    m_state.sidebarOpen = sidebarOpen;
    emit sidebarOpenChanged();
    m_stateDirty = true;
    flushDirtyState();
}

void DesktopStateModel::setSelectedSessionId(
    const QString& selectedSessionId)
{
    if (!validSessionId(selectedSessionId)) {
        setError(QStringLiteral(
            "Desktop state was not saved because the selected session is invalid."));
        return;
    }
    if (!selectedSessionId.isEmpty() && !m_sessions.isNull()
        && m_sessions->hasAuthoritativeSnapshot()) {
        (void)selectSession(selectedSessionId);
        return;
    }
    if (!selectedSessionId.isEmpty()) {
        if (!m_state.stagedSessionIds.contains(selectedSessionId)
            && m_state.stagedSessionIds.size()
                >= maximumRestoredSessions) {
            setError(QStringLiteral(
                "Only six sessions can be staged at once. Unstage one before selecting another."));
            return;
        }
        const auto previousSelected = m_state.selectedSessionId;
        const auto previousStaged = m_state.stagedSessionIds;
        m_state.selectedSessionId = selectedSessionId;
        if (!m_state.stagedSessionIds.contains(selectedSessionId)) {
            m_state.stagedSessionIds.append(selectedSessionId);
        }
        m_state.stageStateEstablished = true;
        m_hadPersistedStageState = true;
        publishStageChanges(
            previousSelected,
            previousStaged,
            m_state.stageLayoutMode);
        persistStageState();
        return;
    }
    if (!m_state.stagedSessionIds.isEmpty()) {
        return;
    }
    if (m_state.selectedSessionId == selectedSessionId) {
        return;
    }
    m_state.selectedSessionId = selectedSessionId;
    emit selectedSessionIdChanged();
    m_stateDirty = true;
    flushDirtyState();
}

void DesktopStateModel::attachSessionCatalog(
    SessionCatalogModel* sessions)
{
    for (const auto& connection : std::as_const(m_catalogConnections)) {
        disconnect(connection);
    }
    m_catalogConnections.clear();
    m_sessions = sessions;
    if (sessions == nullptr) {
        return;
    }
    m_catalogConnections.append(connect(
        sessions,
        &SessionCatalogModel::authoritativeSnapshotApplied,
        this,
        &DesktopStateModel::reconcileAuthoritativeSnapshot));
    const auto reconcileMutation = [this] {
        reconcileCatalogMutation();
    };
    m_catalogConnections.append(connect(
        sessions,
        &QAbstractItemModel::rowsInserted,
        this,
        reconcileMutation));
    m_catalogConnections.append(connect(
        sessions,
        &QAbstractItemModel::rowsRemoved,
        this,
        reconcileMutation));
    m_catalogConnections.append(connect(
        sessions,
        &QAbstractItemModel::dataChanged,
        this,
        reconcileMutation));
    m_catalogConnections.append(connect(
        sessions,
        &QAbstractItemModel::modelReset,
        this,
        reconcileMutation));
    if (sessions->hasAuthoritativeSnapshot()) {
        reconcileAuthoritativeSnapshot();
    }
}

bool DesktopStateModel::selectSession(const QString& sessionId)
{
    if (sessionId.isEmpty() || !validSessionId(sessionId)) {
        setError(QStringLiteral(
            "The session could not be selected because its identity is invalid."));
        return false;
    }
    const auto presentation = m_sessions.isNull()
        ? std::nullopt
        : m_sessions->presentationSession(sessionId);
    if (!m_sessions || !m_sessions->hasAuthoritativeSnapshot()
        || !presentation || !presentation->canRetainPresentation) {
        setError(QStringLiteral(
            "The session could not be selected because it is no longer available."));
        return false;
    }
    const auto previousSelected = m_state.selectedSessionId;
    const auto previousStaged = m_state.stagedSessionIds;
    const auto previousLayout = m_state.stageLayoutMode;
    auto& deferred = activeDeferredState();
    if (presentation->kind == QStringLiteral("remote")) {
        deferred.sessionIds.removeAll(sessionId);
        deferred.sessionIds.append(sessionId);
        while (deferred.sessionIds.size() > maximumRestoredSessions) {
            deferred.sessionIds.removeFirst();
        }
        deferred.selectedSessionId = sessionId;
        m_state.activeRemoteStageAccountKey =
            accountStorageKey(m_activeAccountUserId);
    } else {
        deferred.selectedSessionId.clear();
    }
    m_state.selectedSessionId = sessionId;
    m_state.stageStateEstablished = true;
    m_hadPersistedStageState = true;
    if (!m_state.stagedSessionIds.contains(sessionId)) {
        m_state.stagedSessionIds.append(sessionId);
    }
    publishStageChanges(previousSelected, previousStaged, previousLayout);
    persistStageState();
    return true;
}

bool DesktopStateModel::stageSession(const QString& sessionId)
{
    if (sessionId.isEmpty() || !validSessionId(sessionId)) {
        setError(QStringLiteral(
            "The session could not be staged because its identity is invalid."));
        return false;
    }
    const auto presentation = m_sessions.isNull()
        ? std::nullopt
        : m_sessions->presentationSession(sessionId);
    if (!m_sessions || !m_sessions->hasAuthoritativeSnapshot()
        || !presentation || !presentation->canRetainPresentation) {
        setError(QStringLiteral(
            "The session could not be staged because it is no longer available."));
        return false;
    }
    if (m_state.stagedSessionIds.contains(sessionId)) {
        return true;
    }
    m_state.stageStateEstablished = true;
    m_hadPersistedStageState = true;
    m_state.stagedSessionIds.append(sessionId);
    if (presentation->kind == QStringLiteral("remote")) {
        auto& deferred = activeDeferredState();
        deferred.sessionIds.removeAll(sessionId);
        deferred.sessionIds.append(sessionId);
        while (deferred.sessionIds.size() > maximumRestoredSessions) {
            deferred.sessionIds.removeFirst();
        }
        m_state.activeRemoteStageAccountKey =
            accountStorageKey(m_activeAccountUserId);
    }
    emit stagedSessionIdsChanged();
    persistStageState();
    return true;
}

bool DesktopStateModel::unstageSession(const QString& sessionId)
{
    if (!m_state.stagedSessionIds.contains(sessionId)) {
        return false;
    }
    const auto previousSelected = m_state.selectedSessionId;
    const auto previousStaged = m_state.stagedSessionIds;
    const auto previousLayout = m_state.stageLayoutMode;
    auto& deferred = activeDeferredState();
    m_state.stageStateEstablished = true;
    m_hadPersistedStageState = true;
    deferred.sessionIds.removeAll(sessionId);
    if (deferred.selectedSessionId == sessionId) {
        deferred.selectedSessionId.clear();
    }
    m_state.stagedSessionIds.removeAll(sessionId);
    if (m_state.selectedSessionId == sessionId) {
        m_state.selectedSessionId = m_state.stagedSessionIds.isEmpty()
            ? QString {}
            : m_state.stagedSessionIds.constLast();
    }
    if (m_state.stageLayoutMode == StageLayoutMode::Focus) {
        m_state.stageLayoutMode = StageLayoutMode::Grid;
    }
    refreshActiveRemoteStageAccountKey();
    publishStageChanges(previousSelected, previousStaged, previousLayout);
    persistStageState();
    return true;
}

void DesktopStateModel::enterFocusMode(const QString& sessionId)
{
    if (!sessionId.isEmpty() && !selectSession(sessionId)) {
        return;
    }
    if (m_state.selectedSessionId.isEmpty()
        || m_state.stageLayoutMode == StageLayoutMode::Focus) {
        return;
    }
    m_state.stageLayoutMode = StageLayoutMode::Focus;
    emit stageLayoutModeChanged();
    persistStageState();
}

void DesktopStateModel::exitFocusMode()
{
    if (m_state.stageLayoutMode == StageLayoutMode::Grid) {
        return;
    }
    m_state.stageLayoutMode = StageLayoutMode::Grid;
    emit stageLayoutModeChanged();
    persistStageState();
}

void DesktopStateModel::toggleFocusForSelectedSession()
{
    if (m_state.selectedSessionId.isEmpty()) {
        return;
    }
    if (m_state.stageLayoutMode == StageLayoutMode::Focus) {
        exitFocusMode();
    } else {
        enterFocusMode();
    }
}

void DesktopStateModel::selectAdjacentSession(const int offset)
{
    if (m_state.stagedSessionIds.isEmpty() || offset == 0) {
        return;
    }
    auto index = m_state.stagedSessionIds.indexOf(
        m_state.selectedSessionId);
    if (index < 0) {
        index = 0;
    }
    const auto count = m_state.stagedSessionIds.size();
    const auto normalizedOffset = ((offset % count) + count) % count;
    const auto nextIndex = (index + normalizedOffset) % count;
    const auto previousSelected = m_state.selectedSessionId;
    m_state.selectedSessionId = m_state.stagedSessionIds.at(nextIndex);
    auto& deferred = activeDeferredState();
    if (isCurrentRemoteSession(m_state.selectedSessionId)) {
        deferred.selectedSessionId = m_state.selectedSessionId;
        m_state.activeRemoteStageAccountKey =
            accountStorageKey(m_activeAccountUserId);
    } else {
        deferred.selectedSessionId.clear();
    }
    if (previousSelected != m_state.selectedSessionId) {
        emit selectedSessionIdChanged();
        persistStageState();
    }
}

void DesktopStateModel::clearError()
{
    setError({});
}

void DesktopStateModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        setError(QStringLiteral(
            "Desktop presentation ignored an invalid authentication event."));
        return;
    }
    const auto object = document.object();
    const auto typeValue = object.value(QStringLiteral("type"));
    if (!typeValue.isString()) {
        setError(QStringLiteral(
            "Desktop presentation ignored authentication without a type."));
        return;
    }
    const auto type = typeValue.toString();
    if (type != QStringLiteral("auth.ready")
        && type != QStringLiteral("auth.required")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        setError(QStringLiteral(
            "Desktop presentation ignored authentication without an exact account epoch."));
        return;
    }
    if (m_hasAccountEpoch && *epoch < m_accountEpoch) {
        return;
    }
    const auto userId = type == QStringLiteral("auth.ready")
        && object.value(QStringLiteral("userId")).isString()
        ? object.value(QStringLiteral("userId")).toString()
        : QString {};
    if (m_hasAccountEpoch && *epoch == m_accountEpoch
        && userId == m_activeAccountUserId) {
        return;
    }
    const auto firstAccountAuthority = !m_hasAccountEpoch;
    const auto accountChanged =
        m_hasAccountEpoch && userId != m_activeAccountUserId;
    if (accountChanged) {
        preserveOutgoingAccountStage();
    }
    const auto incomingAccountKey = accountStorageKey(userId);
    if (firstAccountAuthority) {
        isolatePersistedRemoteStageForAccount(incomingAccountKey);
        m_accountSwitchCarriedStageIds =
            QSet<QString>(
                m_state.stagedSessionIds.cbegin(),
                m_state.stagedSessionIds.cend());
        m_accountSwitchPendingReconcile = true;
    }
    m_hasAccountEpoch = true;
    m_accountEpoch = *epoch;
    m_activeAccountUserId = userId;
    if (accountChanged) {
        m_accountSwitchCarriedStageIds =
            QSet<QString>(
                m_state.stagedSessionIds.cbegin(),
                m_state.stagedSessionIds.cend());
        m_accountSwitchPendingReconcile = true;
    }
    clearRemoteRestoreGeneration();
    setError({});
}

void DesktopStateModel::resetRuntimeAuthority()
{
    m_hasAccountEpoch = false;
    m_accountEpoch = 0;
    clearRemoteRestoreGeneration();
}

void DesktopStateModel::attachWindow(
    QWindow* window,
    const std::optional<QSize> sizeOverride)
{
    if (window == nullptr) {
        setError(QStringLiteral("Desktop state could not attach to a null window."));
        return;
    }

    detachWindow();
    m_window = window;
    const auto generation = ++m_windowGeneration;
    const QPointer<QWindow> attachedWindow(window);
    window->installEventFilter(this);

    if (sizeOverride) {
        const auto available = targetAvailableGeometry(m_state.normalGeometry);
        m_state.normalGeometry =
            centeredGeometry(available, *sizeOverride);
        m_state.maximized = false;
    }

    applyMinimumSize();
    {
        QScopedValueRollback restoring(m_restoringWindow, true);
        window->setWindowState(Qt::WindowNoState);
        window->setGeometry(m_state.normalGeometry);
        window->show();
        window->setGeometry(m_state.normalGeometry);
    }

    connect(
        window,
        &QWindow::xChanged,
        this,
        [this, attachedWindow, generation] {
            if (isAttached(attachedWindow, generation)) {
                observeNormalGeometry();
            }
        });
    connect(
        window,
        &QWindow::yChanged,
        this,
        [this, attachedWindow, generation] {
            if (isAttached(attachedWindow, generation)) {
                observeNormalGeometry();
            }
        });
    connect(
        window,
        &QWindow::widthChanged,
        this,
        [this, attachedWindow, generation] {
            if (isAttached(attachedWindow, generation)) {
                observeNormalGeometry();
            }
        });
    connect(
        window,
        &QWindow::heightChanged,
        this,
        [this, attachedWindow, generation] {
            if (isAttached(attachedWindow, generation)) {
                observeNormalGeometry();
            }
        });
    connect(
        window,
        &QWindow::windowStateChanged,
        this,
        [this, attachedWindow, generation](const Qt::WindowState state) {
            if (isAttached(attachedWindow, generation)) {
                handleWindowStateChanged(state);
            }
        });
    connect(
        window,
        &QWindow::screenChanged,
        this,
        [this, attachedWindow, generation](QScreen* screen) {
            if (isAttached(attachedWindow, generation) && screen != nullptr) {
                handleAttachedWindowScreenChanged(
                    screen->availableGeometry());
            }
        });
    connect(
        window,
        &QWindow::visibleChanged,
        this,
        [this, attachedWindow, generation](const bool visible) {
            if (visible || !isAttached(attachedWindow, generation)) {
                return;
            }
            const auto maximizePending =
                m_maximizeRestoreGeneration == generation;
            confirmCurrentNormalGeometry();
            if (maximizePending) {
                cancelMaximizedRestore();
            }
            m_settleTimer.stop();
            flushDirtyState();
        });
    connect(
        window,
        &QObject::destroyed,
        this,
        [this, generation] {
            if (generation != m_windowGeneration) {
                return;
            }
            if (m_maximizeRestoreGeneration == generation) {
                (void)preserveConfirmedMaximizedState();
                cancelMaximizedRestore();
            }
            m_settleTimer.stop();
            m_window.clear();
            ++m_windowGeneration;
            clearRestoreDownPending();
            cancelCandidateGeometry();
            flushDirtyState();
        });

    if (m_state.maximized) {
        queueMaximizedRestore(attachedWindow, generation);
    }
}

bool DesktopStateModel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window
        && (event->type() == QEvent::Close
            || event->type() == QEvent::Destroy)) {
        confirmCurrentNormalGeometry();
        m_settleTimer.stop();
        flushDirtyState();
    }
    return QObject::eventFilter(watched, event);
}

DesktopStateModel::ScreenLayout DesktopStateModel::applicationScreenLayout()
{
    ScreenLayout layout;
    const auto screens = QGuiApplication::screens();
    layout.availableGeometries.reserve(screens.size());
    for (const auto* screen : screens) {
        layout.availableGeometries.append(
            screen == nullptr ? QRect {} : screen->availableGeometry());
    }
    const auto* primary = QGuiApplication::primaryScreen();
    const auto primaryIterator =
        std::find(screens.cbegin(), screens.cend(), primary);
    if (primaryIterator != screens.cend()) {
        layout.primaryIndex =
            std::distance(screens.cbegin(), primaryIterator);
    }
    return layout;
}

DesktopStateModel::ScreenLayout DesktopStateModel::screenLayout() const
{
    const auto rawLayout = m_screenGeometryProvider();
    ScreenLayout filtered;
    filtered.availableGeometries.reserve(
        rawLayout.availableGeometries.size());
    std::optional<qsizetype> mappedPrimaryIndex;
    for (qsizetype rawIndex = 0;
         rawIndex < rawLayout.availableGeometries.size();
         ++rawIndex) {
        const auto geometry = rawLayout.availableGeometries.at(rawIndex);
        if (geometry.width() < 1 || geometry.height() < 1) {
            continue;
        }
        if (rawIndex == rawLayout.primaryIndex) {
            mappedPrimaryIndex = filtered.availableGeometries.size();
        }
        filtered.availableGeometries.append(geometry);
    }
    if (filtered.availableGeometries.isEmpty()) {
        filtered.availableGeometries.append(QRect(0, 0, 1'920, 1'080));
        filtered.primaryIndex = 0;
    } else {
        filtered.primaryIndex = mappedPrimaryIndex.value_or(0);
    }
    return filtered;
}

QRect DesktopStateModel::targetAvailableGeometry(const QRect& geometry) const
{
    const auto layout = screenLayout();
    auto selectedIndex = layout.primaryIndex;
    qint64 largestArea = 0;
    for (qsizetype index = 0;
         index < layout.availableGeometries.size();
         ++index) {
        const auto area =
            intersectionArea(geometry, layout.availableGeometries.at(index));
        if (area > largestArea) {
            largestArea = area;
            selectedIndex = index;
        }
    }
    return layout.availableGeometries.at(selectedIndex);
}

DesktopStateModel::State DesktopStateModel::defaultState() const
{
    const auto layout = screenLayout();
    return State {
        .activeView = 0,
        .sidebarOpen = true,
        .selectedSessionId = {},
        .stagedSessionIds = {},
        .stageLayoutMode = StageLayoutMode::Grid,
        .deferredRemoteSelections = {},
        .activeRemoteStageAccountKey = {},
        .stageStateEstablished = false,
        .normalGeometry =
            centeredGeometry(layout.availableGeometries.at(layout.primaryIndex)),
        .maximized = false,
    };
}

DesktopStateModel::DecodedState DesktopStateModel::loadState(
    const State& defaults)
{
    const auto currentKey = QString::fromLatin1(settingsKey);
    const auto legacyKey = QString::fromLatin1(legacySettingsKey);
    const auto hasCurrent = m_settings->contains(currentKey);
    const auto stored = m_settings->value(
        hasCurrent ? currentKey : legacyKey);
    if (m_settings->status() != QSettings::NoError) {
        setError(QStringLiteral(
            "Saved desktop state could not be read. Defaults are in use."));
        return {.state = defaults};
    }
    if (!stored.isValid()) {
        setError({});
        return {.state = defaults};
    }
    if (!exactVariantType(stored, QMetaType::fromType<QByteArray>())) {
        setError(QStringLiteral(
            "Saved desktop state has an invalid type. Defaults are in use."));
        return {.state = defaults};
    }
    const auto decoded = hasCurrent
        ? decodeV2(stored.toByteArray())
        : decodeV1(stored.toByteArray());
    if (!decoded) {
        setError(QStringLiteral(
            "Saved desktop state is malformed. Defaults are in use."));
        return {.state = defaults};
    }
    setError({});
    return *decoded;
}

DesktopStateModel::State DesktopStateModel::normalizedState(State state) const
{
    state.normalGeometry = clampGeometry(
        state.normalGeometry,
        targetAvailableGeometry(state.normalGeometry));
    state.stagedSessionIds =
        state.stagedSessionIds.mid(0, maximumRestoredSessions);
    if (!state.selectedSessionId.isEmpty()
        && !state.stagedSessionIds.contains(state.selectedSessionId)) {
        state.selectedSessionId = state.stagedSessionIds.isEmpty()
            ? QString {}
            : state.stagedSessionIds.constLast();
    }
    if (state.selectedSessionId.isEmpty()
        && !state.stagedSessionIds.isEmpty()) {
        state.selectedSessionId = state.stagedSessionIds.constFirst();
    }
    state.stageLayoutMode = StageLayoutMode::Grid;
    for (auto deferred = state.deferredRemoteSelections.begin();
         deferred != state.deferredRemoteSelections.end();
         ++deferred) {
        deferred->sessionIds =
            deferred->sessionIds.mid(0, maximumRestoredSessions);
        if (!deferred->selectedSessionId.isEmpty()
            && !deferred->sessionIds.contains(
                deferred->selectedSessionId)) {
            deferred->selectedSessionId.clear();
        }
    }
    return state;
}

QByteArray DesktopStateModel::encode(const State& state)
{
    const QJsonObject normal {
        {QStringLiteral("x"), state.normalGeometry.x()},
        {QStringLiteral("y"), state.normalGeometry.y()},
        {QStringLiteral("width"), state.normalGeometry.width()},
        {QStringLiteral("height"), state.normalGeometry.height()},
    };
    const QJsonObject window {
        {QStringLiteral("normal"), normal},
        {QStringLiteral("maximized"), state.maximized},
    };
    QJsonObject deferredSelections;
    QStringList accountKeys = state.deferredRemoteSelections.keys();
    std::ranges::sort(accountKeys);
    for (const auto& accountKey : accountKeys) {
        const auto& deferred =
            state.deferredRemoteSelections.value(accountKey);
        deferredSelections.insert(
            accountKey,
            QJsonObject {
                {QStringLiteral("sessionIds"),
                 encodeSessionIds(deferred.sessionIds)},
                {QStringLiteral("selectedSessionId"),
                 deferred.selectedSessionId},
            });
    }
    const QJsonObject stage {
        {QStringLiteral("stagedSessionIds"),
         encodeSessionIds(state.stagedSessionIds)},
        {QStringLiteral("selectedSessionId"), state.selectedSessionId},
        {QStringLiteral("layoutMode"),
         QStringLiteral("grid")},
        {QStringLiteral("stateEstablished"),
         state.stageStateEstablished},
        {QStringLiteral("deferredRemoteSelections"), deferredSelections},
        {QStringLiteral("activeRemoteStageAccountKey"),
         state.activeRemoteStageAccountKey},
    };
    return QJsonDocument(QJsonObject {
        {QStringLiteral("version"), currentVersion},
        {QStringLiteral("activeView"), state.activeView},
        {QStringLiteral("sidebarOpen"), state.sidebarOpen},
        {QStringLiteral("stage"), stage},
        {QStringLiteral("window"), window},
    }).toJson(QJsonDocument::Compact);
}

std::optional<DesktopStateModel::DecodedState> DesktopStateModel::decodeV2(
    const QByteArray& json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const auto root = document.object();
    const auto version = exactInteger(root, QStringLiteral("version"));
    const auto activeView = exactInteger(root, QStringLiteral("activeView"));
    const auto sidebarOpen = root.value(QStringLiteral("sidebarOpen"));
    const auto stageValue = root.value(QStringLiteral("stage"));
    const auto windowValue = root.value(QStringLiteral("window"));
    if (!version || *version != currentVersion || !activeView
        || *activeView < 0 || *activeView > 2 || !sidebarOpen.isBool()
        || !stageValue.isObject() || !windowValue.isObject()) {
        return std::nullopt;
    }

    const auto stage = stageValue.toObject();
    const auto stagedSessionIds = sessionIdList(
        stage.value(QStringLiteral("stagedSessionIds")));
    const auto selectedSessionId =
        stage.value(QStringLiteral("selectedSessionId"));
    const auto layoutMode = stage.value(QStringLiteral("layoutMode"));
    const auto stateEstablished =
        stage.value(QStringLiteral("stateEstablished"));
    const auto deferredSelections =
        stage.value(QStringLiteral("deferredRemoteSelections"));
    const auto activeRemoteStageAccountKey =
        stage.value(QStringLiteral("activeRemoteStageAccountKey"));
    if (!stagedSessionIds || !selectedSessionId.isString()
        || !layoutMode.isString() || !stateEstablished.isBool()
        || (layoutMode.toString() != QStringLiteral("grid")
            && layoutMode.toString() != QStringLiteral("focus"))
        || !deferredSelections.isObject()) {
        return std::nullopt;
    }
    if (!activeRemoteStageAccountKey.isUndefined()
        && (!activeRemoteStageAccountKey.isString()
            || (!activeRemoteStageAccountKey.toString().isEmpty()
                && !validSessionId(
                    activeRemoteStageAccountKey.toString())))) {
        return std::nullopt;
    }
    QHash<QString, DeferredStageState> decodedDeferred;
    const auto deferredObject = deferredSelections.toObject();
    for (auto entry = deferredObject.begin();
         entry != deferredObject.end();
         ++entry) {
        if (entry.key().isEmpty() || !validSessionId(entry.key())
            || !entry.value().isObject()) {
            return std::nullopt;
        }
        const auto object = entry.value().toObject();
        const auto sessionIds = sessionIdList(
            object.value(QStringLiteral("sessionIds")));
        const auto selected =
            object.value(QStringLiteral("selectedSessionId"));
        if (!sessionIds || !selected.isString()
            || !validSessionId(selected.toString())) {
            return std::nullopt;
        }
        decodedDeferred.insert(
            entry.key(),
            {
                .sessionIds = *sessionIds,
                .selectedSessionId = selected.toString(),
            });
    }

    const auto window = windowValue.toObject();
    const auto normalValue = window.value(QStringLiteral("normal"));
    const auto maximized = window.value(QStringLiteral("maximized"));
    if (!normalValue.isObject() || !maximized.isBool()) {
        return std::nullopt;
    }
    const auto normal = normalValue.toObject();
    const auto x = exactInteger(normal, QStringLiteral("x"));
    const auto y = exactInteger(normal, QStringLiteral("y"));
    const auto width = exactInteger(normal, QStringLiteral("width"));
    const auto height = exactInteger(normal, QStringLiteral("height"));
    const auto sessionId = selectedSessionId.toString();
    if (!x || !y || !width || !height
        || std::abs(static_cast<long long>(*x)) > maximumStoredCoordinate
        || std::abs(static_cast<long long>(*y)) > maximumStoredCoordinate
        || *width < 1 || *width > maximumStoredDimension
        || *height < 1 || *height > maximumStoredDimension
        || !validSessionId(sessionId)) {
        return std::nullopt;
    }

    return DecodedState {
        .state = State {
            .activeView = *activeView == 2 ? 0 : *activeView,
            .sidebarOpen = sidebarOpen.toBool(),
            .selectedSessionId = sessionId,
            .stagedSessionIds = *stagedSessionIds,
            .stageLayoutMode = StageLayoutMode::Grid,
            .deferredRemoteSelections = std::move(decodedDeferred),
            .activeRemoteStageAccountKey =
                activeRemoteStageAccountKey.toString(),
            .stageStateEstablished = stateEstablished.toBool(),
            .normalGeometry = QRect(*x, *y, *width, *height),
            .maximized = maximized.toBool(),
        },
        .hadPersistedStageState = stateEstablished.toBool(),
        .needsMigration = *activeView == 2,
    };
}

std::optional<DesktopStateModel::DecodedState> DesktopStateModel::decodeV1(
    const QByteArray& json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const auto root = document.object();
    const auto version = exactInteger(root, QStringLiteral("version"));
    const auto activeView = exactInteger(root, QStringLiteral("activeView"));
    const auto sidebarOpen = root.value(QStringLiteral("sidebarOpen"));
    const auto selectedSessionId =
        root.value(QStringLiteral("selectedSessionId"));
    const auto windowValue = root.value(QStringLiteral("window"));
    if (!version || *version != 1 || !activeView
        || *activeView < 0 || *activeView > 2 || !sidebarOpen.isBool()
        || !selectedSessionId.isString() || !windowValue.isObject()) {
        return std::nullopt;
    }
    const auto sessionId = selectedSessionId.toString();
    if (!validSessionId(sessionId)) {
        return std::nullopt;
    }
    const auto window = windowValue.toObject();
    const auto normalValue = window.value(QStringLiteral("normal"));
    const auto maximized = window.value(QStringLiteral("maximized"));
    if (!normalValue.isObject() || !maximized.isBool()) {
        return std::nullopt;
    }
    const auto normal = normalValue.toObject();
    const auto x = exactInteger(normal, QStringLiteral("x"));
    const auto y = exactInteger(normal, QStringLiteral("y"));
    const auto width = exactInteger(normal, QStringLiteral("width"));
    const auto height = exactInteger(normal, QStringLiteral("height"));
    if (!x || !y || !width || !height
        || std::abs(static_cast<long long>(*x)) > maximumStoredCoordinate
        || std::abs(static_cast<long long>(*y)) > maximumStoredCoordinate
        || *width < 1 || *width > maximumStoredDimension
        || *height < 1 || *height > maximumStoredDimension) {
        return std::nullopt;
    }
    QStringList staged;
    if (!sessionId.isEmpty()) {
        staged.append(sessionId);
    }
    return DecodedState {
        .state = State {
            .activeView = *activeView == 2 ? 0 : *activeView,
            .sidebarOpen = sidebarOpen.toBool(),
            .selectedSessionId = sessionId,
            .stagedSessionIds = staged,
            .stageLayoutMode = StageLayoutMode::Grid,
            .deferredRemoteSelections = {},
            .activeRemoteStageAccountKey = {},
            .stageStateEstablished = !sessionId.isEmpty(),
            .normalGeometry = QRect(*x, *y, *width, *height),
            .maximized = maximized.toBool(),
        },
        .hadPersistedStageState = !sessionId.isEmpty(),
        .needsMigration = true,
    };
}

bool DesktopStateModel::persist(const State& state)
{
    const auto key = QString::fromLatin1(settingsKey);
    const auto existed = m_settings->contains(key);
    const auto previous = m_settings->value(key);
    const auto encoded = encode(state);
    m_settings->setValue(key, encoded);
    m_settings->sync();
    auto persistedValueMatches = [this, &key, &encoded] {
        const auto fileName = m_settings->fileName();
        if (fileName.isEmpty()) {
            return false;
        }
        QSettings verifier(fileName, m_settings->format());
        verifier.setFallbacksEnabled(false);
        verifier.sync();
        return verifier.status() == QSettings::NoError
            && verifier.value(key).metaType()
                == QMetaType::fromType<QByteArray>()
            && verifier.value(key).toByteArray() == encoded;
    };
    if (m_settings->status() == QSettings::NoError
        || persistedValueMatches()) {
        return true;
    }

    if (existed) {
        m_settings->setValue(key, previous);
    } else {
        m_settings->remove(key);
    }
    m_settings->sync();
    setError(QStringLiteral(
        "Desktop state could not be written. The previous stored state remains unchanged."));
    return false;
}

bool DesktopStateModel::isAttached(
    const QPointer<QWindow>& window,
    const quint64 generation) const
{
    return generation == m_windowGeneration && !window.isNull()
        && window == m_window;
}

bool DesktopStateModel::maximizeRestorePending() const
{
    return m_maximizeRestoreGeneration == m_windowGeneration;
}

bool DesktopStateModel::restoreDownPending() const
{
    return m_restoreDownGeneration == m_windowGeneration;
}

bool DesktopStateModel::isScreenSized(const QRect& geometry) const
{
    const auto layout = screenLayout();
    return std::any_of(
        layout.availableGeometries.cbegin(),
        layout.availableGeometries.cend(),
        [&geometry](const QRect& available) {
            return geometry == available;
        });
}

bool DesktopStateModel::isPendingRestoreDownFrame(
    const QRect& geometry) const
{
    if (!restoreDownPending()) {
        return false;
    }
    const auto normalized = clampGeometry(
        geometry,
        targetAvailableGeometry(geometry));
    return isScreenSized(normalized);
}

void DesktopStateModel::initializeState()
{
    const auto defaults = defaultState();
    const auto decoded = m_persistenceEnabled
        ? loadState(defaults)
        : DecodedState {.state = defaults};
    m_state = normalizedState(decoded.state);
    m_hadPersistedStageState =
        decoded.hadPersistedStageState
        || m_state.stageStateEstablished;
    if (decoded.needsMigration) {
        m_stateDirty = true;
        flushDirtyState();
    }
    if (!m_persistenceEnabled) {
        setError({});
    }
}

DesktopStateModel::DeferredStageState&
DesktopStateModel::activeDeferredState()
{
    return m_state.deferredRemoteSelections[
        accountStorageKey(m_activeAccountUserId)];
}

const DesktopStateModel::DeferredStageState&
DesktopStateModel::activeDeferredState() const
{
    static const DeferredStageState empty;
    const auto found = m_state.deferredRemoteSelections.constFind(
        accountStorageKey(m_activeAccountUserId));
    return found == m_state.deferredRemoteSelections.cend()
        ? empty
        : *found;
}

QString DesktopStateModel::accountStorageKey(const QString& userId)
{
    return userId.isEmpty()
        ? QString::fromLatin1(signedOutAccountKey)
        : userId;
}

void DesktopStateModel::preserveOutgoingAccountStage()
{
    const auto previousSelected = m_state.selectedSessionId;
    const auto previousStaged = m_state.stagedSessionIds;
    const auto previousLayout = m_state.stageLayoutMode;
    auto& outgoing = activeDeferredState();
    const auto stagedSessionIds = m_state.stagedSessionIds;
    for (const auto& sessionId : stagedSessionIds) {
        if (!m_knownRemoteSessionIds.contains(sessionId)) {
            continue;
        }
        outgoing.sessionIds.removeAll(sessionId);
        outgoing.sessionIds.append(sessionId);
    }
    while (outgoing.sessionIds.size() > maximumRestoredSessions) {
        outgoing.sessionIds.removeFirst();
    }
    if (m_knownRemoteSessionIds.contains(m_state.selectedSessionId)) {
        outgoing.selectedSessionId = m_state.selectedSessionId;
    }
    m_state.stagedSessionIds.removeIf(
        [this](const QString& sessionId) {
            return m_knownRemoteSessionIds.contains(sessionId);
        });
    if (!m_state.selectedSessionId.isEmpty()
        && !m_state.stagedSessionIds.contains(
            m_state.selectedSessionId)) {
        m_state.selectedSessionId = m_state.stagedSessionIds.isEmpty()
            ? QString {}
            : m_state.stagedSessionIds.constLast();
    }
    if (m_state.stageLayoutMode == StageLayoutMode::Focus
        && previousSelected != m_state.selectedSessionId) {
        m_state.stageLayoutMode = StageLayoutMode::Grid;
    }
    m_knownRemoteSessionIds.clear();
    m_state.activeRemoteStageAccountKey.clear();
    publishStageChanges(
        previousSelected,
        previousStaged,
        previousLayout);
    persistStageState();
}

void DesktopStateModel::isolatePersistedRemoteStageForAccount(
    const QString& incomingAccountKey)
{
    const auto persistedAccountKey =
        m_state.activeRemoteStageAccountKey;
    if (persistedAccountKey.isEmpty()
        || persistedAccountKey == incomingAccountKey) {
        return;
    }
    const auto previousSelected = m_state.selectedSessionId;
    const auto previousStaged = m_state.stagedSessionIds;
    const auto previousLayout = m_state.stageLayoutMode;
    const auto persisted =
        m_state.deferredRemoteSelections.value(persistedAccountKey);
    for (const auto& sessionId : persisted.sessionIds) {
        m_state.stagedSessionIds.removeAll(sessionId);
    }
    if (!m_state.stagedSessionIds.contains(m_state.selectedSessionId)) {
        m_state.selectedSessionId = m_state.stagedSessionIds.isEmpty()
            ? QString {}
            : m_state.stagedSessionIds.constLast();
    }
    if (previousSelected != m_state.selectedSessionId) {
        m_state.stageLayoutMode = StageLayoutMode::Grid;
    }
    m_state.activeRemoteStageAccountKey.clear();
    publishStageChanges(
        previousSelected,
        previousStaged,
        previousLayout);
    persistStageState();
}

void DesktopStateModel::refreshActiveRemoteStageAccountKey()
{
    const auto hasRemoteStage = std::ranges::any_of(
        m_state.stagedSessionIds,
        [this](const QString& sessionId) {
            return isCurrentRemoteSession(sessionId);
        });
    m_state.activeRemoteStageAccountKey = hasRemoteStage
        ? accountStorageKey(m_activeAccountUserId)
        : QString {};
}

void DesktopStateModel::reconcileAuthoritativeSnapshot()
{
    if (m_sessions.isNull()
        || !m_sessions->hasAuthoritativeSnapshot()) {
        return;
    }
    const auto sessions = m_sessions->presentationSessions();
    const auto previousSelected = m_state.selectedSessionId;
    const auto previousStaged = m_state.stagedSessionIds;
    const auto previousLayout = m_state.stageLayoutMode;

    QSet<QString> validIds;
    QSet<QString> retainedIds;
    QSet<QString> remoteIds;
    QStringList localStageReadyIds;
    for (const auto& session : sessions) {
        if (session.canRetainPresentation) {
            validIds.insert(session.id);
            retainedIds.insert(session.id);
        }
        if (session.kind == QStringLiteral("remote")
            && session.canRetainPresentation) {
            remoteIds.insert(session.id);
        }
        if (session.kind == QStringLiteral("local")
            && session.isStageReady) {
            localStageReadyIds.append(session.id);
        }
    }
    auto& deferred = activeDeferredState();
    if (m_accountSwitchPendingReconcile) {
        for (const auto& sessionId :
             std::as_const(m_accountSwitchCarriedStageIds)) {
            if (remoteIds.contains(sessionId)
                && !deferred.sessionIds.contains(sessionId)) {
                m_state.stagedSessionIds.removeAll(sessionId);
            }
        }
        m_accountSwitchCarriedStageIds.clear();
        m_accountSwitchPendingReconcile = false;
    }
    for (const auto& sessionId : std::as_const(m_state.stagedSessionIds)) {
        if (remoteIds.contains(sessionId)
            && !deferred.sessionIds.contains(sessionId)) {
            deferred.sessionIds.append(sessionId);
        }
    }
    while (deferred.sessionIds.size() > maximumRestoredSessions) {
        deferred.sessionIds.removeFirst();
    }

    auto currentValidCount = 0;
    for (const auto& sessionId : std::as_const(m_state.stagedSessionIds)) {
        if (validIds.contains(sessionId)) {
            ++currentValidCount;
        }
    }
    const auto restorationCapacity =
        std::max(0, maximumRestoredSessions - currentValidCount);
    QStringList eligibleDeferredIds;
    for (const auto& sessionId : std::as_const(deferred.sessionIds)) {
        if (validIds.contains(sessionId)
            && !remoteRestoreSuppressed(sessionId)
            && !m_state.stagedSessionIds.contains(sessionId)) {
            eligibleDeferredIds.append(sessionId);
        }
    }
    auto restoredDeferredIds =
        eligibleDeferredIds.mid(0, restorationCapacity);
    if (restorationCapacity > 0
        && !deferred.selectedSessionId.isEmpty()
        && eligibleDeferredIds.contains(deferred.selectedSessionId)
        && !restoredDeferredIds.contains(deferred.selectedSessionId)) {
        restoredDeferredIds.last() = deferred.selectedSessionId;
    }
    for (const auto& sessionId : std::as_const(restoredDeferredIds)) {
        m_state.stagedSessionIds.append(sessionId);
    }

    m_state.stagedSessionIds.removeIf(
        [&retainedIds](const QString& sessionId) {
            return !retainedIds.contains(sessionId);
        });

    if (!m_didApplyInitialDefaultStage) {
        m_didApplyInitialDefaultStage = true;
        if (!m_hadPersistedStageState
            && m_state.stagedSessionIds.isEmpty()) {
            m_state.stagedSessionIds =
                localStageReadyIds.mid(0, maximumRestoredSessions);
        } else {
            m_state.stagedSessionIds =
                m_state.stagedSessionIds.mid(
                    0,
                    maximumRestoredSessions);
        }
        m_state.stageStateEstablished = true;
        m_hadPersistedStageState = true;
    }

    const auto selectedWasPruned =
        !previousSelected.isEmpty()
        && !m_state.stagedSessionIds.contains(previousSelected);
    if (!deferred.selectedSessionId.isEmpty()
        && m_state.stagedSessionIds.contains(
            deferred.selectedSessionId)) {
        m_state.selectedSessionId = deferred.selectedSessionId;
    } else if (!m_state.selectedSessionId.isEmpty()
        && !m_state.stagedSessionIds.contains(
            m_state.selectedSessionId)) {
        m_state.selectedSessionId = m_state.stagedSessionIds.isEmpty()
            ? QString {}
            : m_state.stagedSessionIds.constLast();
    }
    if (m_state.selectedSessionId.isEmpty()
        && !m_state.stagedSessionIds.isEmpty()) {
        m_state.selectedSessionId =
            m_state.stagedSessionIds.constFirst();
    }
    if (m_state.stageLayoutMode == StageLayoutMode::Focus
        && (m_state.selectedSessionId.isEmpty()
            || selectedWasPruned)) {
        m_state.stageLayoutMode = StageLayoutMode::Grid;
    }

    publishStageChanges(
        previousSelected,
        previousStaged,
        previousLayout);
    m_knownRemoteSessionIds = remoteIds;
    refreshActiveRemoteStageAccountKey();
    persistStageState();
    requestRemoteRestores();
}

void DesktopStateModel::reconcileCatalogMutation()
{
    if (m_sessions.isNull()
        || !m_sessions->hasAuthoritativeSnapshot()) {
        return;
    }
    reconcileAuthoritativeSnapshot();
}

void DesktopStateModel::requestRemoteRestores()
{
    if (m_sessions.isNull()
        || !m_sessions->hasAuthoritativeSnapshot()) {
        return;
    }
    const auto stagedSessionIds = m_state.stagedSessionIds;
    for (const auto& sessionId : stagedSessionIds) {
        const auto presentation =
            m_sessions->presentationSession(sessionId);
        const auto incarnation =
            m_sessions->incarnationForSession(sessionId);
        if (!presentation || !presentation->isRemoteConnectable
            || presentation->isStageReady || !incarnation) {
            continue;
        }
        const auto dedupIdentity =
            remoteRestoreIdentity(sessionId, *incarnation);
        if (m_restoredRemoteIncarnations.contains(dedupIdentity)
            || m_pendingRemoteRestores.contains(dedupIdentity)
            || remoteRestoreSuppressed(sessionId)) {
            continue;
        }
        emit remoteRestoreRequested(sessionId, *incarnation);
    }
}

void DesktopStateModel::reportRemoteRestoreDispatch(
    const QString& sessionId,
    const QString& incarnationId,
    const bool accepted)
{
    if (m_sessions.isNull()
        || m_sessions->incarnationForSession(sessionId)
            != std::optional<QString> {incarnationId}
        || !m_state.stagedSessionIds.contains(sessionId)) {
        return;
    }
    const auto identity =
        remoteRestoreIdentity(sessionId, incarnationId);
    if (!accepted) {
        m_suppressedRemoteRestores.insert(identity);
        unstageAutomaticRemoteRestore(sessionId);
        return;
    }
    if (m_restoredRemoteIncarnations.contains(identity)) {
        return;
    }
    while (m_remoteRestoreDedupOrder.size()
           >= maximumRestoredSessions) {
        const auto expired =
            m_remoteRestoreDedupOrder.takeFirst();
        m_restoredRemoteIncarnations.remove(expired);
        m_pendingRemoteRestores.remove(expired);
    }
    m_remoteRestoreDedupOrder.append(identity);
    m_restoredRemoteIncarnations.insert(identity);
    m_pendingRemoteRestores.insert(identity);
}

void DesktopStateModel::remoteOpenSucceeded(
    const QString& sessionId,
    const QString& incarnationId)
{
    m_pendingRemoteRestores.remove(
        remoteRestoreIdentity(sessionId, incarnationId));
}

void DesktopStateModel::remoteOpenFailed(
    const QString& sessionId,
    const QString& incarnationId)
{
    const auto identity =
        remoteRestoreIdentity(sessionId, incarnationId);
    if (!m_pendingRemoteRestores.remove(identity)) {
        return;
    }
    m_restoredRemoteIncarnations.remove(identity);
    m_remoteRestoreDedupOrder.removeAll(identity);
    m_suppressedRemoteRestores.insert(identity);
    const auto currentIncarnation = m_sessions.isNull()
        ? std::optional<QString> {}
        : m_sessions->incarnationForSession(sessionId);
    if (!currentIncarnation || *currentIncarnation == incarnationId) {
        unstageAutomaticRemoteRestore(sessionId);
    }
}

QString DesktopStateModel::remoteRestoreIdentity(
    const QString& sessionId,
    const QString& incarnationId)
{
    return sessionId + QChar(0x1f) + incarnationId;
}

bool DesktopStateModel::remoteRestoreSuppressed(
    const QString& sessionId)
{
    if (m_sessions.isNull()) {
        return false;
    }
    const auto incarnation =
        m_sessions->incarnationForSession(sessionId);
    if (!incarnation) {
        return false;
    }
    const auto currentIdentity =
        remoteRestoreIdentity(sessionId, *incarnation);
    const auto prefix = sessionId + QChar(0x1f);
    m_suppressedRemoteRestores.removeIf(
        [&prefix, &currentIdentity](const QString& identity) {
            return identity.startsWith(prefix)
                && identity != currentIdentity;
        });
    return m_suppressedRemoteRestores.contains(currentIdentity);
}

void DesktopStateModel::unstageAutomaticRemoteRestore(
    const QString& sessionId)
{
    if (!m_state.stagedSessionIds.contains(sessionId)) {
        return;
    }
    const auto previousSelected = m_state.selectedSessionId;
    const auto previousStaged = m_state.stagedSessionIds;
    const auto previousLayout = m_state.stageLayoutMode;
    m_state.stagedSessionIds.removeAll(sessionId);
    if (m_state.selectedSessionId == sessionId) {
        m_state.selectedSessionId = m_state.stagedSessionIds.isEmpty()
            ? QString {}
            : m_state.stagedSessionIds.constLast();
    }
    if (m_state.stageLayoutMode == StageLayoutMode::Focus) {
        m_state.stageLayoutMode = StageLayoutMode::Grid;
    }
    refreshActiveRemoteStageAccountKey();
    publishStageChanges(
        previousSelected,
        previousStaged,
        previousLayout);
    persistStageState();
}

void DesktopStateModel::clearRemoteRestoreGeneration()
{
    m_restoredRemoteIncarnations.clear();
    m_pendingRemoteRestores.clear();
    m_suppressedRemoteRestores.clear();
    m_remoteRestoreDedupOrder.clear();
}

bool DesktopStateModel::isCurrentRemoteSession(
    const QString& sessionId) const
{
    if (m_sessions.isNull()) {
        return false;
    }
    const auto presentation =
        m_sessions->presentationSession(sessionId);
    return presentation
        && presentation->kind == QStringLiteral("remote");
}

void DesktopStateModel::publishStageChanges(
    const QString& previousSelected,
    const QStringList& previousStaged,
    const StageLayoutMode previousLayout)
{
    if (previousSelected != m_state.selectedSessionId) {
        emit selectedSessionIdChanged();
    }
    if (previousStaged != m_state.stagedSessionIds) {
        emit stagedSessionIdsChanged();
    }
    if (previousLayout != m_state.stageLayoutMode) {
        emit stageLayoutModeChanged();
    }
}

void DesktopStateModel::persistStageState()
{
    m_stateDirty = true;
    flushDirtyState();
}

bool DesktopStateModel::writeAuthoritativeState()
{
    if (!m_persistenceEnabled) {
        setError({});
        return true;
    }
    if (persist(m_state)) {
        setError({});
        return true;
    }
    return false;
}

void DesktopStateModel::flushDirtyState()
{
    if (m_stateDirty && writeAuthoritativeState()) {
        m_stateDirty = false;
    }
}

void DesktopStateModel::markStateDirty()
{
    m_stateDirty = true;
    if (!m_persistenceEnabled && !m_candidateNormalGeometry) {
        flushDirtyState();
        return;
    }
    m_settleTimer.start();
}

void DesktopStateModel::settleAndFlush()
{
    if (m_candidateNormalGeometry) {
        if (m_window.isNull()
            || m_window->windowState() != Qt::WindowNoState) {
            cancelCandidateGeometry();
        } else {
            const auto current = clampGeometry(
                m_window->geometry(),
                targetAvailableGeometry(m_window->geometry()));
            if (current != *m_candidateNormalGeometry) {
                m_candidateNormalGeometry = current;
                m_settleTimer.start();
                return;
            }
            (void)acceptObservedNormalGeometry(current);
            m_candidateNormalGeometry.reset();
        }
    }
    flushDirtyState();
}

void DesktopStateModel::observeNormalGeometry()
{
    if (m_window.isNull() || m_restoringWindow
        || m_window->windowState() != Qt::WindowNoState) {
        return;
    }
    const auto geometry = m_window->geometry();
    if (geometry.width() < 1 || geometry.height() < 1) {
        return;
    }
    m_candidateNormalGeometry =
        clampGeometry(geometry, targetAvailableGeometry(geometry));
    m_settleTimer.start();
}

bool DesktopStateModel::preserveConfirmedMaximizedState()
{
    if (!maximizeRestorePending()) {
        return false;
    }
    cancelCandidateGeometry();
    clearRestoreDownPending();
    if (!m_state.maximized) {
        m_state.maximized = true;
        m_stateDirty = true;
    }
    return true;
}

void DesktopStateModel::confirmCurrentNormalGeometry()
{
    if (preserveConfirmedMaximizedState()) {
        return;
    }
    if (m_window.isNull() || m_restoringWindow
        || m_window->windowState() != Qt::WindowNoState) {
        cancelCandidateGeometry();
        return;
    }
    const auto geometry = m_window->geometry();
    if (geometry.width() < 1 || geometry.height() < 1) {
        return;
    }
    const auto normalized =
        clampGeometry(geometry, targetAvailableGeometry(geometry));
    if (isPendingRestoreDownFrame(normalized)) {
        cancelCandidateGeometry();
        return;
    }
    (void)acceptObservedNormalGeometry(normalized);
    m_candidateNormalGeometry.reset();
}

bool DesktopStateModel::acceptObservedNormalGeometry(
    const QRect& geometry)
{
    if (geometry.width() < 1 || geometry.height() < 1) {
        return false;
    }
    m_state.normalGeometry = geometry;
    m_state.maximized = false;
    clearRestoreDownPending();
    m_stateDirty = true;
    return true;
}

void DesktopStateModel::cancelCandidateGeometry()
{
    m_candidateNormalGeometry.reset();
}

void DesktopStateModel::handleWindowStateChanged(
    const Qt::WindowState windowState)
{
    const auto stateGeneration = ++m_windowStateGeneration;
    if (maximizeRestorePending()) {
        if (windowState == Qt::WindowMaximized) {
            cancelCandidateGeometry();
            clearRestoreDownPending();
            m_applyNormalGeometryOnRestore = false;
            m_state.maximized = true;
            m_maximizeRestoreTimer.start();
        }
        return;
    }
    if (m_restoringWindow) {
        return;
    }
    if (windowState == Qt::WindowMaximized) {
        cancelCandidateGeometry();
        clearRestoreDownPending();
        m_applyNormalGeometryOnRestore = false;
        m_state.maximized = true;
        markStateDirty();
        return;
    }
    if (windowState == Qt::WindowFullScreen
        || windowState == Qt::WindowMinimized) {
        cancelCandidateGeometry();
        clearRestoreDownPending();
        return;
    }

    if (m_applyNormalGeometryOnRestore && !m_window.isNull()) {
        const auto generation = m_windowGeneration;
        const QPointer<QWindow> attachedWindow = m_window;
        QTimer::singleShot(
            0,
            this,
            [this, attachedWindow, generation, stateGeneration] {
                restoreAfterSpecialState(
                    attachedWindow,
                    generation,
                    stateGeneration);
            });
        return;
    }

    if (m_state.maximized && !restoreDownPending()) {
        m_restoreDownGeneration = m_windowGeneration;
    }

    const auto generation = m_windowGeneration;
    const QPointer<QWindow> attachedWindow = m_window;
    QTimer::singleShot(
        0,
        this,
        [this, attachedWindow, generation] {
            if (isAttached(attachedWindow, generation)
                && attachedWindow->windowState() == Qt::WindowNoState) {
                observeNormalGeometry();
            }
        });
}

void DesktopStateModel::restoreAfterSpecialState(
    const QPointer<QWindow>& window,
    const quint64 windowGeneration,
    const quint64 stateGeneration)
{
    if (!isAttached(window, windowGeneration)
        || stateGeneration != m_windowStateGeneration
        || !m_applyNormalGeometryOnRestore
        || window->windowState() != Qt::WindowNoState) {
        return;
    }

    m_applyNormalGeometryOnRestore = false;
    const auto geometry = clampGeometry(
        m_state.normalGeometry,
        targetAvailableGeometry(m_state.normalGeometry));
    const auto geometryChanged = geometry != m_state.normalGeometry;
    m_state.normalGeometry = geometry;
    clearRestoreDownPending();
    cancelCandidateGeometry();
    {
        QScopedValueRollback restoring(m_restoringWindow, true);
        window->setGeometry(geometry);
    }

    if (geometryChanged) {
        markStateDirty();
    }
    if (m_state.maximized) {
        queueMaximizedRestore(window, windowGeneration);
        return;
    }
    observeNormalGeometry();
}

void DesktopStateModel::applyMinimumSize()
{
    if (m_window.isNull()) {
        return;
    }
    applyMinimumSize(targetAvailableGeometry(m_state.normalGeometry));
}

void DesktopStateModel::applyMinimumSize(
    const QRect& availableGeometry)
{
    if (m_window.isNull() || availableGeometry.width() < 1
        || availableGeometry.height() < 1) {
        return;
    }
    m_window->setMinimumSize(QSize(
        std::min(minimumWindowWidth, availableGeometry.width()),
        std::min(minimumWindowHeight, availableGeometry.height())));
}

void DesktopStateModel::queueMaximizedRestore(
    const QPointer<QWindow>& window,
    const quint64 generation)
{
    cancelMaximizedRestore();
    clearRestoreDownPending();
    m_maximizeRestoreGeneration = generation;
    m_maximizeRestoreAttempts = 0;
    m_maximizeRestoreTimerConnection = connect(
        &m_maximizeRestoreTimer,
        &QTimer::timeout,
        this,
        [this, window, generation] {
            handleMaximizedRestoreTimeout(window, generation);
        });
    QTimer::singleShot(
        0,
        this,
        [this, window, generation] {
            if (m_maximizeRestoreGeneration != generation) {
                return;
            }
            dispatchMaximizedRestore(window, generation);
        });
}

void DesktopStateModel::dispatchMaximizedRestore(
    const QPointer<QWindow>& window,
    const quint64 generation)
{
    if (m_maximizeRestoreGeneration != generation
        || !isAttached(window, generation) || !m_state.maximized
        || !window->isVisible()) {
        if (m_maximizeRestoreGeneration == generation) {
            cancelMaximizedRestore();
        }
        return;
    }

    ++m_maximizeRestoreAttempts;
    {
        QScopedValueRollback restoring(m_restoringWindow, true);
        window->setWindowState(Qt::WindowMaximized);
    }
    if (m_maximizeRestoreGeneration == generation
        && isAttached(window, generation)) {
        m_maximizeRestoreTimer.start();
    }
}

void DesktopStateModel::handleMaximizedRestoreTimeout(
    const QPointer<QWindow>& window,
    const quint64 generation)
{
    if (m_maximizeRestoreGeneration != generation) {
        return;
    }
    if (!isAttached(window, generation)) {
        cancelMaximizedRestore();
        return;
    }
    if (window->windowState() == Qt::WindowMaximized) {
        cancelMaximizedRestore();
        return;
    }
    if (m_maximizeRestoreAttempts < maximumMaximizeRestoreAttempts) {
        dispatchMaximizedRestore(window, generation);
        return;
    }
    failMaximizedRestore(window, generation);
}

void DesktopStateModel::cancelMaximizedRestore()
{
    m_maximizeRestoreTimer.stop();
    disconnect(m_maximizeRestoreTimerConnection);
    m_maximizeRestoreTimerConnection = {};
    m_maximizeRestoreGeneration.reset();
    m_maximizeRestoreAttempts = 0;
}

void DesktopStateModel::failMaximizedRestore(
    const QPointer<QWindow>& window,
    const quint64 generation)
{
    if (m_maximizeRestoreGeneration != generation) {
        return;
    }
    if (!isAttached(window, generation)) {
        cancelMaximizedRestore();
        return;
    }

    cancelMaximizedRestore();
    clearRestoreDownPending();
    cancelCandidateGeometry();
    m_applyNormalGeometryOnRestore = false;
    m_state.normalGeometry = clampGeometry(
        m_state.normalGeometry,
        targetAvailableGeometry(m_state.normalGeometry));
    m_state.maximized = false;
    applyMinimumSize();
    {
        QScopedValueRollback restoring(m_restoringWindow, true);
        window->setWindowState(Qt::WindowNoState);
        window->setGeometry(m_state.normalGeometry);
    }
    m_stateDirty = true;
    flushDirtyState();
}

void DesktopStateModel::clearRestoreDownPending()
{
    m_restoreDownGeneration.reset();
}

void DesktopStateModel::handleScreenLayoutChanged()
{
    if (!m_applicationScreenConnections.isEmpty()) {
        refreshScreenGeometryConnections();
    }
    const auto windowState = m_window.isNull()
        ? Qt::WindowNoState
        : m_window->windowState();
    const auto maximizePending = maximizeRestorePending();
    if (!m_window.isNull() && windowState == Qt::WindowNoState
        && restoreDownPending()) {
        const auto confirmedGeometry = clampGeometry(
            m_state.normalGeometry,
            targetAvailableGeometry(m_state.normalGeometry));
        const auto stateChanged =
            confirmedGeometry != m_state.normalGeometry;
        m_state.normalGeometry = confirmedGeometry;
        m_state.maximized = true;
        clearRestoreDownPending();
        cancelCandidateGeometry();
        applyMinimumSize(
            targetAvailableGeometry(confirmedGeometry));
        {
            QScopedValueRollback restoring(m_restoringWindow, true);
            m_window->setGeometry(confirmedGeometry);
        }
        if (stateChanged) {
            markStateDirty();
        }
        queueMaximizedRestore(m_window, m_windowGeneration);
        return;
    }

    const auto confirmedGeometry = clampGeometry(
        m_state.normalGeometry,
        targetAvailableGeometry(m_state.normalGeometry));
    const auto geometryChanged =
        confirmedGeometry != m_state.normalGeometry;
    m_state.normalGeometry = confirmedGeometry;

    if (!m_window.isNull() && windowState == Qt::WindowNoState
        && !maximizePending) {
        const auto candidateGeometry =
            m_candidateNormalGeometry.value_or(m_window->geometry());
        if (candidateGeometry.width() > 0
            && candidateGeometry.height() > 0) {
            const auto normalizedCandidate = clampGeometry(
                candidateGeometry,
                targetAvailableGeometry(candidateGeometry));
            if (!isScreenSized(normalizedCandidate)
                && normalizedCandidate != confirmedGeometry) {
                m_candidateNormalGeometry = normalizedCandidate;
                applyMinimumSize(
                    targetAvailableGeometry(normalizedCandidate));
                {
                    QScopedValueRollback restoring(
                        m_restoringWindow,
                        true);
                    m_window->setGeometry(normalizedCandidate);
                }
                if (geometryChanged) {
                    m_stateDirty = true;
                }
                m_settleTimer.start();
                return;
            }
        }
    }

    cancelCandidateGeometry();
    applyMinimumSize();

    if (!m_window.isNull()) {
        const auto generation = m_windowGeneration;
        const QPointer<QWindow> attachedWindow = m_window;
        if (windowState == Qt::WindowMaximized
            || maximizePending) {
            m_state.maximized = true;
            QScopedValueRollback restoring(m_restoringWindow, true);
            if (!maximizePending) {
                m_window->setWindowState(Qt::WindowNoState);
            }
            m_window->setGeometry(m_state.normalGeometry);
            if (!maximizePending) {
                queueMaximizedRestore(attachedWindow, generation);
            }
        } else if (windowState == Qt::WindowNoState) {
            QScopedValueRollback restoring(m_restoringWindow, true);
            m_window->setGeometry(m_state.normalGeometry);
        } else if (windowState == Qt::WindowMinimized
                   || windowState == Qt::WindowFullScreen) {
            m_applyNormalGeometryOnRestore = true;
        }
    }
    if (geometryChanged) {
        markStateDirty();
    }
}

void DesktopStateModel::handleAttachedWindowScreenChanged(
    const QRect& availableGeometry)
{
    if (m_window.isNull() || availableGeometry.width() < 1
        || availableGeometry.height() < 1) {
        return;
    }
    applyMinimumSize(availableGeometry);
    if (restoreDownPending()
        || m_applyNormalGeometryOnRestore) {
        return;
    }
    if (m_restoringWindow
        || m_maximizeRestoreGeneration == m_windowGeneration) {
        return;
    }
    if (m_window->windowState() != Qt::WindowNoState) {
        return;
    }

    const auto geometry =
        clampGeometry(m_window->geometry(), availableGeometry);
    cancelCandidateGeometry();
    const auto changed = geometry != m_state.normalGeometry
        || m_state.maximized;
    m_state.normalGeometry = geometry;
    m_state.maximized = false;
    clearRestoreDownPending();
    {
        QScopedValueRollback restoring(m_restoringWindow, true);
        m_window->setGeometry(geometry);
    }
    if (changed) {
        markStateDirty();
    }
}

void DesktopStateModel::subscribeToApplicationScreens()
{
    auto* application = qobject_cast<QGuiApplication*>(
        QCoreApplication::instance());
    if (application == nullptr) {
        return;
    }
    m_applicationScreenConnections.append(connect(
        application,
        &QGuiApplication::screenAdded,
        this,
        [this](QScreen*) { handleScreenLayoutChanged(); }));
    m_applicationScreenConnections.append(connect(
        application,
        &QGuiApplication::screenRemoved,
        this,
        [this](QScreen*) { handleScreenLayoutChanged(); }));
    m_applicationScreenConnections.append(connect(
        application,
        &QGuiApplication::primaryScreenChanged,
        this,
        [this](QScreen*) { handleScreenLayoutChanged(); }));
    refreshScreenGeometryConnections();
}

void DesktopStateModel::refreshScreenGeometryConnections()
{
    for (const auto& connection : std::as_const(m_screenGeometryConnections)) {
        disconnect(connection);
    }
    m_screenGeometryConnections.clear();
    for (auto* screen : QGuiApplication::screens()) {
        if (screen == nullptr) {
            continue;
        }
        m_screenGeometryConnections.append(connect(
            screen,
            &QScreen::availableGeometryChanged,
            this,
            [this](const QRect&) { handleScreenLayoutChanged(); }));
    }
}

void DesktopStateModel::detachWindow()
{
    const auto detachedGeneration = m_windowGeneration;
    const auto maximizeRestorePending =
        m_maximizeRestoreGeneration == detachedGeneration;
    if (!m_window.isNull()) {
        confirmCurrentNormalGeometry();
        if (maximizeRestorePending) {
            cancelMaximizedRestore();
        }
        m_settleTimer.stop();
        flushDirtyState();
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
        m_window.clear();
    } else {
        if (maximizeRestorePending) {
            (void)preserveConfirmedMaximizedState();
            cancelMaximizedRestore();
        }
        flushDirtyState();
    }
    ++m_windowGeneration;
    cancelCandidateGeometry();
    clearRestoreDownPending();
    m_applyNormalGeometryOnRestore = false;
}

void DesktopStateModel::setError(QString error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = std::move(error);
    emit lastErrorChanged();
}

} // namespace kodosi
