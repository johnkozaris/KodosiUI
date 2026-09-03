#include "terminal/TerminalView.hpp"
#include "logging/ApplicationLogStore.hpp"
#include "terminal/TerminalLink.hpp"

#include <kodosi_runtime.h>

#include <QAccessible>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QHoverEvent>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QWheelEvent>
#include <QtQuick/private/qquickaccessibleattached_p.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype maximumQueuedInputBytes = 1024 * 1024;
constexpr std::size_t maximumQueuedInputFrames = 1024;
constexpr int maximumWheelRowsPerEvent = 100;
constexpr int inputBackoffMilliseconds[] {4, 8, 16, 32, 64};

void publishAccessibleTextChange(
    QObject* object,
    const QString& previous,
    const QString& current)
{
    if (previous == current) {
        return;
    }
    qsizetype prefix = 0;
    const auto shared = std::min(previous.size(), current.size());
    while (prefix < shared && previous[prefix] == current[prefix]) {
        ++prefix;
    }
    if (prefix > 0 && prefix < previous.size()
        && previous[prefix - 1].isHighSurrogate()
        && previous[prefix].isLowSurrogate()) {
        --prefix;
    }

    qsizetype suffix = 0;
    while (suffix < previous.size() - prefix
        && suffix < current.size() - prefix
        && previous[previous.size() - suffix - 1]
            == current[current.size() - suffix - 1]) {
        ++suffix;
    }
    const auto previousEnd = previous.size() - suffix;
    const auto currentEnd = current.size() - suffix;
    if (suffix > 0
        && ((previousEnd > 0 && previousEnd < previous.size()
                && previous[previousEnd - 1].isHighSurrogate()
                && previous[previousEnd].isLowSurrogate())
            || (currentEnd > 0 && currentEnd < current.size()
                && current[currentEnd - 1].isHighSurrogate()
                && current[currentEnd].isLowSurrogate()))) {
        --suffix;
    }

    QAccessibleTextUpdateEvent event(
        object,
        static_cast<int>(prefix),
        previous.sliced(prefix, previous.size() - prefix - suffix),
        current.sliced(prefix, current.size() - prefix - suffix));
    QAccessible::updateAccessibility(&event);
}

QByteArray jsonString(const QString& value)
{
    const auto encoded = QJsonDocument(QJsonArray {value}).toJson(QJsonDocument::Compact);
    return encoded.mid(1, encoded.size() - 2);
}

std::uint32_t unshiftedCodepoint(const int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QLatin1Char('a').unicode() + key - Qt::Key_A;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QLatin1Char('0').unicode() + key - Qt::Key_0;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return static_cast<std::uint32_t>(key - Qt::Key_F1 + 1);
    }
    switch (key) {
    case Qt::Key_ParenRight:
        return '0';
    case Qt::Key_Exclam:
        return '1';
    case Qt::Key_At:
        return '2';
    case Qt::Key_NumberSign:
        return '3';
    case Qt::Key_Dollar:
        return '4';
    case Qt::Key_Percent:
        return '5';
    case Qt::Key_AsciiCircum:
        return '6';
    case Qt::Key_Ampersand:
        return '7';
    case Qt::Key_Asterisk:
        return '8';
    case Qt::Key_ParenLeft:
        return '9';
    case Qt::Key_QuoteLeft:
    case Qt::Key_AsciiTilde:
        return '`';
    case Qt::Key_Backslash:
    case Qt::Key_Bar:
        return '\\';
    case Qt::Key_BracketLeft:
    case Qt::Key_BraceLeft:
        return '[';
    case Qt::Key_BracketRight:
    case Qt::Key_BraceRight:
        return ']';
    case Qt::Key_Comma:
    case Qt::Key_Less:
        return ',';
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        return '=';
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        return '-';
    case Qt::Key_Period:
    case Qt::Key_Greater:
        return '.';
    case Qt::Key_Apostrophe:
    case Qt::Key_QuoteDbl:
        return '\'';
    case Qt::Key_Semicolon:
    case Qt::Key_Colon:
        return ';';
    case Qt::Key_Slash:
    case Qt::Key_Question:
        return '/';
    case Qt::Key_Space:
        return ' ';
    default:
        return 0;
    }
}

TerminalModifiers modifiers(const Qt::KeyboardModifiers value)
{
    return {
        .shift = value.testFlag(Qt::ShiftModifier),
        .control = value.testFlag(Qt::ControlModifier),
        .alt = value.testFlag(Qt::AltModifier),
        .superKey = value.testFlag(Qt::MetaModifier),
    };
}

std::optional<TerminalKey> terminalKey(const QKeyEvent* event)
{
    const auto key = event->key();
    if (event->modifiers().testFlag(Qt::KeypadModifier)) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9) {
            return TerminalKey::NumpadDigit;
        }
        switch (key) {
        case Qt::Key_Plus:
            return TerminalKey::NumpadAdd;
        case Qt::Key_Minus:
            return TerminalKey::NumpadSubtract;
        case Qt::Key_Asterisk:
            return TerminalKey::NumpadMultiply;
        case Qt::Key_Slash:
            return TerminalKey::NumpadDivide;
        case Qt::Key_Period:
        case Qt::Key_Comma:
            return TerminalKey::NumpadDecimal;
        case Qt::Key_Enter:
        case Qt::Key_Return:
            return TerminalKey::NumpadEnter;
        case Qt::Key_Equal:
            return TerminalKey::NumpadEqual;
        default:
            break;
        }
    }
    switch (key) {
    case Qt::Key_QuoteLeft:
    case Qt::Key_AsciiTilde:
        return TerminalKey::Backquote;
    case Qt::Key_Backslash:
    case Qt::Key_Bar:
        return TerminalKey::Backslash;
    case Qt::Key_BracketLeft:
    case Qt::Key_BraceLeft:
        return TerminalKey::BracketLeft;
    case Qt::Key_BracketRight:
    case Qt::Key_BraceRight:
        return TerminalKey::BracketRight;
    case Qt::Key_Comma:
    case Qt::Key_Less:
        return TerminalKey::Comma;
    case Qt::Key_0:
    case Qt::Key_ParenRight:
    case Qt::Key_1:
    case Qt::Key_Exclam:
    case Qt::Key_2:
    case Qt::Key_At:
    case Qt::Key_3:
    case Qt::Key_NumberSign:
    case Qt::Key_4:
    case Qt::Key_Dollar:
    case Qt::Key_5:
    case Qt::Key_Percent:
    case Qt::Key_6:
    case Qt::Key_AsciiCircum:
    case Qt::Key_7:
    case Qt::Key_Ampersand:
    case Qt::Key_8:
    case Qt::Key_Asterisk:
    case Qt::Key_9:
    case Qt::Key_ParenLeft:
        return TerminalKey::Digit;
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        return TerminalKey::Equal;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        return TerminalKey::Minus;
    case Qt::Key_Period:
    case Qt::Key_Greater:
        return TerminalKey::Period;
    case Qt::Key_Apostrophe:
    case Qt::Key_QuoteDbl:
        return TerminalKey::Quote;
    case Qt::Key_Semicolon:
    case Qt::Key_Colon:
        return TerminalKey::Semicolon;
    case Qt::Key_Slash:
    case Qt::Key_Question:
        return TerminalKey::Slash;
    case Qt::Key_Space:
        return TerminalKey::Space;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return TerminalKey::Enter;
    case Qt::Key_Backspace:
        return TerminalKey::Backspace;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return TerminalKey::Tab;
    case Qt::Key_Escape:
        return TerminalKey::Escape;
    case Qt::Key_Home:
        return TerminalKey::Home;
    case Qt::Key_End:
        return TerminalKey::End;
    case Qt::Key_Insert:
        return TerminalKey::Insert;
    case Qt::Key_Delete:
        return TerminalKey::Delete;
    case Qt::Key_PageUp:
        return TerminalKey::PageUp;
    case Qt::Key_PageDown:
        return TerminalKey::PageDown;
    case Qt::Key_Up:
        return TerminalKey::ArrowUp;
    case Qt::Key_Down:
        return TerminalKey::ArrowDown;
    case Qt::Key_Left:
        return TerminalKey::ArrowLeft;
    case Qt::Key_Right:
        return TerminalKey::ArrowRight;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
            return TerminalKey::Function;
        }
        return key >= Qt::Key_A && key <= Qt::Key_Z
            ? std::optional<TerminalKey> {TerminalKey::Character}
            : std::nullopt;
    }
}

