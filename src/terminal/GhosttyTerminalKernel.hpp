#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "terminal/TerminalFrame.hpp"
#include "terminal/TerminalSettings.hpp"

#include <QString>

#include <expected>
#include <memory>

namespace kodosi {

enum class TerminalKey {
    Backquote,
    Backslash,
    BracketLeft,
    BracketRight,
    Comma,
    Digit,
    Equal,
    Minus,
    Period,
    Quote,
    Semicolon,
    Slash,
    Space,
    NumpadDigit,
    NumpadAdd,
    NumpadSubtract,
    NumpadMultiply,
    NumpadDivide,
    NumpadDecimal,
    NumpadEnter,
    NumpadEqual,
    Enter,
    Backspace,
    Tab,
    Escape,
    Home,
    End,
    Insert,
    Delete,
    PageUp,
    PageDown,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    Function,
    Character,
};

enum class TerminalKeyAction {
    Press,
    Repeat,
    Release,
};

struct TerminalModifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool superKey = false;
};

struct TerminalKeyEvent {
    TerminalKey key;
    TerminalKeyAction action;
    QString text;
    std::uint32_t unshiftedCodepoint = 0;
    TerminalModifiers modifiers;
};

class GhosttyTerminalKernel final {
public:
    struct Failure {
        enum class Code {
            MissingCheckpoint,
            StaleSubscription,
            SequenceMismatch,
            ResourceLimit,
            GhosttyRejected,
        };

        Code code;
        QString message;
    };

    using Frame = std::shared_ptr<const TerminalFrame>;
    using Result = std::expected<Frame, Failure>;
    using ConfigureResult = std::expected<void, Failure>;

    explicit GhosttyTerminalKernel(TerminalKernelSettings settings = {});
    ~GhosttyTerminalKernel();

    GhosttyTerminalKernel(const GhosttyTerminalKernel&) = delete;
    GhosttyTerminalKernel& operator=(const GhosttyTerminalKernel&) = delete;
    GhosttyTerminalKernel(GhosttyTerminalKernel&&) = delete;
    GhosttyTerminalKernel& operator=(GhosttyTerminalKernel&&) = delete;

    // Checkpoint installation is transactional. Failure preserves the prior
    // terminal and frame, so RuntimeBridge can reject admission safely.
    [[nodiscard]] Result installCheckpoint(const TerminalSemanticCheckpoint& checkpoint);
    [[nodiscard]] Result applyData(const TerminalData& data);
    [[nodiscard]] Result applyResize(
        const TerminalSubscription& subscription,
        std::uint64_t atSequence,
        std::uint16_t rows,
        std::uint16_t columns);
    [[nodiscard]] std::expected<QByteArray, Failure> encodeKey(
        const TerminalSubscription& subscription,
        TerminalKeyEvent event);
    [[nodiscard]] Result select(
        const TerminalSubscription& subscription,
        std::uint16_t startColumn,
        std::uint16_t startRow,
        std::uint16_t endColumn,
        std::uint16_t endRow,
        bool rectangular = false);
    [[nodiscard]] Result clearSelection(const TerminalSubscription& subscription);
    [[nodiscard]] std::expected<QString, Failure> selectedText(
        const TerminalSubscription& subscription);
    [[nodiscard]] ConfigureResult configure(TerminalKernelSettings settings);
    [[nodiscard]] std::expected<std::size_t, Failure> scrollbackLimitLines() const;
    [[nodiscard]] std::expected<std::size_t, Failure> scrollbackLimitBytes() const;
    [[nodiscard]] Frame frame() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace kodosi
