#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "terminal/TerminalRasterizer.hpp"
#include "terminal/TerminalSessionRegistry.hpp"

#include <QFont>
#include <QPoint>
#include <QQuickItem>
#include <QTimer>
#include <QVariant>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace kodosi {

class TerminalView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(int fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontPixelSizeChanged)
    Q_PROPERTY(double lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged)
    Q_PROPERTY(int cursorStyle READ cursorStyle WRITE setCursorStyle NOTIFY cursorStyleChanged)
    Q_PROPERTY(bool cursorBlink READ cursorBlink WRITE setCursorBlink NOTIFY cursorBlinkChanged)
    Q_PROPERTY(int scrollbackLines READ scrollbackLines WRITE setScrollbackLines NOTIFY scrollbackLinesChanged)
    Q_PROPERTY(QColor selectionBackground READ selectionBackground WRITE setSelectionBackground NOTIFY selectionBackgroundChanged)
    Q_PROPERTY(QColor selectionForeground READ selectionForeground WRITE setSelectionForeground NOTIFY selectionForegroundChanged)
    Q_PROPERTY(QColor preeditBackground READ preeditBackground WRITE setPreeditBackground NOTIFY preeditBackgroundChanged)
    Q_PROPERTY(QColor preeditForeground READ preeditForeground WRITE setPreeditForeground NOTIFY preeditForegroundChanged)
    Q_PROPERTY(bool terminalReady READ terminalReady NOTIFY terminalReadyChanged)
    Q_PROPERTY(bool canSendInput READ canSendInput NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canRetainFocus READ canRetainFocus NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canSendFocus READ canSendFocus NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canResize READ canResize NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY capabilitiesChanged)
    Q_PROPERTY(
        bool focusedSizeAuthority
        READ focusedSizeAuthority
        WRITE setFocusedSizeAuthority
        NOTIFY focusedSizeAuthorityChanged)

public:
    explicit TerminalView(QQuickItem* parent = nullptr);
    ~TerminalView() override;

    [[nodiscard]] QString fontFamily() const;
    [[nodiscard]] int fontPixelSize() const noexcept;
    [[nodiscard]] double lineHeight() const noexcept;
    [[nodiscard]] int cursorStyle() const noexcept;
    [[nodiscard]] bool cursorBlink() const noexcept;
    [[nodiscard]] int scrollbackLines() const noexcept;
    [[nodiscard]] QColor selectionBackground() const;
    [[nodiscard]] QColor selectionForeground() const;
    [[nodiscard]] QColor preeditBackground() const;
    [[nodiscard]] QColor preeditForeground() const;
    [[nodiscard]] bool terminalReady() const noexcept;
    [[nodiscard]] bool canSendInput() const noexcept;
    [[nodiscard]] bool canRetainFocus() const noexcept;
    [[nodiscard]] bool canSendFocus() const noexcept;
    [[nodiscard]] bool canResize() const noexcept;
    [[nodiscard]] bool readOnly() const noexcept;
    [[nodiscard]] bool focusedSizeAuthority() const noexcept;
    [[nodiscard]] QString accessibleText() const;
    [[nodiscard]] int accessibleCursorPosition() const;
    [[nodiscard]] QPair<int, int> accessibleSelection() const;
    [[nodiscard]] QRect accessibleCharacterRect(int offset) const;
    [[nodiscard]] int accessibleOffsetAt(const QPoint& globalPoint) const;
    void setFontFamily(const QString& family);
    void setFontPixelSize(int size);
    void setLineHeight(double lineHeight);
    void setCursorStyle(int style);
    void setCursorBlink(bool enabled);
    void setScrollbackLines(int lines);
    void setSelectionBackground(const QColor& color);
    void setSelectionForeground(const QColor& color);
    void setPreeditBackground(const QColor& color);
    void setPreeditForeground(const QColor& color);
    void setTerminalCapabilities(
        bool canSendInput,
        bool canRetainFocus,
        bool canSendFocus,
        bool canResize);
    void setFocusedSizeAuthority(bool focused);

    [[nodiscard]] bool attach(
        TerminalSessionRegistry& registry,
        TerminalCommandDispatcher& runtime,
        TerminalSubscription subscription,
        QString expectedRuntimeIncarnationId);
    Q_INVOKABLE void detach();

