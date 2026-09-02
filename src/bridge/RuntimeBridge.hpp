#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <expected>

namespace kodosi {

enum class EventLane {
    Auth,
    Sessions,
    System,
    Friends,
    Devices,
    AgentIntel,
    AgentGlobal,
    Trust,
    Rooms,
};

enum class CommandLane {
    Terminal,
    System,
    Auth,
    Friends,
    Devices,
    Trust,
    Rooms,
    Sessions,
    AgentIntel,
};

struct RuntimeFailure {
    enum class Code {
        ContractMismatch,
        StartRejected,
        RuntimeStopped,
        InvalidArgument,
        FfiRejected,
    };

    Code code;
    std::int32_t ffiResult;
    QString message;
};

class CommandDispatcher {
public:
    using Result = std::expected<void, RuntimeFailure>;

    virtual ~CommandDispatcher() = default;

    [[nodiscard]] virtual Result send(CommandLane lane, QByteArrayView json) = 0;
};

struct TerminalSubscription {
    QString sessionId;
    QString subscriptionId;
    std::uint64_t generation;
};

struct TerminalData {
    TerminalSubscription subscription;
    std::uint64_t sequence;
    QByteArray bytes;
};

struct TerminalControl {
    TerminalSubscription subscription;
    QByteArray json;
};

struct TerminalConnectResult {
    TerminalSubscription subscription;
    std::int32_t result;
};

struct TerminalSemanticCheckpoint {
    TerminalSubscription subscription;
    std::uint64_t nextSequence;
    std::uint16_t rows;
    std::uint16_t columns;
    QByteArray semanticJson;
};

class TerminalCommandDispatcher : public CommandDispatcher {
public:
    ~TerminalCommandDispatcher() override = default;

    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
    [[nodiscard]] virtual Result connectTerminal(
        const TerminalSubscription& subscription) = 0;
    [[nodiscard]] virtual Result disconnectTerminal(
        const TerminalSubscription& subscription) = 0;
    [[nodiscard]] virtual Result sendTerminalInput(
        const TerminalSubscription& subscription,
        const QString& expectedRuntimeIncarnationId,
        QByteArrayView bytes) = 0;
};

class TerminalEventSink {
public:
    virtual ~TerminalEventSink() = default;

    virtual void receiveData(TerminalData data) noexcept = 0;
    virtual void receiveControl(TerminalControl control) noexcept = 0;
    virtual void receiveConnectResult(TerminalConnectResult result) noexcept = 0;

    // This runs synchronously on the Rust callback thread. The implementation
    // must install the checkpoint before returning and must not enter QML.
    [[nodiscard]] virtual bool installSemanticCheckpoint(
        TerminalSemanticCheckpoint checkpoint) noexcept = 0;
};

class RuntimeBridge : public QObject, public TerminalCommandDispatcher {
    Q_OBJECT

public:
    using Result = CommandDispatcher::Result;

    explicit RuntimeBridge(QObject* parent = nullptr);
    RuntimeBridge(TerminalEventSink& terminalSink, QObject* parent = nullptr);
    ~RuntimeBridge() override;

    RuntimeBridge(const RuntimeBridge&) = delete;
    RuntimeBridge& operator=(const RuntimeBridge&) = delete;
    RuntimeBridge(RuntimeBridge&&) = delete;
    RuntimeBridge& operator=(RuntimeBridge&&) = delete;

    [[nodiscard]] bool isRunning() const noexcept override;
    [[nodiscard]] Result start();
    void stop() noexcept;

    [[nodiscard]] Result send(CommandLane lane, QByteArrayView json) override;
    [[nodiscard]] Result connectTerminal(
        const TerminalSubscription& subscription) override;
    [[nodiscard]] Result refreshTerminal(const TerminalSubscription& subscription);
    [[nodiscard]] Result disconnectTerminal(
        const TerminalSubscription& subscription) override;
    [[nodiscard]] Result sendTerminalInput(
        const TerminalSubscription& subscription,
        const QString& expectedRuntimeIncarnationId,
        QByteArrayView bytes) override;

signals:
    void eventReceived(kodosi::EventLane lane, QByteArray json);
    void runningChanged(bool running);

private:
    void* m_handle = nullptr;
    TerminalEventSink* m_terminalSink = nullptr;
    std::atomic<std::uint64_t> m_activeGeneration {0};
    std::uint64_t m_nextGeneration = 0;

    [[nodiscard]] Result requireRunning() const;
    [[nodiscard]] Result terminalOperationResult(std::int32_t result, QString operation) const;
    void queueEvent(EventLane lane, const std::uint8_t* json, std::uintptr_t length);

    static RuntimeBridge* resolve(void* userdata) noexcept;
    static void authEvent(const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void sessionEvent(
        const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void systemEvent(
        const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void friendsEvent(
        const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void devicesEvent(
        const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void agentIntelEvent(
        const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void agentGlobalEvent(
        const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void trustEvent(
        const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void roomEvent(
        const std::uint8_t* json, std::uintptr_t length, void* userdata) noexcept;
    static void terminalData(
        const char* sessionId,
        const char* subscriptionId,
        std::uint64_t subscriptionGeneration,
        std::uint64_t sequence,
        const std::uint8_t* bytes,
        std::uintptr_t length,
        void* userdata) noexcept;
    static void terminalControl(
        const char* sessionId,
        const char* subscriptionId,
        std::uint64_t subscriptionGeneration,
        const std::uint8_t* json,
        std::uintptr_t length,
        void* userdata) noexcept;
    static void terminalConnectResult(
        const char* sessionId,
        const char* subscriptionId,
        std::uint64_t subscriptionGeneration,
        std::int32_t result,
        void* userdata) noexcept;
    static std::int32_t terminalSemanticCheckpoint(
        const char* sessionId,
        const char* subscriptionId,
        std::uint64_t subscriptionGeneration,
        std::uint64_t nextSequence,
        std::uint16_t rows,
        std::uint16_t columns,
        const std::uint8_t* semanticJson,
        std::uintptr_t semanticJsonLength,
        void* userdata) noexcept;
};

} // namespace kodosi

Q_DECLARE_METATYPE(kodosi::EventLane)