bool isPasteShortcut(const QKeyEvent* event)
{
    const auto modifiers = event->modifiers();
    const auto terminalShortcut = event->key() == Qt::Key_V
        && modifiers == (Qt::ControlModifier | Qt::ShiftModifier);
    const auto platformShortcut = event->matches(QKeySequence::Paste)
        && !modifiers.testFlag(Qt::ControlModifier);
    return terminalShortcut || platformShortcut;
}

} // namespace

void detail::TerminalFrameMailbox::reset(const std::uint64_t incarnation)
{
    std::scoped_lock lock(m_mutex);
    m_pendingFrame.reset();
    m_incarnation = incarnation;
    m_highestDisplayRevision = 0;
    m_drainQueued = false;
}

detail::TerminalFrameMailbox::EnqueueResult detail::TerminalFrameMailbox::enqueue(
    const std::uint64_t incarnation,
    GhosttyTerminalKernel::Frame frame)
{
    std::scoped_lock lock(m_mutex);
    if (incarnation != m_incarnation || frame == nullptr
        || frame->displayRevision <= m_highestDisplayRevision) {
        return {};
    }
    m_highestDisplayRevision = frame->displayRevision;
    m_pendingFrame = std::move(frame);
    const auto queueDrain = !m_drainQueued;
    m_drainQueued = true;
    return {
        .accepted = true,
        .queueDrain = queueDrain,
    };
}

GhosttyTerminalKernel::Frame detail::TerminalFrameMailbox::take(
    const std::uint64_t incarnation)
{
    std::scoped_lock lock(m_mutex);
    if (incarnation != m_incarnation) {
        return {};
    }
    auto frame = std::exchange(m_pendingFrame, {});
    m_drainQueued = false;
    return frame;
}

TerminalView::TerminalView(QQuickItem* parent)
    : QQuickItem(parent)
    , m_font(QFontDatabase::systemFont(QFontDatabase::FixedFont))
    , m_cursorBlinkTimer(this)
{
    setFlag(ItemHasContents, true);
    setFlag(ItemAcceptsInputMethod, true);
    setActiveFocusOnTab(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    auto* accessible = qobject_cast<QQuickAccessibleAttached*>(
        qmlAttachedPropertiesObject<QQuickAccessibleAttached>(this, true));
    accessible->setRole(QAccessible::Terminal);
    accessible->setName(QStringLiteral("Terminal session"));
    accessible->set_focusable(true);
    accessible->set_editable(false);
    accessible->set_readOnly(true);
    accessible->set_multiLine(true);
    accessible->set_selectableText(true);
    m_font.setFamily(QStringLiteral("JetBrains Mono"));
    m_font.setFixedPitch(true);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setPixelSize(14);
    m_cursorBlinkTimer.setObjectName(QStringLiteral("terminal.cursorBlinkTimer"));
    m_cursorBlinkTimer.setInterval(500);
    connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this] {
        if (!blinkEligible()) {
            updateBlinkTimer();
            return;
        }
        m_cursorPhaseVisible = !m_cursorPhaseVisible;
        m_renderedFrame.reset();
        update();
    });
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        scheduleResize();
    });
}

TerminalView::~TerminalView()
{
    m_renderPerformanceGeneration.fetch_add(1, std::memory_order_acq_rel);
    detach();
}

QString TerminalView::fontFamily() const
{
    return m_font.family();
}

int TerminalView::fontPixelSize() const noexcept
{
    return m_font.pixelSize();
}

double TerminalView::lineHeight() const noexcept
{
    return m_lineHeight;
}

int TerminalView::cursorStyle() const noexcept
{
    switch (m_cursorStyle) {
    case TerminalCursorStyle::Block:
        return 0;
    case TerminalCursorStyle::Bar:
        return 1;
    case TerminalCursorStyle::Underline:
        return 2;
    case TerminalCursorStyle::HollowBlock:
        return 0;
    }
    return 0;
}

bool TerminalView::cursorBlink() const noexcept
{
    return m_cursorBlink;
}

int TerminalView::scrollbackLines() const noexcept
{
    return m_scrollbackLines;
}

QColor TerminalView::selectionBackground() const
{
    return m_palette.selectionBackground;
}

QColor TerminalView::selectionForeground() const
{
    return m_palette.selectionForeground;
}

QColor TerminalView::preeditBackground() const
{
    return m_palette.preeditBackground;
}

QColor TerminalView::preeditForeground() const
{
    return m_palette.preeditForeground;
}

bool TerminalView::terminalReady() const noexcept
{
    return m_terminalReady;
}

bool TerminalView::canSendInput() const noexcept
{
    return m_canSendInput;
}

bool TerminalView::canRetainFocus() const noexcept
{
    return m_canRetainFocus;
}

bool TerminalView::canSendFocus() const noexcept
{
    return m_canSendFocus;
}

bool TerminalView::canResize() const noexcept
{
    return m_canResize;
}

bool TerminalView::readOnly() const noexcept
{
    return !m_canSendInput;
}

bool TerminalView::focusedSizeAuthority() const noexcept
{
    return m_focusedSizeAuthority;
}

QString TerminalView::accessibleText() const
{
    if (!m_frame) {
        return {};
    }
    QString text;
    text.reserve(static_cast<qsizetype>(m_frame->rows) * (m_frame->columns + 1));
    for (std::uint16_t row = 0; row < m_frame->rows; ++row) {
        for (std::uint16_t column = 0; column < m_frame->columns; ++column) {
            const auto* cell = m_frame->cell(column, row);
            if (cell != nullptr && cell->width != 0) {
                text += cell->grapheme.isEmpty() ? QStringLiteral(" ") : cell->grapheme;
            }
        }
        if (row + 1 < m_frame->rows) {
            text += QLatin1Char('\n');
        }
    }
    return text;
}

int TerminalView::accessibleCursorPosition() const
{
    if (!m_frame) {
        return 0;
    }
    int offset = 0;
    for (std::uint16_t row = 0; row < m_frame->rows; ++row) {
        for (std::uint16_t column = 0; column < m_frame->columns; ++column) {
            if (row == m_frame->cursor.row && column == m_frame->cursor.column) {
                return offset;
            }
            const auto* cell = m_frame->cell(column, row);
            if (cell != nullptr && cell->width != 0) {
                offset += std::max(1, static_cast<int>(cell->grapheme.size()));
            }
        }
        if (row + 1 < m_frame->rows) {
            ++offset;
        }
    }
    return offset;
}

QPair<int, int> TerminalView::accessibleSelection() const
{
    if (!m_frame) {
        return {-1, -1};
    }
    int offset = 0;
    int start = -1;
    int end = -1;
    for (std::uint16_t row = 0; row < m_frame->rows; ++row) {
        for (std::uint16_t column = 0; column < m_frame->columns; ++column) {
            const auto* cell = m_frame->cell(column, row);
            if (cell == nullptr || cell->width == 0) {
                continue;
            }
            const auto length = std::max(1, static_cast<int>(cell->grapheme.size()));
            if (cell->selected) {
                start = start < 0 ? offset : start;
                end = offset + length;
            }
            offset += length;
        }
        if (row + 1 < m_frame->rows) {
            ++offset;
        }
    }
    return {start, end};
}

QRect TerminalView::accessibleCharacterRect(const int offset) const
{
    if (!m_frame || window() == nullptr || offset < 0) {
        return {};
    }
    int current = 0;
    std::uint16_t targetColumn = 0;
    std::uint16_t targetRow = 0;
    std::uint8_t targetWidth = 1;
    bool found = false;
    for (std::uint16_t row = 0; row < m_frame->rows && !found; ++row) {
        for (std::uint16_t column = 0; column < m_frame->columns; ++column) {
            const auto* cell = m_frame->cell(column, row);
            if (cell == nullptr || cell->width == 0) {
                continue;
            }
            const auto length = std::max(1, static_cast<int>(cell->grapheme.size()));
            if (offset >= current && offset < current + length) {
                targetColumn = column;
                targetRow = row;
                targetWidth = std::max<std::uint8_t>(cell->width, 1);
                found = true;
                break;
            }
            current += length;
        }
        if (row + 1 < m_frame->rows) {
            ++current;
        }
    }
    if (!found) {
        return {};
    }
    const auto cellSize = TerminalRasterizer::cellSize(m_font, m_lineHeight);
    const auto scenePoint = mapToScene(
        QPointF(cellSize.width() * targetColumn, cellSize.height() * targetRow));
    return {
        window()->mapToGlobal(scenePoint.toPoint()),
        QSize(
            static_cast<int>(cellSize.width() * targetWidth),
            static_cast<int>(cellSize.height())),
    };
}

