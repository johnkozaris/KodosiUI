#include "app/DeepLinkController.hpp"

#include "models/AccountContextFence.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>

#include <algorithm>
#include <chrono>
#include <limits>

namespace kodosi {
namespace {

QString optionalString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    return value.isString() ? value.toString() : QString {};
}

DeepLinkController::MonotonicClock systemMonotonicClock()
{
    return [] { return std::chrono::steady_clock::now(); };
}

} // namespace

DeepLinkController::DeepLinkController(
    SessionCatalogModel& sessions,
    PendingPermissionsModel& permissions,
    ApplicationLifecycleModel& lifecycle,
    QObject* parent)
    : DeepLinkController(
          sessions,
          permissions,
          lifecycle,
          systemMonotonicClock(),
          parent)
{
}

DeepLinkController::DeepLinkController(
    SessionCatalogModel& sessions,
    PendingPermissionsModel& permissions,
    ApplicationLifecycleModel& lifecycle,
    MonotonicClock clock,
    QObject* parent)
    : DeepLinkController(
          sessions,
          permissions,
          RuntimeReadiness {
              .state = lifecycle.state(),
              .generation = lifecycle.runtimeGeneration(),
          },
          std::move(clock),
          parent)
{
    connect(
        &lifecycle,
        &ApplicationLifecycleModel::stateChanged,
        this,
        [this, &lifecycle] {
            updateRuntimeReadiness({
                .state = lifecycle.state(),
                .generation = lifecycle.runtimeGeneration(),
            });
        });
}

DeepLinkController::DeepLinkController(
    SessionCatalogModel& sessions,
    PendingPermissionsModel& permissions,
    const RuntimeReadiness readiness,
    QObject* parent)
    : DeepLinkController(
          sessions,
          permissions,
          readiness,
          systemMonotonicClock(),
          parent)
{
}

DeepLinkController::DeepLinkController(
    SessionCatalogModel& sessions,
    PendingPermissionsModel& permissions,
    const RuntimeReadiness readiness,
    MonotonicClock clock,
    QObject* parent)
    : QObject(parent)
    , m_sessions(sessions)
    , m_permissions(permissions)
    , m_runtimeReadiness(readiness)
    , m_clock(std::move(clock))
{
    Q_ASSERT(m_clock);
    Q_ASSERT(
        m_runtimeReadiness.state
            != ApplicationLifecycleModel::State::Ready
        || m_runtimeReadiness.generation > 0);
    m_pendingTimer.setSingleShot(true);
    connect(
        &m_pendingTimer,
        &QTimer::timeout,
        this,
        [this] { process(); });
    const auto processPending = [this] { process(); };
    connect(
        &sessions,
        &SessionCatalogModel::authorityStateChanged,
        this,
        processPending);
    connect(
        &sessions,
        &SessionCatalogModel::authoritativeSnapshotApplied,
        this,
        processPending);
    connect(
        &sessions,
        &QAbstractItemModel::modelReset,
        this,
        processPending);
    connect(
        &permissions,
        &PendingPermissionsModel::authorityStateChanged,
        this,
        processPending);
    connect(
        &permissions,
        &QAbstractItemModel::modelReset,
        this,
        processPending);
}

QString DeepLinkController::statusCode() const
{
    return m_statusCode;
}

qsizetype DeepLinkController::pendingCount() const noexcept
{
    return m_pending.size();
}

void DeepLinkController::enqueue(DeepLinkDestination destination)
{
    if (!DeepLinkRouter::isSafeIdentity(destination.sessionId)
        || (destination.toolUseId
            && !DeepLinkRouter::isSafeIdentity(*destination.toolUseId))) {
        reject(DeepLinkParseError::UnsafeText);
        return;
    }
    if (m_pending.size() >= MaximumPendingRoutes) {
        setStatus(QStringLiteral("queueFull"));
        emit activationRequested();
        return;
    }
    m_pending.enqueue({
        .destination = std::move(destination),
        .account = m_account,
        .incarnationId = std::nullopt,
        .remainingAuthorityTime = PendingRouteLifetime,
        .deadline = runtimeReady()
            ? std::optional<MonotonicTime>(
                  m_clock() + PendingRouteLifetime)
            : std::nullopt,
    });
    emit activationRequested();
    process();
}

