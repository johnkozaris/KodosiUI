#include "terminal/TerminalSessionRegistry.hpp"
#include "logging/ApplicationLogStore.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <charconv>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kodosi {
namespace {

struct Entry {
    Entry(
        TerminalSurfaceIdentity value,
        TerminalSessionRegistry::Listener callbacks,
        TerminalKernelSettings settings)
        : identity(std::move(value))
        , listener(std::move(callbacks))
        , kernel(settings)
    {
    }

    TerminalSurfaceIdentity identity;
    TerminalSessionRegistry::Listener listener;
    GhosttyTerminalKernel kernel;
    std::mutex listenerMutex;
    std::uint64_t lastPublishedDisplayRevision = 0;
    std::atomic_bool needsCheckpoint = true;
    bool active = true;
};

struct SessionEntry {
    TerminalSubscription subscription;
    std::unordered_map<std::uint64_t, std::shared_ptr<Entry>> surfaces;
    std::deque<std::uint64_t> seedRefreshSurfaceGenerations;
    bool checkpointAdmitted = false;
};

bool sameSubscription(
    const TerminalSubscription& left,
    const TerminalSubscription& right) noexcept
{
    return left.sessionId == right.sessionId
        && left.subscriptionId == right.subscriptionId
        && left.generation == right.generation;
}

bool sameIdentity(
    const TerminalSurfaceIdentity& left,
    const TerminalSurfaceIdentity& right) noexcept
{
    return sameSubscription(left.subscription, right.subscription)
        && left.surfaceGeneration == right.surfaceGeneration;
}

std::string sessionKey(const QString& sessionId)
{
    const auto utf8 = sessionId.toUtf8();
    return {utf8.constData(), static_cast<std::size_t>(utf8.size())};
}

bool publishFrame(const std::shared_ptr<Entry>& entry, GhosttyTerminalKernel::Result result)
{
    std::scoped_lock lock(entry->listenerMutex);
    if (!entry->active) {
        return false;
    }
    if (result) {
        if ((*result)->displayRevision <= entry->lastPublishedDisplayRevision) {
            return true;
        }
        entry->lastPublishedDisplayRevision = (*result)->displayRevision;
        if (entry->listener.frameChanged) {
            entry->listener.frameChanged(*result);
        }
    } else if (entry->listener.failed) {
        entry->listener.failed(std::move(result.error()));
    }
    return true;
}

void publishFailure(
    const std::shared_ptr<Entry>& entry,
    GhosttyTerminalKernel::Failure failure)
{
    std::scoped_lock lock(entry->listenerMutex);
    if (entry->active && entry->listener.failed) {
        entry->listener.failed(std::move(failure));
    }
}

template<typename Callback, typename Value>
void publishValue(
    const std::shared_ptr<Entry>& entry,
    Callback TerminalSessionRegistry::Listener::* callback,
    Value value)
{
    std::scoped_lock lock(entry->listenerMutex);
    if (entry->active && entry->listener.*callback) {
        (entry->listener.*callback)(std::move(value));
    }
}

bool jsonWhitespace(const char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

std::optional<std::uint64_t> unsignedJsonField(
    const QByteArray& json,
    const QByteArray& field)
{
    const auto needle = QByteArrayLiteral("\"") + field + QByteArrayLiteral("\"");
    const auto key = json.indexOf(needle);
    if (key < 0 || json.indexOf(needle, key + needle.size()) >= 0) {
        return std::nullopt;
    }

    auto cursor = key + needle.size();
    while (cursor < json.size() && jsonWhitespace(json[cursor])) {
        ++cursor;
    }
    if (cursor >= json.size() || json[cursor] != ':') {
        return std::nullopt;
    }
    ++cursor;
    while (cursor < json.size() && jsonWhitespace(json[cursor])) {
        ++cursor;
    }
    const auto start = cursor;
    while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9') {
        ++cursor;
    }
    if (cursor == start) {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    const auto* first = json.constData() + start;
    const auto* last = json.constData() + cursor;
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc {} || parsed.ptr != last) {
        return std::nullopt;
    }
    while (cursor < json.size() && jsonWhitespace(json[cursor])) {
        ++cursor;
    }
    if (cursor >= json.size() || (json[cursor] != ',' && json[cursor] != '}')) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint64_t> checkedProduct(
    const std::optional<std::uint64_t> left,
    const std::optional<std::uint64_t> right)
{
    if (!left || !right || (*right != 0 && *left > std::numeric_limits<std::uint64_t>::max() / *right)) {
        return std::nullopt;
    }
    return *left * *right;
}

void deactivate(const std::shared_ptr<Entry>& entry)
{
    std::scoped_lock lock(entry->listenerMutex);
    entry->active = false;
}

} // namespace

class TerminalSessionRegistry::Impl final {
public:
    static constexpr std::size_t maximumSurfacesPerSession = 8;
    static constexpr std::size_t maximumSeedRefreshCorrelations = 64;