int TerminalView::accessibleOffsetAt(const QPoint& globalPoint) const
{
    if (!m_frame || window() == nullptr) {
        return -1;
    }
    const auto scenePoint = window()->mapFromGlobal(globalPoint);
    const auto localPoint = mapFromScene(scenePoint);
    auto cell = terminalCellAt(localPoint);
    if (!cell) {
        return -1;
    }
    if (const auto* value = m_frame->cell(
            static_cast<std::uint16_t>(cell->x()),
            static_cast<std::uint16_t>(cell->y()));
        value != nullptr && value->width == 0 && cell->x() > 0) {
        cell->rx() -= 1;
    }
    int offset = 0;
    for (int row = 0; row <= cell->y(); ++row) {
        const auto endColumn = row == cell->y() ? cell->x() : m_frame->columns;
        for (int column = 0; column < endColumn; ++column) {
            const auto* value = m_frame->cell(
                static_cast<std::uint16_t>(column),
                static_cast<std::uint16_t>(row));
            if (value != nullptr && value->width != 0) {
                offset += std::max(1, static_cast<int>(value->grapheme.size()));
            }
        }
        if (row < cell->y()) {
            ++offset;
        }
    }
    return offset;
}

void TerminalView::setFontFamily(const QString& family)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto trimmed = family.trimmed();
    if (trimmed.isEmpty() || trimmed == m_font.family()) {
        return;
    }
    m_font.setFamily(trimmed);
    m_font.setFixedPitch(true);
    m_font.setStyleHint(QFont::Monospace);
    invalidateMetrics();
    emit fontFamilyChanged();
}

void TerminalView::setFontPixelSize(const int size)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto bounded = std::clamp(size, 8, 32);
    if (bounded == m_font.pixelSize()) {
        return;
    }
    m_font.setPixelSize(bounded);
    invalidateMetrics();
    emit fontPixelSizeChanged();
}

void TerminalView::setLineHeight(const double lineHeight)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto bounded = std::clamp(lineHeight, 0.8, 2.0);
    if (qFuzzyCompare(bounded, m_lineHeight)) {
        return;
    }
    m_lineHeight = bounded;
    invalidateMetrics();
    emit lineHeightChanged();
}

void TerminalView::setCursorStyle(const int style)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto next = style == 1
        ? TerminalCursorStyle::Bar
        : style == 2 ? TerminalCursorStyle::Underline : TerminalCursorStyle::Block;
    if (next == m_cursorStyle) {
        return;
    }
    m_cursorStyle = next;
    m_renderedFrame.reset();
    emit cursorStyleChanged();
    update();
    configureAttachedKernel();
}

void TerminalView::setCursorBlink(const bool enabled)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (enabled == m_cursorBlink) {
        return;
    }
    m_cursorBlink = enabled;
    m_renderedFrame.reset();
    emit cursorBlinkChanged();
    updateBlinkTimer();
    update();
    configureAttachedKernel();
}

void TerminalView::setScrollbackLines(const int lines)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto bounded = std::clamp(lines, 100, 100'000);
    if (bounded == m_scrollbackLines) {
        return;
    }
    m_scrollbackLines = bounded;
    emit scrollbackLinesChanged();
    configureAttachedKernel();
}

void TerminalView::setSelectionBackground(const QColor& color)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!color.isValid() || color == m_palette.selectionBackground) {
        return;
    }
    m_palette.selectionBackground = color;
    m_renderedFrame.reset();
    emit selectionBackgroundChanged();
    update();
}

void TerminalView::setSelectionForeground(const QColor& color)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!color.isValid() || color == m_palette.selectionForeground) {
        return;
    }
    m_palette.selectionForeground = color;
    m_renderedFrame.reset();
    emit selectionForegroundChanged();
    update();
}

void TerminalView::setPreeditBackground(const QColor& color)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!color.isValid() || color == m_palette.preeditBackground) {
        return;
    }
    m_palette.preeditBackground = color;
    m_renderedFrame.reset();
    emit preeditBackgroundChanged();
    update();
}

void TerminalView::setPreeditForeground(const QColor& color)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!color.isValid() || color == m_palette.preeditForeground) {
        return;
    }
    m_palette.preeditForeground = color;
    m_renderedFrame.reset();
    emit preeditForegroundChanged();
    update();
}

void TerminalView::setTerminalCapabilities(
    const bool canSendInput,
    const bool canRetainFocus,
    const bool canSendFocus,
    const bool canResize)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_canSendInput == canSendInput
        && m_canRetainFocus == canRetainFocus
        && m_canSendFocus == canSendFocus
        && m_canResize == canResize) {
        return;
    }
    const auto wasReadOnly = readOnly();
    const auto desiredFocus = m_desiredFocus;
    const auto releaseFocus =
        ((m_canRetainFocus && !canRetainFocus)
            || (m_canSendFocus && !canSendFocus))
        && (m_focusClaimed || !m_pendingFocusRequestId.isEmpty()
            || desiredFocus);
    const auto gainedFocus =
        !m_canSendFocus && canSendFocus && desiredFocus;
    const auto gainedResize =
        !m_canResize && canResize;
    m_canSendInput = canSendInput;
    m_canRetainFocus = canRetainFocus;
    m_canSendFocus = canSendFocus;
    m_canResize = canResize;
    if (!m_canSendInput) {
        m_inputQueue.clear();
        m_queuedInputBytes = 0;
        m_inputRetryQueued = false;
        m_inputBackoffStep = 0;
        m_preedit.clear();
        m_renderedPreedit.clear();
        m_pasteShortcutKey.reset();
        if (auto* inputMethod = QGuiApplication::inputMethod()) {
            inputMethod->reset();
            inputMethod->update(Qt::ImEnabled | Qt::ImCursorRectangle);
        }
    }
    if (releaseFocus) {
        sendFocus(false);
        m_desiredFocus = desiredFocus;
    }
    if (gainedFocus && m_desiredFocus
        && !(m_focusRetryQueued
            && m_focusRetryOperation == FocusOperation::Blur)) {
        sendFocus(true);
    }
    if (!m_canResize) {
        m_resizeRetryQueued = false;
        m_pendingResize.reset();
        m_resizeClaimPending = false;
        m_lastResizeKey.clear();
    } else if (gainedResize) {
        m_resizeClaimPending = m_focusedSizeAuthority;
        scheduleResize();
    }
    emit capabilitiesChanged();
    if (wasReadOnly != readOnly()) {
        if (auto* accessible = qobject_cast<QQuickAccessibleAttached*>(
                qmlAttachedPropertiesObject<QQuickAccessibleAttached>(
                    this,
                    true))) {
            accessible->set_readOnly(readOnly());
            accessible->set_editable(!readOnly());
        }
        if (QAccessible::isActive()) {
            QAccessible::State changed;
            changed.readOnly = true;
            changed.editable = true;
            QAccessibleStateChangeEvent event(this, changed);
            QAccessible::updateAccessibility(&event);
        }
    }
    update();
}

void TerminalView::setFocusedSizeAuthority(const bool focused)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_focusedSizeAuthority == focused) {
        return;
    }
    m_focusedSizeAuthority = focused;
    m_resizeClaimPending = focused && m_canResize;
    emit focusedSizeAuthorityChanged();
    if (m_resizeClaimPending) {
        scheduleResize();
    }
}