void DeepLinkController::reject(const DeepLinkParseError error)
{
    switch (error) {
    case DeepLinkParseError::Oversized:
        setStatus(QStringLiteral("tooLong"));
        break;
    case DeepLinkParseError::MalformedEncoding:
        setStatus(QStringLiteral("malformedEncoding"));
        break;
    case DeepLinkParseError::UnsafeText:
        setStatus(QStringLiteral("unsafeText"));
        break;
    case DeepLinkParseError::InvalidQuery:
        setStatus(QStringLiteral("unsupportedParameters"));
        break;
    case DeepLinkParseError::UnsupportedRoute:
    case DeepLinkParseError::None:
        setStatus(QStringLiteral("invalid"));
        break;
    }
    emit activationRequested();
}

void DeepLinkController::clearStatus()
{
    setStatus({});
}

void DeepLinkController::reportNavigationResult(const bool succeeded)
{
    setStatus(succeeded ? QString {} : QStringLiteral("navigationFailed"));
}

void DeepLinkController::updateRuntimeReadiness(
    const RuntimeReadiness readiness)
{
    if (readiness.generation < m_runtimeReadiness.generation) {
        return;
    }
    if (readiness.state == ApplicationLifecycleModel::State::Ready) {
        Q_ASSERT(readiness.generation > 0);
    }
    if (m_runtimeReadiness.state == readiness.state
        && m_runtimeReadiness.generation == readiness.generation) {
        return;
    }

    const auto wasReady = runtimeReady();
    if (wasReady
        && readiness.state != ApplicationLifecycleModel::State::Ready) {
        pausePendingDeadlines();
    }
    m_runtimeReadiness = readiness;
    if (readiness.state == ApplicationLifecycleModel::State::Failed) {
        clearRuntimeAuthority();
    }
    if (!runtimeReady()) {
        m_pendingTimer.stop();
        if (!m_pending.isEmpty()) {
            setStatus(QStringLiteral("waitingRuntime"));
        }
        return;
    }

    resumePendingDeadlines();
    process();
}

void DeepLinkController::ingestAuthEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = optionalString(object, QStringLiteral("type"));
    if (type != QStringLiteral("auth.ready")
        && type != QStringLiteral("auth.required")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        return;
    }
    AccountIdentity identity {
        .userId = type == QStringLiteral("auth.ready")
            ? optionalString(object, QStringLiteral("userId"))
            : QString {},
        .epoch = *epoch,
    };
    if (m_account && identity.epoch < m_account->epoch) {
        return;
    }
    if (m_account && *m_account != identity) {
        cancelBoundRoutes(QStringLiteral("accountChanged"));
    }
    m_account = std::move(identity);
    for (auto& route : m_pending) {
        if (!route.account) {
            route.account = m_account;
        }
    }
    process();
}

void DeepLinkController::resetRuntimeAuthority()
{
    pausePendingDeadlines();
    m_pendingTimer.stop();
    m_runtimeReadiness.state =
        ApplicationLifecycleModel::State::Failed;
    clearRuntimeAuthority();
    if (!m_pending.isEmpty()) {
        setStatus(QStringLiteral("waitingRuntime"));
        emit activationRequested();
    }
}

bool DeepLinkController::runtimeReady() const noexcept
{
    return m_runtimeReadiness.state
        == ApplicationLifecycleModel::State::Ready;
}

