#include "bridge/RuntimeBridge.hpp"

#include <kodosi_runtime.h>

#include <QMetaObject>
#include <QThread>

#include <limits>
#include <utility>

static_assert(KODOSI_FFI_ABI_VERSION == 5, "KodosiQT requires FFI ABI 5");

namespace kodosi {
namespace {

constexpr std::uint32_t supportedProtocolVersion = 36;
constexpr std::uint32_t requiredTerminalCapability = 1;

QByteArray copyBytes(const std::uint8_t* bytes, const std::uintptr_t length)
{
    if (length == 0) {
        return {};
    }
    if (bytes == nullptr
        || length > static_cast<std::uintptr_t>(std::numeric_limits<qsizetype>::max())) {
        return {};
    }
    return QByteArray(
        reinterpret_cast<const char*>(bytes),
        static_cast<qsizetype>(length));
}

TerminalSubscription copySubscription(
    const char* sessionId,
    const char* subscriptionId,
    const std::uint64_t generation)
{
    return {
        sessionId == nullptr ? QString{} : QString::fromUtf8(sessionId),
        subscriptionId == nullptr ? QString{} : QString::fromUtf8(subscriptionId),
        generation,
    };
}

bool validIdentifier(const QByteArray& identifier)
{
    return !identifier.isEmpty() && !identifier.contains('\0');
}

RuntimeFailure failure(
    const RuntimeFailure::Code code,
    const std::int32_t ffiResult,
    QString message)
{
    return {code, ffiResult, std::move(message)};
}

} // namespace

RuntimeBridge::RuntimeBridge(QObject* parent)
    : QObject(parent)
{
}

RuntimeBridge::RuntimeBridge(TerminalEventSink& terminalSink, QObject* parent)
    : QObject(parent)
    , m_terminalSink(&terminalSink)
{
}

RuntimeBridge::~RuntimeBridge()
{
    stop();
}

bool RuntimeBridge::isRunning() const noexcept
{
    return m_handle != nullptr;
}

RuntimeBridge::Result RuntimeBridge::start()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (isRunning()) {
        return {};
    }

    const auto protocolVersion = kodosi_protocol_version();
    if (protocolVersion != supportedProtocolVersion
        || kodosi_terminal_semantic_checkpoint_v2_capability() != requiredTerminalCapability
        || kodosi_terminal_connect_result_v2_capability() != requiredTerminalCapability) {
        return std::unexpected(failure(
            RuntimeFailure::Code::ContractMismatch,
            KODOSI_FFI_DESER_FAILED,
            QStringLiteral("The linked Kodosi runtime does not satisfy ABI 5 / protocol 36.")));
    }

    const kodosi_callbacks_v2_t callbacks {
        .on_auth_event = &RuntimeBridge::authEvent,
        .on_session_event = &RuntimeBridge::sessionEvent,
        .on_system_event = &RuntimeBridge::systemEvent,
        .on_friends_event = &RuntimeBridge::friendsEvent,
        .on_devices_event = &RuntimeBridge::devicesEvent,
        .on_agent_intel_event = &RuntimeBridge::agentIntelEvent,
        .on_agent_global_event = &RuntimeBridge::agentGlobalEvent,
        .on_trust_event = &RuntimeBridge::trustEvent,
        .on_room_event = &RuntimeBridge::roomEvent,
        .on_terminal_data_v2 = &RuntimeBridge::terminalData,
        .on_terminal_control_v2 = &RuntimeBridge::terminalControl,
        .on_terminal_connect_result_v2 = &RuntimeBridge::terminalConnectResult,
        .on_terminal_semantic_checkpoint_v2 = &RuntimeBridge::terminalSemanticCheckpoint,
    };
    const auto generation = ++m_nextGeneration;
    m_activeGeneration.store(generation, std::memory_order_release);
    m_handle = kodosi_start_v2(&callbacks, sizeof(callbacks), this);
    if (m_handle == nullptr) {
        m_activeGeneration.store(0, std::memory_order_release);
        return std::unexpected(failure(
            RuntimeFailure::Code::StartRejected,
            KODOSI_FFI_RUNTIME_STOPPED,
            QStringLiteral("The Kodosi runtime rejected startup.")));
    }