bool TerminalView::attach(
    TerminalSessionRegistry& registry,
    TerminalCommandDispatcher& runtime,
    TerminalSubscription subscription,
    QString expectedRuntimeIncarnationId)
{
    Q_ASSERT(thread() == QThread::currentThread());
    detach();
    if (subscription.sessionId.isEmpty() || subscription.subscriptionId.isEmpty()
        || expectedRuntimeIncarnationId.isEmpty()) {
        return false;
    }

    m_registry = &registry;
    m_runtime = &runtime;
    m_subscription = std::move(subscription);
    m_expectedRuntimeIncarnationId = std::move(expectedRuntimeIncarnationId);
    if (m_nextSurfaceGeneration == std::numeric_limits<std::uint64_t>::max()) {
        m_registry = nullptr;
        m_runtime = nullptr;
        return false;
    }
    m_surfaceGeneration = ++m_nextSurfaceGeneration;
    m_renderPerformanceGeneration.fetch_add(1, std::memory_order_acq_rel);
    const auto epoch = m_attachmentEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_frameMailbox.reset(epoch);
    const auto registered = m_registry->registerSession(
        m_subscription,
        {
            .frameChanged = [this, epoch](auto frame) {
                enqueueFrame(epoch, std::move(frame));
            },
            .failed = [this, epoch](auto failure) {
                QMetaObject::invokeMethod(
                    this,
                    [this, epoch, failure = std::move(failure)]() mutable {
                        if (m_attachmentEpoch.load(std::memory_order_acquire) == epoch) {
                            presentFailure(std::move(failure));
                        }
                    },
                    Qt::QueuedConnection);
            },
            .connectionCompleted = [this, epoch](const std::int32_t result) {
                QMetaObject::invokeMethod(
                    this,
                    [this, epoch, result] {
                        if (m_attachmentEpoch.load(std::memory_order_acquire) == epoch) {
                            emit connectionCompleted(result == KODOSI_FFI_OK, result);
                            if (result == KODOSI_FFI_OK && hasActiveFocus()) {
                                sendFocus(true);
                            }
                        }
                    },
                    Qt::QueuedConnection);
            },
            .focusCompleted = [this, epoch](auto outcome) {
                QMetaObject::invokeMethod(
                    this,
                    [this, epoch, outcome = std::move(outcome)]() mutable {
                        if (m_attachmentEpoch.load(std::memory_order_acquire) == epoch) {
                            handleFocusOutcome(std::move(outcome));
                        }
                    },
                    Qt::QueuedConnection);
            },
            .resizeCompleted = [this, epoch](auto outcome) {
                QMetaObject::invokeMethod(
                    this,
                    [this, epoch, outcome = std::move(outcome)]() mutable {
                        if (m_attachmentEpoch.load(std::memory_order_acquire) == epoch) {
                            handleResizeOutcome(std::move(outcome));
                        }
                    },
                    Qt::QueuedConnection);
            },
            .notificationRequested = [this, epoch](auto notification) {
                QMetaObject::invokeMethod(
                    this,
                    [this,
                     epoch,
                     notification = std::move(notification)]() mutable {
                        if (m_attachmentEpoch.load(std::memory_order_acquire)
                            == epoch) {
                            emit terminalNotificationRequested(
                                std::move(notification.title),
                                std::move(notification.body));
                        }
                    },
                    Qt::QueuedConnection);
            },
            .closed = [this, epoch] {
                QMetaObject::invokeMethod(
                    this,
                    [this, epoch] {
                        if (m_attachmentEpoch.load(std::memory_order_acquire) != epoch) {
                            return;
                        }
                        detach();
                        emit terminalClosed();
                    },
                    Qt::QueuedConnection);
            },
        },
        kernelSettings());
    if (!registered) {
        m_registry = nullptr;
        m_runtime = nullptr;
        return false;
    }

    if (auto connected = m_runtime->connectTerminal(m_subscription); !connected) {
        const auto message = connected.error().message;
        m_registry->unregisterSession(m_subscription);
        m_registry = nullptr;
        m_runtime = nullptr;
        emit terminalError(message);
        return false;
    }
    m_resizeClaimPending =
        m_focusedSizeAuthority && m_canResize;
    scheduleResize();
    return true;
}

void TerminalView::detach()
{
    m_renderPerformanceGeneration.fetch_add(1, std::memory_order_acq_rel);
    Q_ASSERT(thread() == QThread::currentThread());
    const auto priorAccessibleText = accessibleText();
    const auto wasReady = m_terminalReady;
    const auto ownsComposition = hasActiveFocus() && !m_preedit.isEmpty();
    if (m_runtime != nullptr && (m_focusClaimed || !m_pendingFocusRequestId.isEmpty())) {
        sendFocus(false);
        for (const auto delay : inputBackoffMilliseconds) {
            if (!m_focusRetryQueued) {
                break;
            }
            m_focusRetryQueued = false;
            m_focusRetryOperation = FocusOperation::None;
            QThread::msleep(static_cast<unsigned long>(delay));
            sendFocus(false);
        }
    }
    const auto epoch = m_attachmentEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_frameMailbox.reset(epoch);
    auto* registry = std::exchange(m_registry, nullptr);
    auto* runtime = std::exchange(m_runtime, nullptr);
    if (registry != nullptr) {
        registry->unregisterSession(m_subscription);
    }
    if (runtime != nullptr && runtime->isRunning()) {
        (void)runtime->disconnectTerminal(m_subscription);
    }
    m_subscription = {};
    m_expectedRuntimeIncarnationId.clear();
    m_surfaceGeneration = 0;
    m_resizeQueued = false;
    m_lastResizeKey.clear();
    m_pendingResize.reset();
    m_pendingFocusRequestId.clear();
    m_focusClaimed = false;
    m_terminalReady = false;
    m_focusRetryQueued = false;
    m_focusRetryOperation = FocusOperation::None;
    m_resizeRetryQueued = false;
    m_resizeClaimPending = false;
    m_desiredFocus = false;
    m_inputQueue.clear();
    m_queuedInputBytes = 0;
    m_inputRetryQueued = false;
    m_inputBackoffStep = 0;
    m_preedit.clear();
    m_renderedPreedit.clear();
    m_selecting = false;
    m_copyShortcutActive = false;
    m_pasteShortcutKey.reset();
    m_pressedLink.reset();
    m_wheelAngleRemainder = 0;
    m_wheelPixelRemainder = 0.0;
    clearHoverLink();
    updateBlinkTimer();
    if (auto* inputMethod = QGuiApplication::inputMethod()) {
        if (ownsComposition) {
            inputMethod->reset();
        }
        inputMethod->update(Qt::ImEnabled | Qt::ImCursorRectangle);
    }
    m_frame.reset();
    m_renderedFrame.reset();
    if (wasReady) {
        emit terminalReadyChanged();
    }
    if (QAccessible::isActive()) {
        publishAccessibleTextChange(this, priorAccessibleText, {});
        if (wasReady) {
            QAccessible::State changed;
            changed.disabled = true;
            QAccessibleStateChangeEvent event(this, changed);
            QAccessible::updateAccessibility(&event);
        }
    }
    update();
}

QSGNode* TerminalView::updatePaintNode(
    QSGNode* oldNode,
    UpdatePaintNodeData* updateData)
{
    Q_UNUSED(updateData);
    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!m_frame || window() == nullptr || width() <= 0.0 || height() <= 0.0) {
        m_renderedFrame.reset();
        m_renderedDevicePixelRatio = 0.0;
        delete node;
        return nullptr;
    }
    const auto nodeNeedsTexture = node == nullptr || node->texture() == nullptr;
    if (node == nullptr) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
    }

    const auto devicePixelRatio = window()->effectiveDevicePixelRatio();
    if (nodeNeedsTexture || m_renderedFrame != m_frame
        || !qFuzzyCompare(m_renderedDevicePixelRatio, devicePixelRatio)
        || m_renderedFontPixelSize != m_font.pixelSize()
        || m_renderedPreedit != m_preedit) {
        TerminalRasterizer::Options options;
        options.palette = m_palette;
        options.overlay.preedit = m_preedit;
        options.overlay.column = m_frame->cursor.column;
        options.overlay.row = m_frame->cursor.row;
        options.lineHeight = m_lineHeight;
        options.cursorStyle = m_cursorStyle;
        options.cursorPhaseVisible = m_cursorPhaseVisible;
        QElapsedTimer rasterTimer;
        rasterTimer.start();
        const auto image = TerminalRasterizer::render(
            *m_frame,
            m_font,
            devicePixelRatio,
            options);
        if (image.isNull()) {
            queueRenderPerformance(
                rasterTimer.elapsed(),
                QStringLiteral("invalid"));
            delete node;
            return nullptr;
        }
        auto* texture = window()->createTextureFromImage(
            image,
            QQuickWindow::TextureHasAlphaChannel);
        if (texture == nullptr) {
            queueRenderPerformance(
                rasterTimer.elapsed(),
                QStringLiteral("texture-failed"));
            delete node;
            return nullptr;
        }
        queueRenderPerformance(rasterTimer.elapsed(), QStringLiteral("ok"));
        node->setTexture(texture);
        m_renderedFrame = m_frame;
        m_renderedDevicePixelRatio = devicePixelRatio;
        m_renderedFontPixelSize = m_font.pixelSize();
        m_renderedPreedit = m_preedit;
    }
    node->setRect(boundingRect());
    node->setFiltering(QSGTexture::Nearest);
    return node;
}