    std::mutex mutex;
    std::unordered_map<std::string, SessionEntry> sessions;

    std::shared_ptr<Entry> exact(const TerminalSurfaceIdentity& identity)
    {
        std::scoped_lock lock(mutex);
        const auto session = sessions.find(
            sessionKey(identity.subscription.sessionId));
        if (session == sessions.end()
            || !sameSubscription(
                session->second.subscription,
                identity.subscription)) {
            return {};
        }
        const auto surface =
            session->second.surfaces.find(identity.surfaceGeneration);
        if (surface != session->second.surfaces.end()
            && sameIdentity(surface->second->identity, identity)) {
            return surface->second;
        }
        if (identity.surfaceGeneration == 0
            && session->second.surfaces.size() == 1) {
            return session->second.surfaces.cbegin()->second;
        }
        return {};
    }

    std::vector<std::shared_ptr<Entry>> exact(
        const TerminalSubscription& subscription)
    {
        std::scoped_lock lock(mutex);
        const auto session = sessions.find(sessionKey(subscription.sessionId));
        if (session == sessions.end()
            || !sameSubscription(session->second.subscription, subscription)) {
            return {};
        }
        std::vector<std::shared_ptr<Entry>> entries;
        entries.reserve(session->second.surfaces.size());
        for (const auto& [_, entry] : session->second.surfaces) {
            entries.push_back(entry);
        }
        return entries;
    }

    std::shared_ptr<Entry> primary(
        const TerminalSubscription& subscription)
    {
        std::scoped_lock lock(mutex);
        const auto session = sessions.find(sessionKey(subscription.sessionId));
        if (session == sessions.end()
            || !sameSubscription(session->second.subscription, subscription)
            || session->second.surfaces.empty()) {
            return {};
        }
        const auto primary = std::ranges::min_element(
            session->second.surfaces,
            {},
            [](const auto& value) { return value.first; });
        return primary->second;
    }

    struct CheckpointTargets {
        std::vector<std::shared_ptr<Entry>> entries;
        bool seedOnly;
    };

    std::optional<CheckpointTargets> checkpointTargets(
        const TerminalSubscription& subscription)
    {
        std::scoped_lock lock(mutex);
        const auto session = sessions.find(sessionKey(subscription.sessionId));
        if (session == sessions.end()
            || !sameSubscription(
                session->second.subscription,
                subscription)) {
            return std::nullopt;
        }
        CheckpointTargets targets {
            .entries = {},
            .seedOnly =
                !session->second.seedRefreshSurfaceGenerations.empty(),
        };
        if (targets.seedOnly) {
            while (!session->second.seedRefreshSurfaceGenerations.empty()) {
                const auto surfaceGeneration =
                    session->second.seedRefreshSurfaceGenerations.front();
                session->second.seedRefreshSurfaceGenerations.pop_front();
                const auto surface =
                    session->second.surfaces.find(surfaceGeneration);
                if (surface != session->second.surfaces.end()
                    && surface->second->needsCheckpoint.load(
                        std::memory_order_acquire)) {
                    targets.entries.push_back(surface->second);
                    break;
                }
            }
        } else {
            targets.entries.reserve(session->second.surfaces.size());
            for (const auto& [_, entry] : session->second.surfaces) {
                if (entry->needsCheckpoint.load(std::memory_order_acquire)) {
                    targets.entries.push_back(entry);
                }
            }
        }
        return targets;
    }