void DeepLinkController::process()
{
    if (m_processing) {
        return;
    }
    m_processing = true;
    const auto resetProcessing =
        qScopeGuard([this] { m_processing = false; });

    if (!runtimeReady()) {
        m_pendingTimer.stop();
        if (!m_pending.isEmpty()) {
            setStatus(QStringLiteral("waitingRuntime"));
        }
        return;
    }

    while (!m_pending.isEmpty()) {
        auto& route = m_pending.head();
        if (route.deadline && m_clock() >= *route.deadline) {
            m_pending.dequeue();
            setStatus(QStringLiteral("expired"));
            emit activationRequested();
            continue;
        }
        if (!m_account || !route.account) {
            setStatus(QStringLiteral("waitingAccount"));
            schedulePendingTimeout();
            return;
        }
        if (*route.account != *m_account) {
            m_pending.dequeue();
            setStatus(QStringLiteral("accountChanged"));
            emit activationRequested();
            continue;
        }
        if (m_sessions.authorityState()
            == SessionCatalogModel::AuthorityState::Loading) {
            setStatus(QStringLiteral("waitingSessions"));
            schedulePendingTimeout();
            return;
        }
        if (m_sessions.authorityState()
            == SessionCatalogModel::AuthorityState::Failed) {
            m_pending.dequeue();
            setStatus(QStringLiteral("sessionsUnavailable"));
            emit activationRequested();
            continue;
        }
        const auto session =
            m_sessions.presentationSession(route.destination.sessionId);
        if (!session || !session->canRetainPresentation) {
            m_pending.dequeue();
            setStatus(QStringLiteral("sessionMissing"));
            emit activationRequested();
            continue;
        }
        if (!route.incarnationId) {
            route.incarnationId = m_sessions.incarnationForSession(
                route.destination.sessionId);
        }
        const auto currentIncarnation = m_sessions.incarnationForSession(
            route.destination.sessionId);
        if (!route.incarnationId || !currentIncarnation
            || *route.incarnationId != *currentIncarnation) {
            m_pending.dequeue();
            setStatus(QStringLiteral("sessionRestarted"));
            emit activationRequested();
            continue;
        }

        if (!route.destination.toolUseId) {
            const auto sessionId = route.destination.sessionId;
            m_pending.dequeue();
            setStatus({});
            emit openSessionRequested(sessionId);
            continue;
        }

        const auto resolution = m_permissions.resolveDeepLink(
            route.destination.sessionId,
            *route.incarnationId,
            *route.destination.toolUseId);
        switch (resolution.state) {
        case PendingPermissionsModel::DeepLinkResolutionState::Wait:
            setStatus(QStringLiteral("waitingApprovals"));
            schedulePendingTimeout();
            return;
        case PendingPermissionsModel::DeepLinkResolutionState::Exact: {
            const auto sessionId = route.destination.sessionId;
            m_pending.dequeue();
            setStatus({});
            emit reviewApprovalRequested(
                sessionId,
                resolution.identityToken);
            continue;
        }
        case PendingPermissionsModel::DeepLinkResolutionState::Missing: {
            const auto sessionId = route.destination.sessionId;
            m_pending.dequeue();
            setStatus(QStringLiteral("approvalMissing"));
            emit missingApprovalRequested(sessionId);
            continue;
        }
        case PendingPermissionsModel::DeepLinkResolutionState::AuthorityFailed: {
            const auto sessionId = route.destination.sessionId;
            m_pending.dequeue();
            setStatus(QStringLiteral("approvalsUnavailable"));
            emit openSessionRequested(sessionId);
            continue;
        }
        case PendingPermissionsModel::DeepLinkResolutionState::Stale:
            m_pending.dequeue();
            setStatus(QStringLiteral("sessionRestarted"));
            emit activationRequested();
            continue;
        }
    }
    m_pendingTimer.stop();
}

void DeepLinkController::schedulePendingTimeout()
{
    if (!runtimeReady() || m_pending.isEmpty()
        || !m_pending.head().deadline) {
        m_pendingTimer.stop();
        return;
    }
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
        *m_pending.head().deadline - m_clock());
    m_pendingTimer.start(static_cast<int>(
        std::clamp(
            remaining.count(),
            std::chrono::milliseconds::rep {1},
            static_cast<std::chrono::milliseconds::rep>(
                std::numeric_limits<int>::max()))));
}

void DeepLinkController::setStatus(QString code)
{
    if (m_statusCode == code) {
        return;
    }
    m_statusCode = std::move(code);
    emit statusChanged();
}

void DeepLinkController::cancelBoundRoutes(QString code)
{
    bool removed = false;
    QQueue<PendingRoute> retained;
    while (!m_pending.isEmpty()) {
        auto route = m_pending.dequeue();
        if (route.account) {
            removed = true;
        } else {
            retained.enqueue(std::move(route));
        }
    }
    m_pending = std::move(retained);
    schedulePendingTimeout();
    if (removed) {
        setStatus(std::move(code));
        emit activationRequested();
    }
}

void DeepLinkController::pausePendingDeadlines()
{
    const auto now = m_clock();
    for (auto& route : m_pending) {
        if (!route.deadline) {
            continue;
        }
        route.remainingAuthorityTime = std::max(
            std::chrono::milliseconds::zero(),
            std::chrono::ceil<std::chrono::milliseconds>(
                *route.deadline - now));
        route.deadline.reset();
    }
}

void DeepLinkController::resumePendingDeadlines()
{
    const auto now = m_clock();
    for (auto& route : m_pending) {
        if (!route.deadline) {
            route.deadline = now + route.remainingAuthorityTime;
        }
    }
}

void DeepLinkController::clearRuntimeAuthority()
{
    m_account.reset();
    for (auto& route : m_pending) {
        route.account.reset();
        route.incarnationId.reset();
    }
}

} // namespace kodosi