void TerminalView::queueRenderPerformance(
    const qint64 durationMilliseconds,
    QString outcome)
{
    if (durationMilliseconds < 16
        || m_renderPerformanceQueued.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const auto generation =
        m_renderPerformanceGeneration.load(std::memory_order_acquire);
    const QPointer<TerminalView> self(this);
    QMetaObject::invokeMethod(
        this,
        [self,
         generation,
         durationMilliseconds = std::clamp<qint64>(
             durationMilliseconds,
             0,
             60'000),
         outcome = outcome.left(32)] {
            if (self == nullptr) {
                return;
            }
            if (self->m_renderPerformanceGeneration.load(
                       std::memory_order_acquire)
                    != generation) {
                self->m_renderPerformanceQueued.store(
                    false,
                    std::memory_order_release);
                return;
            }
            self->m_renderPerformanceQueued.store(
                false,
                std::memory_order_release);
            qCInfo(kodosiPerformance).noquote()
                << QStringLiteral(
                       "category=terminal operation=frame.raster "
                       "duration_ms=%1 outcome=%2")
                       .arg(
                           QString::number(durationMilliseconds),
                           outcome);
        },
        Qt::QueuedConnection);
}

void TerminalView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
        scheduleResize();
    }
}

void TerminalView::keyPressEvent(QKeyEvent* event)
{
    if (m_runtime == nullptr || m_registry == nullptr || !m_terminalReady) {
        QQuickItem::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_C
        && event->modifiers().testFlags(Qt::ControlModifier | Qt::ShiftModifier)) {
        m_copyShortcutActive = true;
        (void)copySelection();
        event->accept();
        return;
    }
    if (isPasteShortcut(event)) {
        if (!event->isAutoRepeat()) {
            m_pasteShortcutKey = event->key();
        }
        if (m_canSendInput && !event->isAutoRepeat()) {
            pasteClipboard();
        }
        event->accept();
        return;
    }
    if (!m_canSendInput) {
        QQuickItem::keyPressEvent(event);
        return;
    }
    const auto text = event->text();
    const auto key = terminalKey(event);
    if (key && sendKey(
            event,
            event->isAutoRepeat() ? TerminalKeyAction::Repeat : TerminalKeyAction::Press)) {
        event->accept();
        return;
    }
    if (!text.isEmpty()
        && !event->modifiers().testAnyFlags(
            Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)
        && event->key() != Qt::Key_Return
        && event->key() != Qt::Key_Enter
        && event->key() != Qt::Key_Tab
        && event->key() != Qt::Key_Backtab) {
        sendText(text);
        event->accept();
        return;
    }
    if (key && sendKey(
            event,
            event->isAutoRepeat() ? TerminalKeyAction::Repeat : TerminalKeyAction::Press)) {
        event->accept();
        return;
    }
    QQuickItem::keyPressEvent(event);
}

void TerminalView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_C && m_copyShortcutActive) {
        m_copyShortcutActive = false;
        event->accept();
        return;
    }
    if (m_pasteShortcutKey && event->key() == *m_pasteShortcutKey) {
        m_pasteShortcutKey.reset();
        event->accept();
        return;
    }
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }
    if (!m_terminalReady || !m_canSendInput) {
        QQuickItem::keyReleaseEvent(event);
        return;
    }
    if (sendKey(event, TerminalKeyAction::Release)) {
        event->accept();
        return;
    }
    QQuickItem::keyReleaseEvent(event);
}

void TerminalView::inputMethodEvent(QInputMethodEvent* event)
{
    if (!m_terminalReady || !m_canSendInput) {
        QQuickItem::inputMethodEvent(event);
        return;
    }
    if (!event->commitString().isEmpty()) {
        sendText(event->commitString());
    }
    if (m_preedit != event->preeditString()) {
        m_preedit = event->preeditString();
        update();
    }
    event->accept();
}

QVariant TerminalView::inputMethodQuery(const Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return m_runtime != nullptr && m_terminalReady
            && m_canSendInput;
    case Qt::ImFont:
        return m_font;
    case Qt::ImCursorRectangle:
        if (m_frame) {
            const auto cell = TerminalRasterizer::cellSize(m_font, m_lineHeight);
            return QRectF(
                cell.width() * m_frame->cursor.column,
                cell.height() * m_frame->cursor.row,
                cell.width(),
                cell.height());
        }
        return QRectF {};
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return 0;
    case Qt::ImSurroundingText:
    case Qt::ImCurrentSelection:
        return QString {};
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void TerminalView::focusInEvent(QFocusEvent* event)
{
    QQuickItem::focusInEvent(event);
    sendFocus(true);
    updateBlinkTimer();
}

void TerminalView::focusOutEvent(QFocusEvent* event)
{
    sendFocus(false);
    m_copyShortcutActive = false;
    m_pasteShortcutKey.reset();
    m_preedit.clear();
    updateBlinkTimer();
    update();
    QQuickItem::focusOutEvent(event);
}

void TerminalView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_terminalReady) {
        QQuickItem::mousePressEvent(event);
        return;
    }
    const auto cell = terminalCellAt(event->position());
    if (!cell) {
        QQuickItem::mousePressEvent(event);
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    if (event->modifiers() == Qt::NoModifier) {
        if (auto link = linkAtCell(*cell, true)) {
            m_pressedLink = std::move(*link);
            m_selecting = false;
            event->accept();
            return;
        }
    }
    m_pressedLink.reset();
    beginSelection(*cell);
    event->accept();
}

void TerminalView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressedLink && event->buttons().testFlag(Qt::LeftButton)) {
        event->accept();
        return;
    }
    if (!m_selecting || !event->buttons().testFlag(Qt::LeftButton)) {
        QQuickItem::mouseMoveEvent(event);
        return;
    }
    if (const auto cell = terminalCellAt(
            event->position(),
            CellHitTest::ClampToViewport)) {
        updateSelection(*cell);
    }
    event->accept();
}

void TerminalView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressedLink) {
        const auto pressed = std::exchange(m_pressedLink, std::nullopt);
        const auto cell = terminalCellAt(
            event->position(),
            CellHitTest::Strict);
        const auto released = cell ? linkAtCell(*cell, true) : std::nullopt;
        if (released && *released == *pressed
            && !QDesktopServices::openUrl(*released)) {
            emit terminalError(QStringLiteral("The terminal link could not be opened."));
        }
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton || !m_selecting) {
        QQuickItem::mouseReleaseEvent(event);
        return;
    }
    if (const auto cell = terminalCellAt(
            event->position(),
            CellHitTest::ClampToViewport)) {
        updateSelection(*cell);
    }
    m_selecting = false;
    event->accept();
}

void TerminalView::hoverMoveEvent(QHoverEvent* event)
{
    updateHoverLink(event->position());
    event->accept();
}

void TerminalView::hoverLeaveEvent(QHoverEvent* event)
{
    clearHoverLink();
    event->accept();
}

void TerminalView::wheelEvent(QWheelEvent* event)
{
    if (!m_terminalReady || m_registry == nullptr) {
        QQuickItem::wheelEvent(event);
        return;
    }
    const auto rows = wheelRows(event);
    if (rows != 0) {
        auto scrolled = m_registry->scrollViewport(m_subscription, rows);
        if (!scrolled) {
            emit terminalError(scrolled.error().message);
        }
    }
    if (event->pixelDelta().y() != 0 || event->angleDelta().y() != 0) {
        event->accept();
        return;
    }
    QQuickItem::wheelEvent(event);
}

void TerminalView::itemChange(const ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged) {
        update();
        scheduleResize();
    } else if (change == ItemVisibleHasChanged) {
        updateBlinkTimer();
        scheduleResize();
    } else if (change == ItemSceneChange) {
        QObject::disconnect(m_windowVisibilityConnection);
        if (value.window != nullptr) {
            m_windowVisibilityConnection = connect(
                value.window,
                &QWindow::visibilityChanged,
                this,
                [this] { updateBlinkTimer(); });
        }
        updateBlinkTimer();
    }
}