    std::optional<std::vector<std::shared_ptr<Entry>>>
    pendingCheckpointTargets(
        const TerminalSubscription& subscription,
        const bool completeSeedRefresh)
    {
        std::scoped_lock lock(mutex);
        const auto session = sessions.find(sessionKey(subscription.sessionId));
        if (session == sessions.end()
            || !sameSubscription(
                session->second.subscription,
                subscription)) {
            return std::nullopt;
        }
        if (completeSeedRefresh) {
            return std::vector<std::shared_ptr<Entry>> {};
        }
        std::vector<std::shared_ptr<Entry>> entries;
        entries.reserve(session->second.surfaces.size());
        for (const auto& [_, entry] : session->second.surfaces) {
            if (entry->needsCheckpoint.load(std::memory_order_acquire)) {
                entries.push_back(entry);
            }
        }
        if (entries.empty()) {
            session->second.checkpointAdmitted = true;
        }
        return entries;
    }
};

TerminalSessionRegistry::TerminalSessionRegistry()
    : m_impl(std::make_unique<Impl>())
{
}

TerminalSessionRegistry::~TerminalSessionRegistry()
{
    std::vector<std::shared_ptr<Entry>> entries;
    {
        std::scoped_lock lock(m_impl->mutex);
        for (const auto& [_, session] : m_impl->sessions) {
            entries.reserve(entries.size() + session.surfaces.size());
            for (const auto& [__, entry] : session.surfaces) {
                entries.push_back(entry);
            }
        }
        m_impl->sessions.clear();
    }
    for (const auto& entry : entries) {
        deactivate(entry);
    }
}

bool TerminalSessionRegistry::registerSession(
    TerminalSubscription subscription,
    Listener listener,
    TerminalKernelSettings settings)
{
    return registerSurface(
        {
            .subscription = std::move(subscription),
            .surfaceGeneration = 0,
        },
        std::move(listener),
        settings)
        .has_value();
}

std::optional<TerminalSessionRegistry::SurfaceRegistration>
TerminalSessionRegistry::registerSurface(
    TerminalSurfaceIdentity identity,
    Listener listener,
    TerminalKernelSettings settings)
{
    if (identity.subscription.sessionId.isEmpty()
        || identity.subscription.subscriptionId.isEmpty()) {
        return std::nullopt;
    }
    auto entry = std::make_shared<Entry>(
        std::move(identity),
        std::move(listener),
        settings);
    const auto key = sessionKey(entry->identity.subscription.sessionId);
    std::scoped_lock lock(m_impl->mutex);
    auto session = m_impl->sessions.find(key);
    if (session == m_impl->sessions.end()) {
        SessionEntry created {
            .subscription = entry->identity.subscription,
            .surfaces = {},
            .seedRefreshSurfaceGenerations = {},
            .checkpointAdmitted = false,
        };
        created.surfaces.emplace(
            entry->identity.surfaceGeneration,
            std::move(entry));
        m_impl->sessions.emplace(key, std::move(created));
        return SurfaceRegistration {
            .requiresConnection = true,
            .requiresRefresh = false,
        };
    }
    if (!sameSubscription(
            session->second.subscription,
            entry->identity.subscription)
        || session->second.surfaces.contains(
            entry->identity.surfaceGeneration)
        || session->second.surfaces.size()
            >= Impl::maximumSurfacesPerSession) {
        return std::nullopt;
    }
    const auto requiresRefresh = session->second.checkpointAdmitted;
    if (requiresRefresh
        && session->second.seedRefreshSurfaceGenerations.size()
            >= Impl::maximumSeedRefreshCorrelations) {
        return std::nullopt;
    }
    if (requiresRefresh) {
        session->second.seedRefreshSurfaceGenerations.push_back(
            entry->identity.surfaceGeneration);
    }
    session->second.surfaces.emplace(
        entry->identity.surfaceGeneration,
        std::move(entry));
    return SurfaceRegistration {
        .requiresConnection = false,
        .requiresRefresh = requiresRefresh,
    };
}

void TerminalSessionRegistry::cancelSurfaceRefresh(
    const TerminalSurfaceIdentity& identity)
{
    std::scoped_lock lock(m_impl->mutex);
    const auto session = m_impl->sessions.find(
        sessionKey(identity.subscription.sessionId));
    if (session == m_impl->sessions.end()
        || !sameSubscription(
            session->second.subscription,
            identity.subscription)) {
        return;
    }
    std::erase(
        session->second.seedRefreshSurfaceGenerations,
        identity.surfaceGeneration);
}

void TerminalSessionRegistry::unregisterSession(const TerminalSubscription& subscription)
{
    (void)unregisterSurface({
        .subscription = subscription,
        .surfaceGeneration = 0,
    });
}

bool TerminalSessionRegistry::unregisterSurface(
    const TerminalSurfaceIdentity& identity)
{
    std::scoped_lock lock(m_impl->mutex);
    const auto session = m_impl->sessions.find(
        sessionKey(identity.subscription.sessionId));
    if (session == m_impl->sessions.end()
        || !sameSubscription(
            session->second.subscription,
            identity.subscription)) {
        return false;
    }
    const auto surface =
        session->second.surfaces.find(identity.surfaceGeneration);
    if (surface == session->second.surfaces.end()
        || !sameIdentity(surface->second->identity, identity)) {
        return false;
    }
    std::erase(
        session->second.seedRefreshSurfaceGenerations,
        identity.surfaceGeneration);
    deactivate(surface->second);
    session->second.surfaces.erase(surface);
    if (session->second.surfaces.empty()) {
        m_impl->sessions.erase(session);
        return true;
    }
    return false;
}

std::expected<QByteArray, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::encodeKey(
    const TerminalSurfaceIdentity& identity,
    TerminalKeyEvent keyEvent)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal key input belongs to an unregistered surface."),
        });
    }
    return entry->kernel.encodeKey(
        identity.subscription,
        std::move(keyEvent));
}