    emit runningChanged(true);
    return {};
}

void RuntimeBridge::stop() noexcept
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!isRunning()) {
        return;
    }
    m_activeGeneration.store(0, std::memory_order_release);
    auto* handle = std::exchange(m_handle, nullptr);
    kodosi_stop(handle);
    emit runningChanged(false);
}

RuntimeBridge::Result RuntimeBridge::send(const CommandLane lane, const QByteArrayView json)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (auto running = requireRunning(); !running) {
        return running;
    }
    if (json.size() < 0 || json.size() > KODOSI_MAX_FRAME_BYTES) {
        return std::unexpected(failure(
            RuntimeFailure::Code::InvalidArgument,
            KODOSI_FFI_PAYLOAD_TOO_LARGE,
            QStringLiteral("The command payload exceeds the runtime frame limit.")));
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(json.data());
    const auto length = static_cast<std::uintptr_t>(json.size());
    std::int32_t result = KODOSI_FFI_DESER_FAILED;
    switch (lane) {
    case CommandLane::Terminal:
        result = kodosi_send_terminal(m_handle, bytes, length);
        break;
    case CommandLane::System:
        result = kodosi_send_system(m_handle, bytes, length);
        break;
    case CommandLane::Auth:
        result = kodosi_send_auth(m_handle, bytes, length);
        break;
    case CommandLane::Friends:
        result = kodosi_send_friends(m_handle, bytes, length);
        break;
    case CommandLane::Devices:
        result = kodosi_send_devices(m_handle, bytes, length);
        break;
    case CommandLane::Trust:
        result = kodosi_send_trust(m_handle, bytes, length);
        break;
    case CommandLane::Rooms:
        result = kodosi_send_room(m_handle, bytes, length);
        break;
    case CommandLane::Sessions:
        result = kodosi_send_sessions(m_handle, bytes, length);
        break;
    case CommandLane::AgentIntel:
        result = kodosi_send_agent_intel(m_handle, bytes, length);
        break;
    }
    return terminalOperationResult(result, QStringLiteral("command dispatch"));
}

RuntimeBridge::Result RuntimeBridge::connectTerminal(
    const TerminalSubscription& subscription)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (auto running = requireRunning(); !running) {
        return running;
    }
    const auto sessionId = subscription.sessionId.toUtf8();
    const auto subscriptionId = subscription.subscriptionId.toUtf8();
    if (!validIdentifier(sessionId) || !validIdentifier(subscriptionId)) {
        return std::unexpected(failure(
            RuntimeFailure::Code::InvalidArgument,
            KODOSI_FFI_DESER_FAILED,
            QStringLiteral("Terminal subscription identifiers must be non-empty UTF-8 strings.")));
    }
    const auto result = kodosi_terminal_connect_v2(
        m_handle,
        sessionId.constData(),
        subscriptionId.constData(),
        subscription.generation);
    return terminalOperationResult(result, QStringLiteral("terminal connect"));
}

RuntimeBridge::Result RuntimeBridge::refreshTerminal(
    const TerminalSubscription& subscription)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (auto running = requireRunning(); !running) {
        return running;
    }
    const auto sessionId = subscription.sessionId.toUtf8();
    const auto subscriptionId = subscription.subscriptionId.toUtf8();
    if (!validIdentifier(sessionId) || !validIdentifier(subscriptionId)) {
        return std::unexpected(failure(
            RuntimeFailure::Code::InvalidArgument,
            KODOSI_FFI_DESER_FAILED,
            QStringLiteral("Terminal subscription identifiers must be non-empty UTF-8 strings.")));
    }
    const auto result = kodosi_terminal_refresh_v2(
        m_handle,
        sessionId.constData(),
        subscriptionId.constData(),
        subscription.generation);
    return terminalOperationResult(result, QStringLiteral("terminal refresh"));
}