void TerminalView::enqueueFrame(
    const std::uint64_t epoch,
    GhosttyTerminalKernel::Frame frame)
{
    if (m_attachmentEpoch.load(std::memory_order_acquire) != epoch) {
        return;
    }
    const auto enqueued = m_frameMailbox.enqueue(epoch, std::move(frame));
    if (enqueued.queueDrain) {
        QMetaObject::invokeMethod(
            this,
            [this, epoch] { drainFrame(epoch); },
            Qt::QueuedConnection);
    }
}

void TerminalView::drainFrame(const std::uint64_t epoch)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_attachmentEpoch.load(std::memory_order_acquire) != epoch) {
        return;
    }
    auto frame = m_frameMailbox.take(epoch);
    if (frame) {
        presentFrame(std::move(frame));
    }
}

void TerminalView::presentFrame(GhosttyTerminalKernel::Frame frame)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto priorAccessibleText = accessibleText();
    m_frame = std::move(frame);
    m_pressedLink.reset();
    clearHoverLink();
    const auto wasReady = m_terminalReady;
    m_terminalReady = m_frame != nullptr;
    if (m_frame) {
        const auto sizeHint =
            TerminalRasterizer::logicalSize(*m_frame, m_font, m_lineHeight);
        setImplicitSize(sizeHint.width(), sizeHint.height());
    }
    emit frameChanged();
    if (wasReady != m_terminalReady) {
        emit terminalReadyChanged();
    }
    update();
    updateBlinkTimer();
    scheduleResize();
    if (auto* inputMethod = QGuiApplication::inputMethod()) {
        Qt::InputMethodQueries queries = Qt::ImCursorRectangle;
        if (wasReady != m_terminalReady) {
            queries |= Qt::ImEnabled;
        }
        inputMethod->update(queries);
    }
    if (QAccessible::isActive()) {
        publishAccessibleTextChange(this, priorAccessibleText, accessibleText());
        if (wasReady != m_terminalReady) {
            QAccessible::State changed;
            changed.disabled = true;
            QAccessibleStateChangeEvent event(this, changed);
            QAccessible::updateAccessibility(&event);
        }
        QAccessibleTextCursorEvent cursorEvent(this, accessibleCursorPosition());
        QAccessible::updateAccessibility(&cursorEvent);
        const auto selection = accessibleSelection();
        QAccessibleTextSelectionEvent selectionEvent(
            this,
            selection.first,
            selection.second);
        QAccessible::updateAccessibility(&selectionEvent);
    }
}

void TerminalView::sendText(const QString& text)
{
    if (!m_canSendInput || m_runtime == nullptr || text.isEmpty()) {
        return;
    }
    (void)enqueueInput(text.toUtf8());
}

void TerminalView::pasteClipboard()
{
    if (!m_canSendInput || m_registry == nullptr || m_runtime == nullptr
        || !m_terminalReady) {
        return;
    }
    auto* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return;
    }
    auto text = clipboard->text(QClipboard::Clipboard).toUtf8();
    if (text.isEmpty()) {
        return;
    }
    if (text.size() > maximumQueuedInputBytes
        || m_queuedInputBytes > maximumQueuedInputBytes - text.size()
        || m_inputQueue.size() >= maximumQueuedInputFrames) {
        emit terminalError(QStringLiteral("Terminal input queue is full; input was not accepted."));
        return;
    }
    auto encoded = m_registry->encodePaste(m_subscription, std::move(text));
    if (!encoded) {
        emit terminalError(encoded.error().message);
        return;
    }
    (void)enqueueInput(std::move(*encoded));
}

bool TerminalView::sendKey(QKeyEvent* event, const TerminalKeyAction action)
{
    const auto key = terminalKey(event);
    if (!m_canSendInput || !key || m_registry == nullptr
        || m_runtime == nullptr) {
        return false;
    }
    auto text = event->text();
    const auto unshifted = unshiftedCodepoint(event->key());
    if (unshifted != 0 && !text.isEmpty()) {
        const auto hasControlText = text.size() != 1
            || text.front().unicode() < QLatin1Char(' ').unicode()
            || text.front().unicode() == 0x7f;
        if (hasControlText) {
            auto character = QChar(static_cast<char16_t>(unshifted));
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                character = character.toUpper();
            }
            text = QString(character);
        }
    } else if (*key != TerminalKey::Character && *key != TerminalKey::Digit
        && *key != TerminalKey::Backquote && *key != TerminalKey::Backslash
        && *key != TerminalKey::BracketLeft && *key != TerminalKey::BracketRight
        && *key != TerminalKey::Comma && *key != TerminalKey::Equal
        && *key != TerminalKey::Minus && *key != TerminalKey::Period
        && *key != TerminalKey::Quote && *key != TerminalKey::Semicolon
        && *key != TerminalKey::Slash && *key != TerminalKey::Space) {
        text.clear();
    }
    auto encoded = m_registry->encodeKey(
        m_subscription,
        {
            .key = *key,
            .action = action,
            .text = std::move(text),
            .unshiftedCodepoint = unshifted,
            .modifiers = modifiers(event->modifiers()),
        });
    if (!encoded) {
        emit terminalError(encoded.error().message);
        return true;
    }
    (void)enqueueInput(std::move(*encoded));
    return true;
}

bool TerminalView::enqueueInput(QByteArray bytes)
{
    if (!m_canSendInput || bytes.isEmpty() || m_registry == nullptr
        || m_runtime == nullptr || !m_terminalReady) {
        return false;
    }
    if (bytes.size() > maximumQueuedInputBytes
        || m_queuedInputBytes > maximumQueuedInputBytes - bytes.size()
        || m_inputQueue.size() >= maximumQueuedInputFrames) {
        emit terminalError(QStringLiteral("Terminal input queue is full; input was not accepted."));
        return false;
    }
    auto bottom = m_registry->scrollViewportToBottom(m_subscription);
    if (!bottom) {
        emit terminalError(bottom.error().message);
        return false;
    }
    m_queuedInputBytes += bytes.size();
    m_inputQueue.push_back(std::move(bytes));
    drainInputQueue();
    return true;
}

void TerminalView::drainInputQueue()
{
    if (!m_canSendInput || m_runtime == nullptr
        || m_inputRetryQueued) {
        return;
    }
    for (int sent = 0; sent < 64 && !m_inputQueue.empty(); ++sent) {
        auto result = m_runtime->sendTerminalInput(
            m_subscription,
            m_expectedRuntimeIncarnationId,
            m_inputQueue.front());
        if (result) {
            m_queuedInputBytes -= m_inputQueue.front().size();
            m_inputQueue.pop_front();
            m_inputBackoffStep = 0;
            continue;
        }
        if (result.error().ffiResult == KODOSI_FFI_BUSY) {
            const auto step = std::min<std::size_t>(
                m_inputBackoffStep,
                std::size(inputBackoffMilliseconds) - 1);
            const auto epoch = m_attachmentEpoch.load(std::memory_order_acquire);
            m_inputRetryQueued = true;
            m_inputBackoffStep = static_cast<std::uint8_t>(
                std::min<std::size_t>(
                    step + 1,
                    std::size(inputBackoffMilliseconds) - 1));
            QTimer::singleShot(
                inputBackoffMilliseconds[step],
                this,
                [this, epoch] {
                    if (m_attachmentEpoch.load(std::memory_order_acquire) != epoch) {
                        return;
                    }
                    m_inputRetryQueued = false;
                    drainInputQueue();
                });
            return;
        }

        const auto message = result.error().message;
        m_inputQueue.clear();
        m_queuedInputBytes = 0;
        m_inputBackoffStep = 0;
        emit terminalError(message);
        return;
    }
    if (!m_inputQueue.empty()) {
        const auto epoch = m_attachmentEpoch.load(std::memory_order_acquire);
        m_inputRetryQueued = true;
        QTimer::singleShot(0, this, [this, epoch] {
            if (m_attachmentEpoch.load(std::memory_order_acquire) != epoch) {
                return;
            }
            m_inputRetryQueued = false;
            drainInputQueue();
        });
    }
}

void TerminalView::sendFocus(const bool focused)
{
    m_desiredFocus = focused;
    dispatchFocus(
        focused ? FocusOperation::Focus : FocusOperation::Blur);
}