std::expected<QByteArray, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::encodeKey(
    const TerminalSubscription& subscription,
    TerminalKeyEvent keyEvent)
{
    return encodeKey(
        {
            .subscription = subscription,
            .surfaceGeneration = 0,
        },
        std::move(keyEvent));
}

std::expected<QByteArray, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::encodePaste(
    const TerminalSurfaceIdentity& identity,
    QByteArray text)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal paste belongs to an unregistered surface."),
        });
    }
    return entry->kernel.encodePaste(
        identity.subscription,
        std::move(text));
}

std::expected<QByteArray, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::encodePaste(
    const TerminalSubscription& subscription,
    QByteArray text)
{
    return encodePaste(
        {
            .subscription = subscription,
            .surfaceGeneration = 0,
        },
        std::move(text));
}

std::expected<QByteArray, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::encodeMouse(
    const TerminalSurfaceIdentity& identity,
    TerminalMouseEvent event)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal mouse input belongs to an unregistered surface."),
        });
    }
    return entry->kernel.encodeMouse(
        identity.subscription,
        std::move(event));
}

std::expected<QByteArray, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::encodeMouse(
    const TerminalSubscription& subscription,
    TerminalMouseEvent event)
{
    return encodeMouse(
        {
            .subscription = subscription,
            .surfaceGeneration = 0,
        },
        std::move(event));
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::scrollViewport(
    const TerminalSurfaceIdentity& identity,
    const int rows)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal scrolling belongs to an unregistered surface."),
        });
    }
    auto result = entry->kernel.scrollViewport(identity.subscription, rows);
    publishFrame(entry, result);
    return result;
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::scrollViewport(
    const TerminalSubscription& subscription,
    const int rows)
{
    return scrollViewport(
        {
            .subscription = subscription,
            .surfaceGeneration = 0,
        },
        rows);
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::scrollViewportToBottom(
    const TerminalSurfaceIdentity& identity)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal scrolling belongs to an unregistered surface."),
        });
    }
    const auto prior = entry->kernel.frame();
    auto result = entry->kernel.scrollViewportToBottom(identity.subscription);
    if (!result || *result != prior) {
        publishFrame(entry, result);
    }
    return result;
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::scrollViewportToBottom(
    const TerminalSubscription& subscription)
{
    return scrollViewportToBottom({
        .subscription = subscription,
        .surfaceGeneration = 0,
    });
}

