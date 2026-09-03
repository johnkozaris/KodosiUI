#include "terminal/TerminalSessionRegistry.hpp"
#include "logging/ApplicationLogStore.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <charconv>
#include <limits>
#include <mutex>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kodosi {
namespace {

struct Entry {
    Entry(
        TerminalSubscription value,
        TerminalSessionRegistry::Listener callbacks,
        TerminalKernelSettings settings)
        : subscription(std::move(value))
        , listener(std::move(callbacks))
        , kernel(settings)
    {
    }

    TerminalSubscription subscription;
    TerminalSessionRegistry::Listener listener;
    GhosttyTerminalKernel kernel;
    std::mutex listenerMutex;
    std::uint64_t lastPublishedDisplayRevision = 0;
    bool active = true;
};

bool sameSubscription(
    const TerminalSubscription& left,
    const TerminalSubscription& right) noexcept
{
    return left.sessionId == right.sessionId
        && left.subscriptionId == right.subscriptionId
        && left.generation == right.generation;
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
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<Entry>> sessions;

    std::shared_ptr<Entry> exact(const TerminalSubscription& subscription)
    {
        std::scoped_lock lock(mutex);
        const auto found = sessions.find(sessionKey(subscription.sessionId));
        if (found == sessions.end()
            || !sameSubscription(found->second->subscription, subscription)) {
            return {};
        }
        return found->second;
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
        entries.reserve(m_impl->sessions.size());
        for (const auto& [_, entry] : m_impl->sessions) {
            entries.push_back(entry);
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
    if (subscription.sessionId.isEmpty() || subscription.subscriptionId.isEmpty()) {
        return false;
    }

    auto entry = std::make_shared<Entry>(
        std::move(subscription),
        std::move(listener),
        settings);
    const auto key = sessionKey(entry->subscription.sessionId);
    std::scoped_lock lock(m_impl->mutex);
    const auto found = m_impl->sessions.find(key);
    if (found != m_impl->sessions.end()
        && found->second->subscription.generation >= entry->subscription.generation) {
        return false;
    }
    if (found != m_impl->sessions.end()) {
        deactivate(found->second);
    }
    m_impl->sessions.insert_or_assign(key, std::move(entry));
    return true;
}

void TerminalSessionRegistry::unregisterSession(const TerminalSubscription& subscription)
{
    std::scoped_lock lock(m_impl->mutex);
    const auto found = m_impl->sessions.find(sessionKey(subscription.sessionId));
    if (found != m_impl->sessions.end()
        && sameSubscription(found->second->subscription, subscription)) {
        deactivate(found->second);
        m_impl->sessions.erase(found);
    }
}

std::expected<QByteArray, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::encodeKey(
    const TerminalSubscription& subscription,
    TerminalKeyEvent keyEvent)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal key input belongs to an unregistered subscription."),
        });
    }
    return entry->kernel.encodeKey(
        subscription,
        std::move(keyEvent));
}

std::expected<QByteArray, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::encodePaste(
    const TerminalSubscription& subscription,
    QByteArray text)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal paste belongs to an unregistered subscription."),
        });
    }
    return entry->kernel.encodePaste(subscription, std::move(text));
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::scrollViewport(
    const TerminalSubscription& subscription,
    const int rows)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal scrolling belongs to an unregistered subscription."),
        });
    }
    auto result = entry->kernel.scrollViewport(subscription, rows);
    publishFrame(entry, result);
    return result;
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::scrollViewportToBottom(
    const TerminalSubscription& subscription)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal scrolling belongs to an unregistered subscription."),
        });
    }
    const auto prior = entry->kernel.frame();
    auto result = entry->kernel.scrollViewportToBottom(subscription);
    if (!result || *result != prior) {
        publishFrame(entry, result);
    }
    return result;
}

std::expected<QString, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::linkAt(
    const TerminalSubscription& subscription,
    const std::uint64_t viewportRevision,
    const std::uint16_t column,
    const std::uint16_t row)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal link lookup belongs to an unregistered subscription."),
        });
    }
    return entry->kernel.linkAt(
        subscription,
        viewportRevision,
        column,
        row);
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::beginSelection(
    const TerminalSubscription& subscription,
    const std::uint64_t viewportRevision,
    const std::uint16_t column,
    const std::uint16_t row,
    const bool rectangular)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal selection belongs to an unregistered subscription."),
        });
    }
    auto result = entry->kernel.beginSelection(
        subscription,
        viewportRevision,
        column,
        row,
        rectangular);
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
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal selection belongs to an unregistered subscription."),
        });
    }
    auto result = entry->kernel.updateSelection(
        subscription,
        viewportRevision,
        column,
        row);
    if (result || !result.error().isRecoverableHitTestRace()) {
        publishFrame(entry, result);
    }
    return result;
}