signals:
    void fontFamilyChanged();
    void fontPixelSizeChanged();
    void lineHeightChanged();
    void cursorStyleChanged();
    void cursorBlinkChanged();
    void scrollbackLinesChanged();
    void selectionBackgroundChanged();
    void selectionForegroundChanged();
    void preeditBackgroundChanged();
    void preeditForegroundChanged();
    void terminalReadyChanged();
    void capabilitiesChanged();
    void focusedSizeAuthorityChanged();
    void frameChanged();
    void terminalError(QString message);
    void connectionCompleted(bool connected, std::int32_t result);
    void terminalNotificationRequested(QString title, QString body);
    void terminalClosed();

protected:
    QSGNode* updatePaintNode(
        QSGNode* oldNode,
        UpdatePaintNodeData* updateData) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class FocusOperation {
        None,
        Focus,
        Blur,
    };

    struct PendingResize {
        QByteArray key;
        QString requestId;
        std::uint16_t columns;
        std::uint16_t rows;
        std::uint32_t widthPixels;
        std::uint32_t heightPixels;
        std::uint32_t cellWidthPixels;
        std::uint32_t cellHeightPixels;
        bool claim = false;
    };

    QFont m_font;
    qreal m_lineHeight = 1.1;
    TerminalCursorStyle m_cursorStyle = TerminalCursorStyle::Block;
    bool m_cursorBlink = false;
    int m_scrollbackLines = 10'000;
    TerminalRasterizer::Palette m_palette;
    QTimer m_cursorBlinkTimer;
    QMetaObject::Connection m_windowVisibilityConnection;
    bool m_cursorPhaseVisible = true;
    GhosttyTerminalKernel::Frame m_frame;
    GhosttyTerminalKernel::Frame m_renderedFrame;
    TerminalSessionRegistry* m_registry = nullptr;
    TerminalCommandDispatcher* m_runtime = nullptr;
    TerminalSubscription m_subscription;
    QString m_expectedRuntimeIncarnationId;
    qreal m_renderedDevicePixelRatio = 0.0;
    int m_renderedFontPixelSize = 0;
    QString m_preedit;
    QString m_renderedPreedit;
    std::uint64_t m_surfaceGeneration = 0;
    std::uint64_t m_nextSurfaceGeneration = 0;
    bool m_resizeQueued = false;
    QByteArray m_lastResizeKey;
    std::optional<PendingResize> m_pendingResize;
    QString m_pendingFocusRequestId;
    bool m_focusClaimed = false;
    bool m_terminalReady = false;
    bool m_focusRetryQueued = false;
    FocusOperation m_focusRetryOperation = FocusOperation::None;
    bool m_resizeRetryQueued = false;
    bool m_desiredFocus = false;
    bool m_canSendInput = false;
    bool m_canRetainFocus = false;
    bool m_canSendFocus = false;
    bool m_canResize = false;
    bool m_focusedSizeAuthority = false;
    bool m_resizeClaimPending = false;
    std::deque<QByteArray> m_inputQueue;
    qsizetype m_queuedInputBytes = 0;
    bool m_inputRetryQueued = false;
    std::uint8_t m_inputBackoffStep = 0;
    std::atomic<std::uint64_t> m_attachmentEpoch {0};
    std::mutex m_mailboxMutex;
    GhosttyTerminalKernel::Frame m_pendingFrame;
    bool m_frameDrainQueued = false;
    QPoint m_selectionAnchor;
    bool m_selecting = false;
    bool m_copyShortcutActive = false;

    void enqueueFrame(std::uint64_t epoch, GhosttyTerminalKernel::Frame frame);
    void drainFrame(std::uint64_t epoch);
    void presentFrame(GhosttyTerminalKernel::Frame frame);
    void presentFailure(GhosttyTerminalKernel::Failure failure);
    void sendText(const QString& text);
    [[nodiscard]] bool sendKey(QKeyEvent* event, TerminalKeyAction action);
    void enqueueInput(QByteArray bytes);
    void drainInputQueue();
    void sendFocus(bool focused);
    void dispatchFocus(FocusOperation operation);
    void scheduleFocusRetry(FocusOperation operation);
    void scheduleResizeRetry();
    void handleFocusOutcome(TerminalFocusOutcome outcome);
    void handleResizeOutcome(TerminalResizeOutcome outcome);
    void scheduleResize();
    void dispatchResize();
    [[nodiscard]] std::optional<QPoint> terminalCellAt(const QPointF& position) const;
    void updateSelection(const QPoint& endpoint);
    [[nodiscard]] bool copySelection();
    [[nodiscard]] TerminalKernelSettings kernelSettings() const;
    void configureAttachedKernel();
    void updateBlinkTimer();
    [[nodiscard]] bool blinkEligible() const;
    void invalidateMetrics();
};

} // namespace kodosi