std::expected<QString, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::linkAt(
    const TerminalSurfaceIdentity& identity,
    const std::uint64_t viewportRevision,
    const std::uint16_t column,
    const std::uint16_t row)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal link lookup belongs to an unregistered surface."),
        });
    }
    return entry->kernel.linkAt(
        identity.subscription,
        viewportRevision,
        column,
        row);
}

std::expected<QString, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::linkAt(
    const TerminalSubscription& subscription,
    const std::uint64_t viewportRevision,
    const std::uint16_t column,
    const std::uint16_t row)
{
    return linkAt(
        {
            .subscription = subscription,
            .surfaceGeneration = 0,
        },
        viewportRevision,
        column,
        row);
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::beginSelection(
    const TerminalSurfaceIdentity& identity,
    const std::uint64_t viewportRevision,
    const std::uint16_t column,
    const std::uint16_t row,
    const bool rectangular)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal selection belongs to an unregistered surface."),
        });
    }
    auto result = entry->kernel.beginSelection(
        identity.subscription,
        viewportRevision,
        column,
        row,
        rectangular);
    if (result || !result.error().isRecoverableHitTestRace()) {
        publishFrame(entry, result);
    }
    return result;
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::beginSelection(
    const TerminalSubscription& subscription,
    const std::uint64_t viewportRevision,
    const std::uint16_t column,
    const std::uint16_t row,
    const bool rectangular)
{
    return beginSelection(
        {
            .subscription = subscription,
            .surfaceGeneration = 0,
        },
        viewportRevision,
        column,
        row,
        rectangular);
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::updateSelection(
    const TerminalSurfaceIdentity& identity,
    const std::uint64_t viewportRevision,
    const std::uint16_t column,
    const std::uint16_t row)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal selection belongs to an unregistered surface."),
        });
    }
    auto result = entry->kernel.updateSelection(
        identity.subscription,
        viewportRevision,
        column,
        row);
    if (result || !result.error().isRecoverableHitTestRace()) {
        publishFrame(entry, result);
    }
    return result;
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::updateSelection(
    const TerminalSubscription& subscription,
    const std::uint64_t viewportRevision,
    const std::uint16_t column,
    const std::uint16_t row)
{
    return updateSelection(
        {
            .subscription = subscription,
            .surfaceGeneration = 0,
        },
        viewportRevision,
        column,
        row);
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::clearSelection(
    const TerminalSurfaceIdentity& identity)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal selection belongs to an unregistered surface."),
        });
    }
    auto result = entry->kernel.clearSelection(identity.subscription);
    publishFrame(entry, result);
    return result;
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::clearSelection(
    const TerminalSubscription& subscription)
{
    return clearSelection({
        .subscription = subscription,
        .surfaceGeneration = 0,
    });
}

std::expected<QString, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::selectedText(const TerminalSurfaceIdentity& identity)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal selection belongs to an unregistered surface."),
        });
    }
    return entry->kernel.selectedText(identity.subscription);
}

std::expected<QString, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::selectedText(const TerminalSubscription& subscription)
{
    return selectedText({
        .subscription = subscription,
        .surfaceGeneration = 0,
    });
}

GhosttyTerminalKernel::ConfigureResult TerminalSessionRegistry::configure(
    const TerminalSurfaceIdentity& identity,
    TerminalKernelSettings settings)
{
    auto entry = m_impl->exact(identity);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal settings belong to an unregistered surface."),
        });
    }
    auto result = entry->kernel.configure(settings);
    if (!result) {
        publishFailure(entry, result.error());
    } else if (result->has_value()) {
        publishFrame(entry, GhosttyTerminalKernel::Result {**result});
    }
    return result;
}

GhosttyTerminalKernel::ConfigureResult TerminalSessionRegistry::configure(
    const TerminalSubscription& subscription,
    TerminalKernelSettings settings)
{
    return configure(
        {
            .subscription = subscription,
            .surfaceGeneration = 0,
        },
        settings);
}