GhosttyTerminalKernel::Result TerminalSessionRegistry::clearSelection(
    const TerminalSubscription& subscription)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal selection belongs to an unregistered subscription."),
        });
    }
    auto result = entry->kernel.clearSelection(subscription);
    publishFrame(entry, result);
    return result;
}

std::expected<QString, GhosttyTerminalKernel::Failure>
TerminalSessionRegistry::selectedText(const TerminalSubscription& subscription)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal selection belongs to an unregistered subscription."),
        });
    }
    return entry->kernel.selectedText(subscription);
}

GhosttyTerminalKernel::ConfigureResult TerminalSessionRegistry::configure(
    const TerminalSubscription& subscription,
    TerminalKernelSettings settings)
{
    auto entry = m_impl->exact(subscription);
    if (!entry) {
        return std::unexpected(GhosttyTerminalKernel::Failure {
            GhosttyTerminalKernel::Failure::Code::StaleSubscription,
            QStringLiteral("Terminal settings belong to an unregistered subscription."),
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

void TerminalSessionRegistry::receiveData(TerminalData data) noexcept
{
    ScopedPerformanceSpan span(
        PerformanceCategory::Terminal,
        QStringLiteral("frame.apply"),
        8);
    if (auto entry = m_impl->exact(data.subscription)) {
        if (!publishFrame(entry, entry->kernel.applyData(data))) {
            span.setOutcome(QStringLiteral("inactive"));
        }
    } else {
        span.setOutcome(QStringLiteral("stale"));
    }
}

void TerminalSessionRegistry::receiveControl(TerminalControl control) noexcept
{
    ScopedPerformanceSpan span(
        PerformanceCategory::Terminal,
        QStringLiteral("frame.control"),
        8);
    auto entry = m_impl->exact(control.subscription);
    if (!entry) {
        span.setOutcome(QStringLiteral("stale"));
        return;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(control.json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        span.setOutcome(QStringLiteral("invalid"));
        publishFailure(entry, {
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
            publishFailure(entry, {
                GhosttyTerminalKernel::Failure::Code::ResourceLimit,
                QStringLiteral("Terminal resize control fields are invalid."),
            });
            return;
        }
        publishFrame(
            entry,
            entry->kernel.applyResize(
                control.subscription,
                *atSequence,
                static_cast<std::uint16_t>(*rows),
                static_cast<std::uint16_t>(*columns)));
    } else if (type == QStringLiteral("Closed")) {
        const auto finalSequence =
            unsignedJsonField(control.json, QByteArrayLiteral("finalSequence"));
        if (!finalSequence) {
            publishFailure(entry, {
                GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal close control fields are invalid."),
            });
            return;
        }
        std::scoped_lock lock(entry->listenerMutex);
        if (entry->active && entry->listener.closed) {
            entry->listener.closed();
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
            publishFailure(entry, {
                GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal focus result fields are invalid."),
            });
            return;
        }
        publishValue(
            entry,
            &TerminalSessionRegistry::Listener::focusCompleted,
            TerminalFocusOutcome {
                .applied = !rejected,
                .requestId = requestId.toString(),
                .runtimeIncarnationId = runtimeIncarnationId.toString(),
                .reason = rejected ? reason.toString() : QString {},
            });
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
            publishFailure(entry, {
                GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal resize result fields are invalid."),
            });
            return;
        }
        publishValue(
            entry,
            &TerminalSessionRegistry::Listener::resizeCompleted,
            TerminalResizeOutcome {
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
            });
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
            publishFailure(entry, {
                GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
                QStringLiteral("Terminal notification fields are invalid."),
            });
            return;
        }
        publishValue(
            entry,
            &TerminalSessionRegistry::Listener::notificationRequested,
            TerminalNotification {
                .title = title.isString() ? title.toString() : QString {},
                .body = body.isString() ? body.toString() : QString {},
            });
    } else {
        publishFailure(entry, {
            GhosttyTerminalKernel::Failure::Code::GhosttyRejected,
            QStringLiteral("Terminal control type is unsupported."),
        });
    }
}

void TerminalSessionRegistry::receiveConnectResult(TerminalConnectResult result) noexcept
{
    if (auto entry = m_impl->exact(result.subscription);
        entry) {
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
    auto entry = m_impl->exact(checkpoint.subscription);
    if (!entry) {
        span.setOutcome(QStringLiteral("stale"));
        return false;
    }
    auto result = entry->kernel.installCheckpoint(checkpoint);
    const auto accepted = result.has_value();
    const auto current = publishFrame(entry, std::move(result));
    if (!accepted) {
        span.setOutcome(QStringLiteral("rejected"));
    } else if (!current) {
        span.setOutcome(QStringLiteral("inactive"));
    }
    return accepted && current;
}

} // namespace kodosi