RuntimeBridge::Result RuntimeBridge::disconnectTerminal(
    const TerminalSubscription& subscription)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (auto running = requireRunning(); !running) {
        return running;
    }
    const auto sessionId = subscription.sessionId.toUtf8();
    const auto subscriptionId = subscription.subscriptionId.toUtf8();
    if (!validIdentifier(sessionId) || !validIdentifier(subscriptionId)) {
        return std::unexpected(failure(
            RuntimeFailure::Code::InvalidArgument,
            KODOSI_FFI_DESER_FAILED,
            QStringLiteral("Terminal subscription identifiers must be non-empty UTF-8 strings.")));
    }
    const auto result = kodosi_terminal_disconnect_v2(
        m_handle,
        sessionId.constData(),
        subscriptionId.constData(),
        subscription.generation);
    return terminalOperationResult(result, QStringLiteral("terminal disconnect"));
}

RuntimeBridge::Result RuntimeBridge::sendTerminalInput(
    const TerminalSubscription& subscription,
    const QString& expectedRuntimeIncarnationId,
    const QByteArrayView bytes)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (auto running = requireRunning(); !running) {
        return running;
    }
    const auto sessionId = subscription.sessionId.toUtf8();
    const auto subscriptionId = subscription.subscriptionId.toUtf8();
    const auto incarnationId = expectedRuntimeIncarnationId.toUtf8();
    if (!validIdentifier(sessionId) || !validIdentifier(subscriptionId)
        || !validIdentifier(incarnationId) || bytes.size() < 0) {
        return std::unexpected(failure(
            RuntimeFailure::Code::InvalidArgument,
            KODOSI_FFI_DESER_FAILED,
            QStringLiteral("Terminal input requires valid session and incarnation identifiers.")));
    }

    const auto result = kodosi_terminal_input(
        m_handle,
        sessionId.constData(),
        incarnationId.constData(),
        subscriptionId.constData(),
        subscription.generation,
        reinterpret_cast<const std::uint8_t*>(bytes.data()),
        static_cast<std::uintptr_t>(bytes.size()));
    return terminalOperationResult(result, QStringLiteral("terminal input"));
}

RuntimeBridge::Result RuntimeBridge::requireRunning() const
{
    if (isRunning()) {
        return {};
    }
    return std::unexpected(failure(
        RuntimeFailure::Code::RuntimeStopped,
        KODOSI_FFI_RUNTIME_STOPPED,
        QStringLiteral("The Kodosi runtime is not running.")));
}

RuntimeBridge::Result RuntimeBridge::terminalOperationResult(
    const std::int32_t result,
    QString operation) const
{
    if (result == KODOSI_FFI_OK) {
        return {};
    }
    return std::unexpected(failure(
        RuntimeFailure::Code::FfiRejected,
        result,
        QStringLiteral("%1 failed with runtime result %2.").arg(operation).arg(result)));
}

void RuntimeBridge::queueEvent(
    const EventLane lane,
    const std::uint8_t* json,
    const std::uintptr_t length)
{
    const auto generation = m_activeGeneration.load(std::memory_order_acquire);
    if (generation == 0) {
        return;
    }
    const auto payload = copyBytes(json, length);
    if (length != 0 && payload.isEmpty()) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, generation, lane, payload] {
            if (m_activeGeneration.load(std::memory_order_acquire) == generation) {
                emit eventReceived(lane, payload);
            }
        },
        Qt::QueuedConnection);
}

RuntimeBridge* RuntimeBridge::resolve(void* userdata) noexcept
{
    return static_cast<RuntimeBridge*>(userdata);
}

void RuntimeBridge::authEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::Auth, json, length);
    }
}

void RuntimeBridge::sessionEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::Sessions, json, length);
    }
}

void RuntimeBridge::systemEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::System, json, length);
    }
}

void RuntimeBridge::friendsEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::Friends, json, length);
    }
}