void TerminalView::dispatchFocus(const FocusOperation operation)
{
    const auto focused = operation == FocusOperation::Focus;
    if (operation == FocusOperation::None) {
        return;
    }
    if (focused && !m_canSendFocus) {
        return;
    }
    if (m_runtime == nullptr || !m_runtime->isRunning()) {
        return;
    }
    if (focused && (m_focusClaimed || !m_pendingFocusRequestId.isEmpty())) {
        return;
    }
    auto command = QByteArrayLiteral("{\"type\":");
    command += jsonString(
        focused ? QStringLiteral("session.focus") : QStringLiteral("session.blur"));
    command += QByteArrayLiteral(",\"sessionId\":");
    command += jsonString(m_subscription.sessionId);
    command += QByteArrayLiteral(",\"clientId\":");
    command += jsonString(m_subscription.subscriptionId);
    if (focused) {
        m_pendingFocusRequestId =
            QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        command += QByteArrayLiteral(",\"requestId\":");
        command += jsonString(m_pendingFocusRequestId);
    }
    command += QByteArrayLiteral(",\"expectedRuntimeIncarnationId\":");
    command += jsonString(m_expectedRuntimeIncarnationId);
    command += '}';
    if (auto result = m_runtime->send(CommandLane::Terminal, command); !result) {
        if (focused) {
            m_pendingFocusRequestId.clear();
        }
        if (result.error().ffiResult == KODOSI_FFI_BUSY) {
            scheduleFocusRetry(operation);
        } else {
            m_focusRetryQueued = false;
            m_focusRetryOperation = FocusOperation::None;
            emit terminalError(result.error().message);
        }
        return;
    }
    m_focusRetryQueued = false;
    m_focusRetryOperation = FocusOperation::None;
    if (!focused) {
        m_focusClaimed = false;
        m_pendingFocusRequestId.clear();
        if (m_desiredFocus && m_canSendFocus) {
            sendFocus(true);
        }
    }
}

void TerminalView::scheduleFocusRetry(
    const FocusOperation operation)
{
    if (m_focusRetryQueued
        && m_focusRetryOperation == operation) {
        return;
    }
    const auto epoch = m_attachmentEpoch.load(std::memory_order_acquire);
    m_focusRetryQueued = true;
    m_focusRetryOperation = operation;
    QTimer::singleShot(16, this, [this, epoch] {
        if (m_attachmentEpoch.load(std::memory_order_acquire) != epoch) {
            return;
        }
        const auto operation = m_focusRetryOperation;
        m_focusRetryQueued = false;
        m_focusRetryOperation = FocusOperation::None;
        dispatchFocus(operation);
    });
}

void TerminalView::scheduleResizeRetry()
{
    if (m_resizeRetryQueued) {
        return;
    }
    const auto epoch = m_attachmentEpoch.load(std::memory_order_acquire);
    m_resizeRetryQueued = true;
    QTimer::singleShot(16, this, [this, epoch] {
        if (m_attachmentEpoch.load(std::memory_order_acquire) != epoch) {
            return;
        }
        m_resizeRetryQueued = false;
        dispatchResize();
    });
}

void TerminalView::handleFocusOutcome(TerminalFocusOutcome outcome)
{
    if (outcome.requestId != m_pendingFocusRequestId
        || outcome.runtimeIncarnationId != m_expectedRuntimeIncarnationId) {
        return;
    }
    m_pendingFocusRequestId.clear();
    m_focusClaimed = outcome.applied;
    if (!outcome.applied) {
        emit terminalError(
            outcome.reason.isEmpty()
                ? QStringLiteral("The runtime rejected terminal focus.")
                : std::move(outcome.reason));
    }
}

void TerminalView::handleResizeOutcome(TerminalResizeOutcome outcome)
{
    if (!m_pendingResize
        || outcome.sessionId != m_subscription.sessionId
        || outcome.subscriptionId != m_subscription.subscriptionId
        || outcome.requestId != m_pendingResize->requestId
        || outcome.expectedRuntimeIncarnationId != m_expectedRuntimeIncarnationId
        || outcome.subscriptionGeneration != m_subscription.generation
        || outcome.surfaceGeneration != m_surfaceGeneration
        || outcome.columns != m_pendingResize->columns
        || outcome.rows != m_pendingResize->rows
        || outcome.widthPixels != m_pendingResize->widthPixels
        || outcome.heightPixels != m_pendingResize->heightPixels
        || outcome.cellWidthPixels != m_pendingResize->cellWidthPixels
        || outcome.cellHeightPixels != m_pendingResize->cellHeightPixels) {
        return;
    }
    const auto completedClaim = m_pendingResize->claim;
    if (outcome.applied) {
        m_lastResizeKey = m_pendingResize->key;
        if (completedClaim) {
            m_resizeClaimPending = false;
        }
    } else {
        emit terminalError(
            outcome.reason.isEmpty()
                ? QStringLiteral("The runtime rejected terminal resize.")
                : std::move(outcome.reason));
    }
    m_pendingResize.reset();
    if (!completedClaim && m_resizeClaimPending) {
        scheduleResize();
    }
}

void TerminalView::scheduleResize()
{
    if (m_resizeQueued || m_runtime == nullptr || m_frame == nullptr) {
        return;
    }
    const auto epoch = m_attachmentEpoch.load(std::memory_order_acquire);
    m_resizeQueued = true;
    QTimer::singleShot(0, this, [this, epoch] {
        if (m_attachmentEpoch.load(std::memory_order_acquire) != epoch) {
            return;
        }
        m_resizeQueued = false;
        dispatchResize();
    });
}

void TerminalView::dispatchResize()
{
    if (m_runtime == nullptr || !m_runtime->isRunning() || m_surfaceGeneration == 0
        || !m_canResize || !isVisible()
        || width() <= 0.0 || height() <= 0.0) {
        return;
    }
    const auto cell = TerminalRasterizer::cellSize(m_font, m_lineHeight);
    if (cell.width() <= 0.0 || cell.height() <= 0.0) {
        return;
    }
    const auto columns = static_cast<std::uint16_t>(std::clamp(
        std::floor(width() / cell.width()),
        1.0,
        static_cast<double>(std::numeric_limits<std::uint16_t>::max())));
    const auto rows = static_cast<std::uint16_t>(std::clamp(
        std::floor(height() / cell.height()),
        1.0,
        static_cast<double>(std::numeric_limits<std::uint16_t>::max())));
    const auto dpr = window() == nullptr ? 1.0 : window()->effectiveDevicePixelRatio();
    const auto cellWidthPixels = static_cast<std::uint32_t>(
        std::max(1LL, std::llround(cell.width() * dpr)));
    const auto cellHeightPixels = static_cast<std::uint32_t>(
        std::max(1LL, std::llround(cell.height() * dpr)));
    const auto widthProduct =
        static_cast<std::uint64_t>(columns) * cellWidthPixels;
    const auto heightProduct =
        static_cast<std::uint64_t>(rows) * cellHeightPixels;
    if (widthProduct > std::numeric_limits<std::uint32_t>::max()
        || heightProduct > std::numeric_limits<std::uint32_t>::max()) {
        emit terminalError(QStringLiteral("Terminal pixel geometry exceeds the runtime limit."));
        return;
    }
    const auto widthPixels = static_cast<std::uint32_t>(widthProduct);
    const auto heightPixels = static_cast<std::uint32_t>(heightProduct);
    auto resizeKey = QByteArray::number(columns) + 'x' + QByteArray::number(rows)
        + ':' + QByteArray::number(widthPixels) + 'x' + QByteArray::number(heightPixels)
        + ':' + QByteArray::number(cellWidthPixels) + 'x' + QByteArray::number(cellHeightPixels);
    if ((!m_resizeClaimPending && resizeKey == m_lastResizeKey)
        || (m_pendingResize && resizeKey == m_pendingResize->key)) {
        return;
    }
    PendingResize pending {
        .key = std::move(resizeKey),
        .requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces),
        .columns = columns,
        .rows = rows,
        .widthPixels = widthPixels,
        .heightPixels = heightPixels,
        .cellWidthPixels = cellWidthPixels,
        .cellHeightPixels = cellHeightPixels,
        .claim = m_resizeClaimPending,
    };

    auto command = QByteArrayLiteral("{\"type\":\"session.resize\",\"sessionId\":");
    command += jsonString(m_subscription.sessionId);
    command += QByteArrayLiteral(",\"requestId\":");
    command += jsonString(pending.requestId);
    command += QByteArrayLiteral(",\"expectedRuntimeIncarnationId\":");
    command += jsonString(m_expectedRuntimeIncarnationId);
    command += QByteArrayLiteral(",\"subscriptionId\":");
    command += jsonString(m_subscription.subscriptionId);
    command += QByteArrayLiteral(",\"subscriptionGeneration\":");
    command += QByteArray::number(m_subscription.generation);
    command += QByteArrayLiteral(",\"surfaceGeneration\":");
    command += QByteArray::number(m_surfaceGeneration);
    command += QByteArrayLiteral(",\"cols\":");
    command += QByteArray::number(columns);
    command += QByteArrayLiteral(",\"rows\":");
    command += QByteArray::number(rows);
    command += QByteArrayLiteral(",\"widthPixels\":");
    command += QByteArray::number(widthPixels);
    command += QByteArrayLiteral(",\"heightPixels\":");
    command += QByteArray::number(heightPixels);
    command += QByteArrayLiteral(",\"cellWidthPixels\":");
    command += QByteArray::number(cellWidthPixels);
    command += QByteArrayLiteral(",\"cellHeightPixels\":");
    command += QByteArray::number(cellHeightPixels);
    command += pending.claim
        ? QByteArrayLiteral(",\"claim\":true}")
        : QByteArrayLiteral(",\"claim\":false}");
    m_pendingResize = std::move(pending);
    if (auto result = m_runtime->send(CommandLane::Terminal, command); !result) {
        m_pendingResize.reset();
        if (result.error().ffiResult == KODOSI_FFI_BUSY) {
            scheduleResizeRetry();
        } else {
            emit terminalError(result.error().message);
        }
    }
}

