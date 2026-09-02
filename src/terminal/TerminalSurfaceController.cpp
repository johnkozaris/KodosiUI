#include "terminal/TerminalSurfaceController.hpp"

#include "terminal/TerminalView.hpp"

#include <QUuid>

#include <algorithm>
#include <limits>
#include <ranges>

namespace kodosi {

TerminalSurfaceController::TerminalSurfaceController(
    TerminalSessionRegistry& registry,
    RuntimeBridge& runtime,
    SessionCatalogModel& sessions,
    QObject* parent)
    : QObject(parent)
    , m_registry(registry)
    , m_runtime(runtime)
    , m_sessions(sessions)
{
    m_attachmentRetryTimer.setSingleShot(true);
    m_attachmentRetryTimer.setInterval(250);
    connect(
        &m_attachmentRetryTimer,
        &QTimer::timeout,
        this,
        &TerminalSurfaceController::retryPendingAttachments);
    const auto reconcileBindings = [this] { reconcile(); };
    connect(
        &runtime,
        &RuntimeBridge::runningChanged,
        this,
        &TerminalSurfaceController::runtimeChanged);
    connect(
        &sessions,
        &SessionCatalogModel::authoritativeSnapshotApplied,
        this,
        &TerminalSurfaceController::catalogSnapshotApplied);
    connect(&sessions, &QAbstractItemModel::modelReset, this, reconcileBindings);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, reconcileBindings);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, reconcileBindings);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, reconcileBindings);
}

bool TerminalSurfaceController::bind(
    TerminalView* view,
    const QString& sessionId)
{
    if (view == nullptr || sessionId.isEmpty()) {
        emit attachmentRejected(
            sessionId,
            QStringLiteral("The selected terminal session is invalid."));
        return false;
    }
    const auto currentIncarnation = m_sessions.incarnationForSession(sessionId);
    const auto presentation =
        m_sessions.presentationSession(sessionId);
    if (!currentIncarnation && !m_waitingForFreshCatalog) {
        emit attachmentRejected(
            sessionId,
            QStringLiteral("The selected session is no longer available."));
        return false;
    }
    if (presentation && !presentation->canRetainPresentation) {
        emit attachmentRejected(
            sessionId,
            QStringLiteral("The selected session can no longer present a terminal."));
        return false;
    }
    const auto found = std::ranges::find_if(m_bindings, [&](const Binding& binding) {
        return binding.view == view;
    });
    if (found == m_bindings.end()) {
        m_bindings.push_back({
            .view = view,
            .sessionId = sessionId,
            .runtimeIncarnationId = currentIncarnation.value_or(QString {}),
            .notificationConnection = {},
            .attached = false,
        });
        m_bindings.back().notificationConnection = connect(
            view,
            &TerminalView::terminalNotificationRequested,
            this,
            [this, view](QString title, QString body) {
                forwardNotification(
                    view,
                    std::move(title),
                    std::move(body));
            });
        view->setTerminalCapabilities(
            presentation && presentation->canSendInput,
            presentation && presentation->canRetainFocus,
            presentation && presentation->canSendFocus,
            presentation && presentation->canResize);
        connect(view, &QObject::destroyed, this, [this, view] {
            m_bindings.removeIf([&](const Binding& binding) {
                return binding.view == view || binding.view.isNull();
            });
        });
        connect(
            view,
            &TerminalView::connectionCompleted,
            this,
            [this, view](const bool connected, const std::int32_t result) {
                const auto binding = std::ranges::find_if(
                    m_bindings,
                    [&](const Binding& value) { return value.view == view; });
                if (binding == m_bindings.end()) {
                    return;
                }
                if (connected) {
                    binding->retryAttempts = 0;
                    binding->retryPending = false;
                    binding->retryExhausted = false;
                    emit attachmentReady(binding->sessionId);
                    return;
                }
                binding->view->detach();
                binding->attached = false;
                scheduleRemoteRetry(
                    *binding,
                    QStringLiteral(
                        "The runtime rejected the terminal connection (%1).")
                        .arg(result));
            });
        connect(view, &TerminalView::terminalClosed, this, [this, view] {
            const auto binding = std::ranges::find_if(
                m_bindings,
                [&](const Binding& value) { return value.view == view; });
            if (binding != m_bindings.end()) {
                binding->attached = false;
            }
        });
        tryAttach(m_bindings.back());
        return true;
    }
    if (found->sessionId == sessionId) {
        found->view->setTerminalCapabilities(
            presentation && presentation->canSendInput,
            presentation && presentation->canRetainFocus,
            presentation && presentation->canSendFocus,
            presentation && presentation->canResize);
        tryAttach(*found);
        return true;
    }
    view->detach();
    found->sessionId = sessionId;
    found->runtimeIncarnationId = currentIncarnation.value_or(QString {});
    found->retryAttempts = 0;
    found->retryPending = false;
    found->retryExhausted = false;
    found->attached = false;
    found->view->setTerminalCapabilities(
        presentation && presentation->canSendInput,
        presentation && presentation->canRetainFocus,
        presentation && presentation->canSendFocus,
        presentation && presentation->canResize);
    tryAttach(*found);
    return true;
}