void RuntimeBridge::devicesEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::Devices, json, length);
    }
}

void RuntimeBridge::agentIntelEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::AgentIntel, json, length);
    }
}

void RuntimeBridge::agentGlobalEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::AgentGlobal, json, length);
    }
}

void RuntimeBridge::trustEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::Trust, json, length);
    }
}

void RuntimeBridge::roomEvent(
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    if (auto* bridge = resolve(userdata)) {
        bridge->queueEvent(EventLane::Rooms, json, length);
    }
}

void RuntimeBridge::terminalData(
    const char* sessionId,
    const char* subscriptionId,
    const std::uint64_t subscriptionGeneration,
    const std::uint64_t sequence,
    const std::uint8_t* bytes,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    auto* bridge = resolve(userdata);
    if (bridge == nullptr || bridge->m_terminalSink == nullptr
        || bridge->m_activeGeneration.load(std::memory_order_acquire) == 0) {
        return;
    }
    auto payload = copyBytes(bytes, length);
    if (length != 0 && payload.isEmpty()) {
        return;
    }
    bridge->m_terminalSink->receiveData({
        copySubscription(sessionId, subscriptionId, subscriptionGeneration),
        sequence,
        std::move(payload),
    });
}

void RuntimeBridge::terminalControl(
    const char* sessionId,
    const char* subscriptionId,
    const std::uint64_t subscriptionGeneration,
    const std::uint8_t* json,
    const std::uintptr_t length,
    void* userdata) noexcept
{
    auto* bridge = resolve(userdata);
    if (bridge == nullptr || bridge->m_terminalSink == nullptr
        || bridge->m_activeGeneration.load(std::memory_order_acquire) == 0) {
        return;
    }
    auto payload = copyBytes(json, length);
    if (length != 0 && payload.isEmpty()) {
        return;
    }
    bridge->m_terminalSink->receiveControl({
        copySubscription(sessionId, subscriptionId, subscriptionGeneration),
        std::move(payload),
    });
}

void RuntimeBridge::terminalConnectResult(
    const char* sessionId,
    const char* subscriptionId,
    const std::uint64_t subscriptionGeneration,
    const std::int32_t result,
    void* userdata) noexcept
{
    auto* bridge = resolve(userdata);
    if (bridge == nullptr || bridge->m_terminalSink == nullptr
        || bridge->m_activeGeneration.load(std::memory_order_acquire) == 0) {
        return;
    }
    bridge->m_terminalSink->receiveConnectResult({
        copySubscription(sessionId, subscriptionId, subscriptionGeneration),
        result,
    });
}

std::int32_t RuntimeBridge::terminalSemanticCheckpoint(
    const char* sessionId,
    const char* subscriptionId,
    const std::uint64_t subscriptionGeneration,
    const std::uint64_t nextSequence,
    const std::uint16_t rows,
    const std::uint16_t columns,
    const std::uint8_t* semanticJson,
    const std::uintptr_t semanticJsonLength,
    void* userdata) noexcept
{
    auto* bridge = resolve(userdata);
    if (bridge == nullptr || bridge->m_terminalSink == nullptr
        || bridge->m_activeGeneration.load(std::memory_order_acquire) == 0) {
        return KODOSI_FFI_TERMINAL_CHECKPOINT_REJECTED;
    }
    auto payload = copyBytes(semanticJson, semanticJsonLength);
    if (semanticJsonLength != 0 && payload.isEmpty()) {
        return KODOSI_FFI_TERMINAL_CHECKPOINT_REJECTED;
    }
    const auto accepted = bridge->m_terminalSink->installSemanticCheckpoint({
        copySubscription(sessionId, subscriptionId, subscriptionGeneration),
        nextSequence,
        rows,
        columns,
        std::move(payload),
    });
    return accepted ? KODOSI_FFI_OK : KODOSI_FFI_TERMINAL_CHECKPOINT_REJECTED;
}

} // namespace kodosi