void TerminalView::presentFailure(GhosttyTerminalKernel::Failure failure)
{
    Q_ASSERT(thread() == QThread::currentThread());
    emit terminalError(std::move(failure.message));
}

std::optional<QPoint> TerminalView::terminalCellAt(
    const QPointF& position,
    const CellHitTest hitTest) const
{
    if (!m_frame || m_frame->columns == 0 || m_frame->rows == 0) {
        return std::nullopt;
    }
    const auto cell = TerminalRasterizer::cellSize(m_font, m_lineHeight);
    if (cell.width() <= 0.0 || cell.height() <= 0.0) {
        return std::nullopt;
    }
    if (hitTest == CellHitTest::Strict
        && (position.x() < 0.0 || position.y() < 0.0
            || position.x() >= width() || position.y() >= height()
            || position.x() >= cell.width() * m_frame->columns
            || position.y() >= cell.height() * m_frame->rows)) {
        return std::nullopt;
    }
    const auto column = std::clamp(
        static_cast<int>(std::floor(position.x() / cell.width())),
        0,
        static_cast<int>(m_frame->columns) - 1);
    const auto row = std::clamp(
        static_cast<int>(std::floor(position.y() / cell.height())),
        0,
        static_cast<int>(m_frame->rows) - 1);
    return QPoint(column, row);
}

std::optional<QUrl> TerminalView::linkAtCell(
    const QPoint& cell,
    const bool reportFailure)
{
    if (m_registry == nullptr || !m_terminalReady) {
        return std::nullopt;
    }
    auto value = m_registry->linkAt(
        m_subscription,
        m_frame->viewportRevision,
        static_cast<std::uint16_t>(cell.x()),
        static_cast<std::uint16_t>(cell.y()));
    if (!value) {
        if (reportFailure
            && value.error().code
                != GhosttyTerminalKernel::Failure::Code::StaleFrame) {
            emit terminalError(value.error().message);
        }
        return std::nullopt;
    }
    return validatedTerminalLink(*value);
}

void TerminalView::updateHoverLink(const QPointF& position)
{
    const auto cell = terminalCellAt(position, CellHitTest::Strict);
    if (cell && linkAtCell(*cell, false)) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
}

void TerminalView::clearHoverLink()
{
    unsetCursor();
}

int TerminalView::wheelRows(QWheelEvent* event)
{
    const auto cell = TerminalRasterizer::cellSize(m_font, m_lineHeight);
    if (event->pixelDelta().y() != 0 && cell.height() > 0.0) {
        const auto bounded = std::clamp(
            static_cast<qreal>(event->pixelDelta().y()),
            -cell.height() * maximumWheelRowsPerEvent,
            cell.height() * maximumWheelRowsPerEvent);
        m_wheelPixelRemainder += bounded;
        const auto rows = std::clamp(
            static_cast<int>(m_wheelPixelRemainder / cell.height()),
            -maximumWheelRowsPerEvent,
            maximumWheelRowsPerEvent);
        m_wheelPixelRemainder -= rows * cell.height();
        return -rows;
    }
    if (event->angleDelta().y() != 0) {
        const auto bounded = std::clamp(
            event->angleDelta().y(),
            -120 * maximumWheelRowsPerEvent,
            120 * maximumWheelRowsPerEvent);
        m_wheelAngleRemainder += bounded;
        const auto rows = std::clamp(
            m_wheelAngleRemainder / 120,
            -maximumWheelRowsPerEvent,
            maximumWheelRowsPerEvent);
        m_wheelAngleRemainder -= rows * 120;
        return -rows;
    }
    return 0;
}

void TerminalView::beginSelection(const QPoint& anchor)
{
    if (m_registry == nullptr || m_frame == nullptr) {
        return;
    }
    auto selected = m_registry->beginSelection(
        m_subscription,
        m_frame->viewportRevision,
        static_cast<std::uint16_t>(anchor.x()),
        static_cast<std::uint16_t>(anchor.y()));
    m_selecting = selected.has_value();
    if (!selected && !selected.error().isRecoverableHitTestRace()) {
        emit terminalError(selected.error().message);
    }
}

void TerminalView::updateSelection(const QPoint& endpoint)
{
    if (m_registry == nullptr || m_frame == nullptr) {
        return;
    }
    auto selected = m_registry->updateSelection(
        m_subscription,
        m_frame->viewportRevision,
        static_cast<std::uint16_t>(endpoint.x()),
        static_cast<std::uint16_t>(endpoint.y()));
    if (!selected) {
        if (selected.error().isRecoverableHitTestRace()) {
            m_selecting = false;
            if (auto cleared = m_registry->clearSelection(m_subscription);
                !cleared) {
                emit terminalError(cleared.error().message);
            }
            return;
        }
        emit terminalError(selected.error().message);
    }
}

bool TerminalView::copySelection()
{
    if (m_registry == nullptr) {
        return false;
    }
    auto text = m_registry->selectedText(m_subscription);
    if (!text) {
        emit terminalError(text.error().message);
        return true;
    }
    if (text->isEmpty()) {
        return false;
    }
    QGuiApplication::clipboard()->setText(*text, QClipboard::Clipboard);
    return true;
}

TerminalKernelSettings TerminalView::kernelSettings() const
{
    return {
        .cursorStyle = m_cursorStyle,
        .cursorBlink = m_cursorBlink,
        .scrollbackLines = static_cast<std::size_t>(m_scrollbackLines),
        .scrollbackBytes = terminalScrollbackByteBudget(
            static_cast<std::size_t>(m_scrollbackLines)),
    };
}

void TerminalView::configureAttachedKernel()
{
    if (m_registry == nullptr) {
        return;
    }
    if (auto configured =
            m_registry->configure(m_subscription, kernelSettings());
        !configured) {
        emit terminalError(configured.error().message);
    }
}

bool TerminalView::blinkEligible() const
{
    return m_cursorBlink && m_terminalReady && m_frame
        && m_frame->cursor.visible && hasActiveFocus() && isVisible()
        && window() != nullptr && window()->isVisible();
}

void TerminalView::updateBlinkTimer()
{
    if (blinkEligible()) {
        if (!m_cursorBlinkTimer.isActive()) {
            m_cursorPhaseVisible = true;
            m_cursorBlinkTimer.start();
        }
        return;
    }
    m_cursorBlinkTimer.stop();
    if (!m_cursorPhaseVisible) {
        m_cursorPhaseVisible = true;
        m_renderedFrame.reset();
        update();
    }
}

void TerminalView::invalidateMetrics()
{
    m_renderedFrame.reset();
    if (m_frame) {
        const auto sizeHint =
            TerminalRasterizer::logicalSize(*m_frame, m_font, m_lineHeight);
        setImplicitSize(sizeHint.width(), sizeHint.height());
    }
    update();
    scheduleResize();
    if (auto* inputMethod = QGuiApplication::inputMethod()) {
        inputMethod->update(Qt::ImFont | Qt::ImCursorRectangle);
    }
}

} // namespace kodosi