bool TerminalSurfaceController::retry(
    TerminalView* view,
    const QString& sessionId)
{
    const auto currentIncarnation =
        m_sessions.incarnationForSession(sessionId);
    const auto presentation =
        m_sessions.presentationSession(sessionId);
    if (view == nullptr || sessionId.isEmpty() || !currentIncarnation
        || !presentation || !presentation->canRetainPresentation) {
        emit attachmentRejected(
            sessionId,
            QStringLiteral("The selected session is no longer available."));
        return false;
    }
    const auto found = std::ranges::find_if(m_bindings, [&](const Binding& binding) {
        return binding.view == view;
    });
    if (found == m_bindings.end()) {
        return bind(view, sessionId);
    }
    view->detach();
    found->sessionId = sessionId;
    found->runtimeIncarnationId = *currentIncarnation;
    found->retryAttempts = 0;
    found->retryPending = false;
    found->retryExhausted = false;
    found->attached = false;
    found->view->setTerminalCapabilities(
        presentation->canSendInput,
        presentation->canRetainFocus,
        presentation->canSendFocus,
        presentation->canResize);
    tryAttach(*found);
    return true;
}

void TerminalSurfaceController::detach(TerminalView* view)
{
    const auto found = std::ranges::find_if(m_bindings, [&](const Binding& binding) {
        return binding.view == view;
    });
    if (found == m_bindings.end()) {
        return;
    }
    if (found->view != nullptr) {
        found->view->detach();
    }
    disconnect(found->notificationConnection);
    m_bindings.erase(found);
}

void TerminalSurfaceController::setNotificationSink(
    TerminalNotificationSink* sink) noexcept
{
    m_notificationSink = sink;
}

void TerminalSurfaceController::runtimeChanged(const bool running)
{
    m_attachmentRetryTimer.stop();
    m_waitingForFreshCatalog = true;
    for (auto& binding : m_bindings) {
        if (binding.view != nullptr) {
            binding.view->detach();
        }
        binding.runtimeIncarnationId.clear();
        binding.retryAttempts = 0;
        binding.retryPending = false;
        binding.retryExhausted = false;
        binding.attached = false;
    }
    Q_UNUSED(running);
}

void TerminalSurfaceController::catalogSnapshotApplied()
{
    if (!m_runtime.isRunning()) {
        return;
    }
    m_waitingForFreshCatalog = false;
    reconcile();
}