void TerminalSessionRegistry::receiveData(TerminalData data) noexcept
{
    ScopedPerformanceSpan span(
        PerformanceCategory::Terminal,
        QStringLiteral("frame.apply"),
        8);
    const auto entries = m_impl->exact(data.subscription);
    if (entries.empty()) {
        span.setOutcome(QStringLiteral("stale"));
        return;
    }
    bool current = false;
    for (const auto& entry : entries) {
        if (entry->needsCheckpoint.load(std::memory_order_acquire)) {
            continue;
        }
        current = publishFrame(entry, entry->kernel.applyData(data)) || current;
    }
    if (!current) {
        span.setOutcome(QStringLiteral("inactive"));
    }
}

void TerminalSessionRegistry::receiveControl(TerminalControl control) noexcept
{
    ScopedPerformanceSpan span(
        PerformanceCategory::Terminal,
        QStringLiteral("frame.control"),
        8);
    const auto entries = m_impl->exact(control.subscription);
    if (entries.empty()) {
        span.setOutcome(QStringLiteral("stale"));
        return;
    }
    const auto publishFailureToAll = [&entries](
                                         GhosttyTerminalKernel::Failure failure) {
        for (const auto& entry : entries) {
            publishFailure(entry, failure);
        }
    };

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(control.json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        span.setOutcome(QStringLiteral("invalid"));
        publishFailureToAll({
            GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
            QStringLiteral("Terminal control frame is not valid JSON."),
        });
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("Resize")) {
        const auto rows = unsignedJsonField(control.json, QByteArrayLiteral("rows"));
        const auto columns = unsignedJsonField(control.json, QByteArrayLiteral("cols"));
        const auto atSequence =
            unsignedJsonField(control.json, QByteArrayLiteral("at_sequence"));
        if (!rows || !columns || !atSequence || *rows == 0
            || *rows > std::numeric_limits<std::uint16_t>::max()
            || *columns == 0
            || *columns > std::numeric_limits<std::uint16_t>::max()) {
            publishFailureToAll({
                GhosttyTerminalKernel::Failure::Code::ResourceLimit,
                QStringLiteral("Terminal resize control fields are invalid."),
            });
            return;
        }
        for (const auto& entry : entries) {
            if (entry->needsCheckpoint.load(std::memory_order_acquire)) {
                continue;
            }
            publishFrame(
                entry,
                entry->kernel.applyResize(
                control.subscription,
                *atSequence,
                static_cast<std::uint16_t>(*rows),
                static_cast<std::uint16_t>(*columns)));
        }
    } else if (type == QStringLiteral("Closed")) {
        const auto finalSequence =
            unsignedJsonField(control.json, QByteArrayLiteral("finalSequence"));
        if (!finalSequence) {
            publishFailureToAll({
                GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal close control fields are invalid."),
            });
            return;
        }
        for (const auto& entry : entries) {
            std::scoped_lock lock(entry->listenerMutex);
            if (entry->active && entry->listener.closed) {
                entry->listener.closed();
            }
        }
    } else if (type == QStringLiteral("term.focusApplied")
        || type == QStringLiteral("term.focusRejected")) {
        const auto requestId = object.value(QStringLiteral("requestId"));
        const auto runtimeIncarnationId =
            object.value(QStringLiteral("runtimeIncarnationId"));
        const auto rejected = type == QStringLiteral("term.focusRejected");
        const auto reason = object.value(QStringLiteral("reason"));
        if (!requestId.isString() || !runtimeIncarnationId.isString()
            || (rejected && !reason.isString())) {
            publishFailureToAll({
                GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal focus result fields are invalid."),
            });
            return;
        }
        const TerminalFocusOutcome outcome {
            .applied = !rejected,
            .requestId = requestId.toString(),
            .runtimeIncarnationId = runtimeIncarnationId.toString(),
            .reason = rejected ? reason.toString() : QString {},
        };
        for (const auto& entry : entries) {
            publishValue(
                entry,
                &TerminalSessionRegistry::Listener::focusCompleted,
                outcome);
        }
    } else if (type == QStringLiteral("term.resizeApplied")
        || type == QStringLiteral("term.resizeRejected")) {
        const auto sessionId = object.value(QStringLiteral("sessionId"));
        const auto requestId = object.value(QStringLiteral("requestId"));
        const auto expectedRuntimeIncarnationId =
            object.value(QStringLiteral("expectedRuntimeIncarnationId"));
        const auto subscriptionId = object.value(QStringLiteral("subscriptionId"));
        const auto rejected = type == QStringLiteral("term.resizeRejected");
        const auto reason = object.value(QStringLiteral("reason"));
        const auto subscriptionGeneration =
            unsignedJsonField(control.json, QByteArrayLiteral("subscriptionGeneration"));
        const auto surfaceGeneration =
            unsignedJsonField(control.json, QByteArrayLiteral("surfaceGeneration"));
        const auto columns = unsignedJsonField(control.json, QByteArrayLiteral("cols"));
        const auto rows = unsignedJsonField(control.json, QByteArrayLiteral("rows"));
        const auto widthPixels =
            unsignedJsonField(control.json, QByteArrayLiteral("widthPixels"));
        const auto heightPixels =
            unsignedJsonField(control.json, QByteArrayLiteral("heightPixels"));
        const auto cellWidthPixels =
            unsignedJsonField(control.json, QByteArrayLiteral("cellWidthPixels"));
        const auto cellHeightPixels =
            unsignedJsonField(control.json, QByteArrayLiteral("cellHeightPixels"));
        const auto widthProduct = checkedProduct(columns, cellWidthPixels);
        const auto heightProduct = checkedProduct(rows, cellHeightPixels);
        if (!sessionId.isString() || !requestId.isString()
            || !expectedRuntimeIncarnationId.isString()
            || !subscriptionId.isString()
            || (rejected && !reason.isString()) || !subscriptionGeneration
            || !surfaceGeneration || !columns || !rows || !widthPixels
            || !heightPixels || !cellWidthPixels || !cellHeightPixels
            || *columns == 0 || *columns > std::numeric_limits<std::uint16_t>::max()
            || *rows == 0 || *rows > std::numeric_limits<std::uint16_t>::max()
            || *widthPixels > std::numeric_limits<std::uint32_t>::max()
            || *heightPixels > std::numeric_limits<std::uint32_t>::max()
            || *cellWidthPixels == 0
            || *cellWidthPixels > std::numeric_limits<std::uint32_t>::max()
            || *cellHeightPixels == 0
            || *cellHeightPixels > std::numeric_limits<std::uint32_t>::max()
            || !widthProduct || !heightProduct
            || *widthProduct != *widthPixels || *heightProduct != *heightPixels
            || sessionId.toString() != control.subscription.sessionId
            || subscriptionId.toString() != control.subscription.subscriptionId
            || *subscriptionGeneration != control.subscription.generation) {
            publishFailureToAll({
                GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal resize result fields are invalid."),
            });
            return;
        }
        const TerminalResizeOutcome outcome {
                .applied = !rejected,
                .sessionId = sessionId.toString(),
                .requestId = requestId.toString(),
                .expectedRuntimeIncarnationId =
                    expectedRuntimeIncarnationId.toString(),
                .subscriptionId = subscriptionId.toString(),
                .subscriptionGeneration = *subscriptionGeneration,
                .surfaceGeneration = *surfaceGeneration,
                .columns = static_cast<std::uint16_t>(*columns),
                .rows = static_cast<std::uint16_t>(*rows),
                .widthPixels = static_cast<std::uint32_t>(*widthPixels),
                .heightPixels = static_cast<std::uint32_t>(*heightPixels),
                .cellWidthPixels = static_cast<std::uint32_t>(*cellWidthPixels),
                .cellHeightPixels = static_cast<std::uint32_t>(*cellHeightPixels),
                .reason = rejected ? reason.toString() : QString {},
        };
        for (const auto& entry : entries) {
            if (entry->identity.surfaceGeneration != 0
                && entry->identity.surfaceGeneration != *surfaceGeneration) {
                continue;
            }
            publishValue(
                entry,
                &TerminalSessionRegistry::Listener::resizeCompleted,
                outcome);
        }
    } else if (type == QStringLiteral("term.bell") || type == QStringLiteral("term.title")) {
        const auto sessionId = object.value(QStringLiteral("sessionId"));
        const auto title = object.value(QStringLiteral("title"));
        if (!sessionId.isString() || sessionId.toString() != control.subscription.sessionId
            || (type == QStringLiteral("term.title") && !title.isNull() && !title.isString())) {
            publishFailureToAll({GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal effect fields are invalid.")});
            return;
        }
        for (const auto& entry : entries) {
            if (type == QStringLiteral("term.title")) {
                auto text = title.toString().left(512);
                text.removeIf([](QChar character) { return character.category() == QChar::Other_Control
                    || character.category() == QChar::Other_Format; });
                publishValue(entry, &TerminalSessionRegistry::Listener::titleChanged, text);
            } else {
                std::scoped_lock lock(entry->listenerMutex);
                if (entry->active && entry->listener.bell) {
                    entry->listener.bell();
                }
            }
        }
    } else if (type == QStringLiteral("term.notification")) {
        const auto sessionId = object.value(QStringLiteral("sessionId"));
        const auto title = object.value(QStringLiteral("title"));
        const auto body = object.value(QStringLiteral("body"));
        const auto optionalString = [](const QJsonValue& value) {
            return value.isUndefined() || value.isNull() || value.isString();
        };
        if (!sessionId.isString()
            || sessionId.toString() != control.subscription.sessionId
            || !optionalString(title) || !optionalString(body)) {
            publishFailureToAll({
                GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal notification fields are invalid."),
            });
            return;
        }
        if (const auto entry = m_impl->primary(control.subscription)) {
            publishValue(
                entry,
                &TerminalSessionRegistry::Listener::notificationRequested,
                TerminalNotification {
                    .title = title.isString() ? title.toString() : QString {},
                    .body = body.isString() ? body.toString() : QString {},
                });
        }
    } else {
        publishFailureToAll({
            GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
            QStringLiteral("Terminal control type is unsupported."),
        });
    }
}

void TerminalSessionRegistry::receiveConnectResult(TerminalConnectResult result) noexcept
{
    const auto entries = m_impl->exact(result.subscription);
    for (const auto& entry : entries) {
        std::scoped_lock lock(entry->listenerMutex);
        if (entry->active && entry->listener.connectionCompleted) {
            entry->listener.connectionCompleted(result.result);
        }
    }
}

bool TerminalSessionRegistry::installSemanticCheckpoint(
    TerminalSemanticCheckpoint checkpoint) noexcept
{
    ScopedPerformanceSpan span(
        PerformanceCategory::Terminal,
        QStringLiteral("frame.checkpoint"),
        16);
    auto targets = m_impl->checkpointTargets(checkpoint.subscription);
    if (!targets) {
        span.setOutcome(QStringLiteral("stale"));
        return false;
    }
    if (targets->entries.empty()) {
        return targets->seedOnly;
    }
    bool allAccepted = true;
    bool anyCurrent = false;
    const auto install =
        [&](const std::vector<std::shared_ptr<Entry>>& entries) {
            for (const auto& entry : entries) {
                auto result = entry->kernel.installCheckpoint(checkpoint);
                const auto accepted = result.has_value();
                if (accepted) {
                    entry->needsCheckpoint.store(
                        false,
                        std::memory_order_release);
                }
                const auto current =
                    publishFrame(entry, std::move(result));
                if (current) {
                    anyCurrent = true;
                    allAccepted = allAccepted && accepted;
                }
            }
        };
    install(targets->entries);
    while (allAccepted) {
        auto additional = m_impl->pendingCheckpointTargets(
            checkpoint.subscription,
            targets->seedOnly);
        if (!additional) {
            allAccepted = false;
            break;
        }
        if (additional->empty()) {
            break;
        }
        install(*additional);
    }
    if (!allAccepted) {
        span.setOutcome(QStringLiteral("rejected"));
    } else if (!anyCurrent) {
        span.setOutcome(QStringLiteral("inactive"));
    }
    return allAccepted && anyCurrent;
}

} // namespace kodosi