void TerminalSurfaceController::reconcile()
{
    if (!m_sessions.hasAuthoritativeSnapshot()) {
        m_waitingForFreshCatalog = true;
        for (auto& binding : m_bindings) {
            if (binding.view != nullptr) {
                binding.view->setTerminalCapabilities(
                    false,
                    false,
                    false,
                    false);
                binding.view->detach();
            }
            binding.runtimeIncarnationId.clear();
            binding.retryPending = false;
            binding.attached = false;
        }
        return;
    }
    m_waitingForFreshCatalog = false;
    for (auto binding = m_bindings.begin(); binding != m_bindings.end();) {
        if (binding->view == nullptr) {
            binding = m_bindings.erase(binding);
            continue;
        }
        const auto currentIncarnation =
            m_sessions.incarnationForSession(binding->sessionId);
        const auto presentation =
            m_sessions.presentationSession(binding->sessionId);
        if (!currentIncarnation || !presentation
            || !presentation->canRetainPresentation) {
            binding->view->detach();
            emit attachmentRejected(
                binding->sessionId,
                QStringLiteral("The terminal session was replaced or removed."));
            disconnect(binding->notificationConnection);
            binding = m_bindings.erase(binding);
            continue;
        }
        binding->view->setTerminalCapabilities(
            presentation->canSendInput,
            presentation->canRetainFocus,
            presentation->canSendFocus,
            presentation->canResize);
        if (binding->runtimeIncarnationId != *currentIncarnation) {
            binding->view->detach();
            binding->runtimeIncarnationId = *currentIncarnation;
            binding->retryAttempts = 0;
            binding->retryPending = false;
            binding->retryExhausted = false;
            binding->attached = false;
        }
        if (!m_runtime.isRunning()) {
            binding->view->detach();
            binding->attached = false;
            ++binding;
            continue;
        }
        tryAttach(*binding);
        ++binding;
    }
}

void TerminalSurfaceController::tryAttach(Binding& binding)
{
    if (binding.attached || binding.retryPending || binding.retryExhausted
        || binding.view == nullptr || !m_runtime.isRunning()
        || m_waitingForFreshCatalog || binding.runtimeIncarnationId.isEmpty()) {
        return;
    }
    if (m_nextSubscriptionGeneration
        == std::numeric_limits<std::uint64_t>::max()) {
        emit attachmentRejected(
            binding.sessionId,
            QStringLiteral("The terminal subscription generation is exhausted."));
        return;
    }
    const TerminalSubscription subscription {
        .sessionId = binding.sessionId,
        .subscriptionId =
            QUuid::createUuidV7().toString(QUuid::WithoutBraces),
        .generation = ++m_nextSubscriptionGeneration,
    };
    binding.attached = binding.view->attach(
        m_registry,
        m_runtime,
        subscription,
        binding.runtimeIncarnationId);
    if (!binding.attached) {
        scheduleRemoteRetry(
            binding,
            QStringLiteral("The runtime rejected the terminal attachment."));
    }
}

void TerminalSurfaceController::scheduleRemoteRetry(
    Binding& binding,
    QString reason)
{
    constexpr int maximumRemoteAttachmentAttempts = 20;
    const auto presentation =
        m_sessions.presentationSession(binding.sessionId);
    if (!presentation || !presentation->isRemoteConnectable) {
        emit attachmentRejected(binding.sessionId, std::move(reason));
        return;
    }
    ++binding.retryAttempts;
    if (binding.retryAttempts >= maximumRemoteAttachmentAttempts) {
        binding.retryPending = false;
        binding.retryExhausted = true;
        emit attachmentRejected(
            binding.sessionId,
            QStringLiteral(
                "The remote terminal did not become ready in time."));
        return;
    }
    binding.retryPending = true;
    if (!m_attachmentRetryTimer.isActive()) {
        m_attachmentRetryTimer.start();
    }
}

void TerminalSurfaceController::retryPendingAttachments()
{
    for (auto& binding : m_bindings) {
        if (!binding.retryPending || binding.view == nullptr) {
            continue;
        }
        binding.retryPending = false;
        tryAttach(binding);
    }
}

void TerminalSurfaceController::forwardNotification(
    TerminalView* view,
    QString title,
    QString body)
{
    if (m_notificationSink == nullptr) {
        return;
    }
    const auto binding =
        std::ranges::find_if(m_bindings, [&](const Binding& value) {
            return value.view == view;
        });
    if (binding == m_bindings.end() || !binding->attached
        || binding->runtimeIncarnationId.isEmpty()) {
        return;
    }
    m_notificationSink->receiveTerminalNotification({
        .sessionId = binding->sessionId,
        .runtimeIncarnationId = binding->runtimeIncarnationId,
        .title = std::move(title),
        .body = std::move(body),
    });
}

} // namespace kodosi
