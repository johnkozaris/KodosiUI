#include "terminal/GhosttyTerminalKernel.hpp"
#include "terminal/GhosttyC.hpp"
#include "terminal/TerminalAccessibility.hpp"
#include "terminal/TerminalRasterizer.hpp"
#include "terminal/TerminalSessionRegistry.hpp"
#include "terminal/TerminalSurfaceController.hpp"
#include "terminal/TerminalView.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QAccessible>
#include <QByteArray>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest/QTest>

#include <optional>
#include <utility>

namespace {

constexpr std::int32_t ffiOk = 0;
constexpr std::int32_t ffiBusy = 6;

class FakeTerminalDispatcher final
    : public kodosi::TerminalCommandDispatcher {
public:
    bool running = true;
    int connectCount = 0;
    int disconnectCount = 0;
    int busyTerminalCommands = 0;
    int busyInputCommands = 0;
    QVector<QJsonObject> terminalCommands;
    QVector<QByteArray> inputCommands;

    bool isRunning() const noexcept override
    {
        return running;
    }

    Result send(
        const kodosi::CommandLane lane,
        const QByteArrayView json) override
    {
        if (lane != kodosi::CommandLane::Terminal) {
            return {};
        }
        const auto document =
            QJsonDocument::fromJson(json.toByteArray());
        if (document.isObject()) {
            terminalCommands.append(document.object());
        }
        if (busyTerminalCommands > 0) {
            --busyTerminalCommands;
            return busyFailure();
        }
        return {};
    }

    Result connectTerminal(
        const kodosi::TerminalSubscription&) override
    {
        ++connectCount;
        return {};
    }

    Result disconnectTerminal(
        const kodosi::TerminalSubscription&) override
    {
        ++disconnectCount;
        return {};
    }

    Result sendTerminalInput(
        const kodosi::TerminalSubscription&,
        const QString&,
        const QByteArrayView bytes) override
    {
        inputCommands.append(bytes.toByteArray());
        if (busyInputCommands > 0) {
            --busyInputCommands;
            return busyFailure();
        }
        return {};
    }

private:
    static Result busyFailure()
    {
        return std::unexpected(kodosi::RuntimeFailure {
            .code = kodosi::RuntimeFailure::Code::FfiRejected,
            .ffiResult = ffiBusy,
            .message = QStringLiteral("Busy"),
        });
    }
};

class FakeRuntimeBridge final : public kodosi::RuntimeBridge {
public:
    explicit FakeRuntimeBridge(kodosi::TerminalEventSink& sink)
        : RuntimeBridge(sink)
    {
    }

    bool isRunning() const noexcept override
    {
        return true;
    }

    Result send(
        kodosi::CommandLane,
        QByteArrayView) override
    {
        return {};
    }

    Result connectTerminal(
        const kodosi::TerminalSubscription&) override
    {
        ++connectCount;
        return {};
    }

    Result disconnectTerminal(
        const kodosi::TerminalSubscription&) override
    {
        ++disconnectCount;
        return {};
    }

    Result sendTerminalInput(
        const kodosi::TerminalSubscription&,
        const QString&,
        QByteArrayView) override
    {
        ++inputCount;
        return {};
    }

    int connectCount = 0;
    int disconnectCount = 0;
    int inputCount = 0;
};

QByteArray checkpointFor(const QByteArray& bytes, const std::uint16_t columns, const std::uint16_t rows)
{
    GhosttyTerminal terminal = nullptr;
    if (ghostty_terminal_new(nullptr, &terminal, columns, rows) != GHOSTTY_SUCCESS) {
        QTest::qFail("failed to create checkpoint terminal", __FILE__, __LINE__);
        return {};
    }
    ghostty_terminal_vt_write(
        terminal,
        reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()));

    const GhosttyCheckpointEncodeOptions options = GHOSTTY_CHECKPOINT_ENCODE_OPTIONS_INIT;
    GhosttyBuffer buffer {};
    GhosttyCheckpointInfo info = GHOSTTY_INIT_SIZED(GhosttyCheckpointInfo);
    if (ghostty_checkpoint_encode_buf(terminal, &options, &buffer, &info)
        != GHOSTTY_OUT_OF_SPACE) {
        ghostty_terminal_free(terminal);
        QTest::qFail("failed to size checkpoint", __FILE__, __LINE__);
        return {};
    }
    QByteArray checkpoint(static_cast<qsizetype>(buffer.len), Qt::Uninitialized);
    buffer.ptr = reinterpret_cast<std::uint8_t*>(checkpoint.data());
    buffer.cap = static_cast<std::size_t>(checkpoint.size());
    buffer.len = 0;
    if (ghostty_checkpoint_encode_buf(terminal, &options, &buffer, &info)
        != GHOSTTY_SUCCESS) {
        ghostty_terminal_free(terminal);
        QTest::qFail("failed to encode checkpoint", __FILE__, __LINE__);
        return {};
    }
    checkpoint.resize(static_cast<qsizetype>(buffer.len));
    ghostty_terminal_free(terminal);
    return checkpoint;
}

QString frameText(const kodosi::TerminalFrame& frame)
{
    QString text;
    for (std::uint16_t row = 0; row < frame.rows; ++row) {
        for (std::uint16_t column = 0; column < frame.columns; ++column) {
            const auto* cell = frame.cell(column, row);
            if (cell != nullptr && cell->width != 0) {
                text += cell->grapheme.isEmpty() ? QStringLiteral(" ") : cell->grapheme;
            }
        }
        text += QLatin1Char('\n');
    }
    return text;
}

void makeTerminalReady(
    kodosi::TerminalSessionRegistry& registry,
    const kodosi::TerminalSubscription& subscription)
{
    QVERIFY(registry.installSemanticCheckpoint({
        subscription,
        1,
        24,
        80,
        checkpointFor(QByteArrayLiteral("ready"), 80, 24),
    }));
    registry.receiveConnectResult({subscription, ffiOk});
    QCoreApplication::processEvents();
}

void completeResize(
    kodosi::TerminalSessionRegistry& registry,
    const kodosi::TerminalSubscription& subscription,
    QJsonObject command)
{
    command.insert(
        QStringLiteral("type"),
        QStringLiteral("term.resizeApplied"));
    command.remove(QStringLiteral("claim"));
    registry.receiveControl({
        subscription,
        QJsonDocument(command).toJson(QJsonDocument::Compact),
    });
    QCoreApplication::processEvents();
}

} // namespace

class TerminalKernelTest final : public QObject {
    Q_OBJECT

private slots:
    void checkpointPrecedesContiguousRawData();
    void failedCheckpointPreservesPriorFrame();
    void resizeRequiresExactSequenceBoundary();
    void registryConsumesNormativeControlWireWithoutLosingU64Precision();
    void rasterizerPreservesTerminalCellBackgrounds();
    void rasterizerUsesBrandedSelectionAndPreeditPalette();
    void rasterizerHandlesWideTailBackgroundAndCursorSpan();
    void rasterizerAppliesLineHeightAndOwnedCursorPolicy();
    void kernelAppliesNativeCursorDefaultsAndScrollback();
    void terminalViewOwnsValidatedDisplaySettings();
    void terminalViewOwnsExplicitSelectionAndPreeditPalette();
    void keyEncodingUsesRestoredTerminalModes();
    void selectionUsesGhosttyTrackedStateAndFormatting();
    void terminalViewExposesNativeAccessibleTextInterface();
    void qmlConstructionEnrollsTerminalInAccessibleTree();
    void terminalViewGatesDeniedCommandsAndRevocation();
    void terminalViewRetriesRevocationBlurBeforeRefocus();
    void terminalViewClaimsFocusedResizeExactly();
    void terminalViewPreservesClaimAcrossInflightResize();
    void terminalViewDefersResizeWhileEffectivelyHidden();
    void surfaceControllerFencesSessionIncarnations();
    void surfaceControllerKeepsBindingsIndependent();
};

void TerminalKernelTest::checkpointPrecedesContiguousRawData()
{
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        7,
    };
    kodosi::GhosttyTerminalKernel kernel;
    const auto beforeCheckpoint = kernel.applyData({
        subscription,
        12,
        QByteArrayLiteral("rejected"),
    });
    QVERIFY(!beforeCheckpoint);
    QCOMPARE(
        beforeCheckpoint.error().code,
        kodosi::GhosttyTerminalKernel::Failure::Code::MissingCheckpoint);

    const auto restored = kernel.installCheckpoint({
        subscription,
        12,
        3,
        20,
        checkpointFor(QByteArrayLiteral("checkpoint"), 20, 3),
    });
    QVERIFY(restored);
    QVERIFY(frameText(**restored).contains(QStringLiteral("checkpoint")));

    const auto continued = kernel.applyData({
        subscription,
        12,
        QByteArrayLiteral(" + raw"),
    });
    QVERIFY(continued);
    QCOMPARE((*continued)->nextSequence, 13);
    QVERIFY(frameText(**continued).contains(QStringLiteral("checkpoint + raw")));

    const auto duplicate = kernel.applyData({
        subscription,
        12,
        QByteArrayLiteral("duplicate"),
    });
    QVERIFY(!duplicate);
    QCOMPARE(
        duplicate.error().code,
        kodosi::GhosttyTerminalKernel::Failure::Code::SequenceMismatch);
}

void TerminalKernelTest::rasterizerPreservesTerminalCellBackgrounds()
{
    kodosi::TerminalFrame frame;
    frame.columns = 2;
    frame.rows = 1;
    frame.background = QColor(QStringLiteral("#10131a"));
    frame.foreground = QColor(QStringLiteral("#f5f7ff"));
    frame.cells = {
        {
            .grapheme = QStringLiteral("A"),
            .foreground = frame.foreground,
            .background = frame.background,
        },
        {
            .foreground = frame.foreground,
            .background = QColor(QStringLiteral("#305080")),
        },
    };
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(14);
    const auto image = kodosi::TerminalRasterizer::render(frame, font, 1.0);

    QVERIFY(!image.isNull());
    const auto logicalSize = kodosi::TerminalRasterizer::logicalSize(frame, font);
    QCOMPARE(image.size(), logicalSize.toSize());
    const auto secondCellCenter = QPoint(
        static_cast<int>(logicalSize.width() * 0.75),
        static_cast<int>(logicalSize.height() * 0.5));
    QCOMPARE(image.pixelColor(secondCellCenter), QColor(QStringLiteral("#305080")));
}

void TerminalKernelTest::rasterizerUsesBrandedSelectionAndPreeditPalette()
{
    kodosi::TerminalFrame frame;
    frame.columns = 2;
    frame.rows = 1;
    frame.background = QColor(QStringLiteral("#0a0807"));
    frame.foreground = QColor(QStringLiteral("#f1e9e3"));
    frame.cells = {
        {
            .foreground = frame.foreground,
            .background = frame.background,
            .selected = true,
        },
        {
            .foreground = frame.foreground,
            .background = frame.background,
        },
    };
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(14);
    kodosi::TerminalRasterizer::Options options;
    options.palette.selectionBackground = QColor(QStringLiteral("#db8a62"));
    options.palette.selectionForeground = QColor(QStringLiteral("#160e0a"));
    options.palette.preeditBackground = QColor(QStringLiteral("#e69a72"));
    options.palette.preeditForeground = QColor(QStringLiteral("#160e0a"));
    options.overlay = {
        .preedit = QStringLiteral("x"),
        .column = 1,
        .row = 0,
    };

    const auto image =
        kodosi::TerminalRasterizer::render(frame, font, 1.0, options);
    QVERIFY(!image.isNull());
    QCOMPARE(
        image.pixelColor(QPoint(image.width() / 4, image.height() / 2)),
        options.palette.selectionBackground);
    QCOMPARE(
        image.pixelColor(QPoint(image.width() * 3 / 4, 1)),
        options.palette.preeditBackground);
}

void TerminalKernelTest::rasterizerHandlesWideTailBackgroundAndCursorSpan()
{
    kodosi::TerminalFrame frame;
    frame.columns = 2;
    frame.rows = 1;
    frame.background = QColor(QStringLiteral("#10131a"));
    frame.foreground = QColor(QStringLiteral("#f5f7ff"));
    frame.cursorColor = QColor(QStringLiteral("#ff405f"));
    frame.cells = {
        {
            .grapheme = QString::fromUtf8("界"),
            .foreground = frame.foreground,
            .background = QColor(QStringLiteral("#203040")),
            .width = 2,
        },
        {
            .foreground = frame.foreground,
            .background = QColor(QStringLiteral("#305080")),
            .width = 0,
        },
    };
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(14);
    const auto logicalSize = kodosi::TerminalRasterizer::logicalSize(frame, font);
    const auto withoutCursor = kodosi::TerminalRasterizer::render(frame, font, 1.0);
    const auto tailBackground = QPoint(
        static_cast<int>(logicalSize.width() * 0.75),
        static_cast<int>(logicalSize.height() - 2.0));
    QCOMPARE(
        withoutCursor.pixelColor(tailBackground),
        QColor(QStringLiteral("#305080")));

    frame.cursor = {
        .visible = true,
        .wideTail = true,
        .column = 1,
        .row = 0,
        .visualStyle = 1,
    };
    const auto withCursor = kodosi::TerminalRasterizer::render(frame, font, 1.0);
    const auto headCenter = QPoint(
        static_cast<int>(logicalSize.width() * 0.25),
        static_cast<int>(logicalSize.height() * 0.5));
    const auto tailCenter = QPoint(
        static_cast<int>(logicalSize.width() * 0.75),
        static_cast<int>(logicalSize.height() * 0.5));
    QVERIFY(withCursor.pixelColor(headCenter) != withoutCursor.pixelColor(headCenter));
    QVERIFY(withCursor.pixelColor(tailCenter) != withoutCursor.pixelColor(tailCenter));
}

void TerminalKernelTest::rasterizerAppliesLineHeightAndOwnedCursorPolicy()
{
    kodosi::TerminalFrame frame;
    frame.columns = 1;
    frame.rows = 1;
    frame.background = QColor(QStringLiteral("#101010"));
    frame.foreground = QColor(QStringLiteral("#eeeeee"));
    frame.cursorColor = QColor(QStringLiteral("#ff405f"));
    frame.cursor = {
        .visible = true,
        .column = 0,
        .row = 0,
        .visualStyle = 1,
    };
    frame.cells = {{
        .foreground = frame.foreground,
        .background = frame.background,
    }};
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(14);

    kodosi::TerminalRasterizer::Options hidden;
    hidden.cursorStyle = kodosi::TerminalCursorStyle::Bar;
    hidden.cursorPhaseVisible = false;
    const auto withoutCursor =
        kodosi::TerminalRasterizer::render(frame, font, 1.0, hidden);

    kodosi::TerminalRasterizer::Options bar;
    bar.cursorStyle = kodosi::TerminalCursorStyle::Bar;
    const auto withBar = kodosi::TerminalRasterizer::render(frame, font, 1.0, bar);
    QCOMPARE(withBar.pixelColor(QPoint(0, withBar.height() / 2)), frame.cursorColor);
    QCOMPARE(
        withoutCursor.pixelColor(QPoint(0, withoutCursor.height() / 2)),
        frame.background);

    kodosi::TerminalRasterizer::Options underline;
    underline.cursorStyle = kodosi::TerminalCursorStyle::Underline;
    underline.lineHeight = 1.5;
    const auto withUnderline =
        kodosi::TerminalRasterizer::render(frame, font, 1.0, underline);
    QVERIFY(withUnderline.height() > withBar.height());
    QCOMPARE(
        withUnderline.pixelColor(QPoint(withUnderline.width() / 2, withUnderline.height() - 1)),
        frame.cursorColor);
    QCOMPARE(
        withUnderline.pixelColor(QPoint(withUnderline.width() / 2, 0)),
        frame.background);
}

void TerminalKernelTest::kernelAppliesNativeCursorDefaultsAndScrollback()
{
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        13,
    };
    kodosi::GhosttyTerminalKernel kernel({
        .cursorStyle = kodosi::TerminalCursorStyle::Bar,
        .cursorBlink = true,
        .scrollbackLines = 12'000,
        .scrollbackBytes = kodosi::terminalScrollbackByteBudget(12'000),
    });
    QVERIFY(kernel.installCheckpoint({
        subscription,
        1,
        2,
        12,
        checkpointFor(QByteArrayLiteral("ready"), 12, 2),
    }));
    const auto limit = kernel.scrollbackLimitLines();
    QVERIFY(limit);
    QCOMPARE(*limit, std::size_t {12'000});
    const auto byteLimit = kernel.scrollbackLimitBytes();
    QVERIFY(byteLimit);
    QCOMPARE(
        *byteLimit,
        kodosi::terminalScrollbackByteBudget(12'000));

    const auto bar = kernel.applyData({
        subscription,
        1,
        QByteArrayLiteral("\x1b[0 q"),
    });
    QVERIFY(bar);
    QCOMPARE((*bar)->cursor.visualStyle, 0);
    QVERIFY((*bar)->cursor.blinking);

    QVERIFY(kernel.configure({
        .cursorStyle = kodosi::TerminalCursorStyle::Underline,
        .cursorBlink = false,
        .scrollbackLines = 25'000,
        .scrollbackBytes = kodosi::terminalScrollbackByteBudget(25'000),
    }));
    const auto updatedLimit = kernel.scrollbackLimitLines();
    QVERIFY(updatedLimit);
    QCOMPARE(*updatedLimit, std::size_t {25'000});
    const auto updatedByteLimit = kernel.scrollbackLimitBytes();
    QVERIFY(updatedByteLimit);
    QCOMPARE(
        *updatedByteLimit,
        kodosi::terminalScrollbackByteBudget(25'000));
    const auto underline = kernel.applyData({
        subscription,
        2,
        QByteArrayLiteral("\x1b[0 q"),
    });
    QVERIFY(underline);
    QCOMPARE((*underline)->cursor.visualStyle, 2);
    QVERIFY(!(*underline)->cursor.blinking);
}

void TerminalKernelTest::terminalViewOwnsValidatedDisplaySettings()
{
    kodosi::TerminalView view;
    view.setFontFamily(QStringLiteral("Iosevka"));
    view.setFontPixelSize(20);
    view.setLineHeight(1.6);
    view.setCursorStyle(2);
    view.setCursorBlink(true);
    view.setScrollbackLines(40'000);

    QCOMPARE(view.fontFamily(), QStringLiteral("Iosevka"));
    QCOMPARE(view.fontPixelSize(), 20);
    QCOMPARE(view.lineHeight(), 1.6);
    QCOMPARE(view.cursorStyle(), 2);
    QVERIFY(view.cursorBlink());
    QCOMPARE(view.scrollbackLines(), 40'000);

    auto* timer = view.findChild<QTimer*>(
        QStringLiteral("terminal.cursorBlinkTimer"));
    QVERIFY(timer != nullptr);
    QVERIFY(!timer->isActive());
}

void TerminalKernelTest::terminalViewOwnsExplicitSelectionAndPreeditPalette()
{
    kodosi::TerminalView view;
    const auto selectionBackground = QColor(QStringLiteral("#db8a62"));
    const auto selectionForeground = QColor(QStringLiteral("#160e0a"));
    const auto preeditBackground = QColor(QStringLiteral("#e69a72"));
    const auto preeditForeground = QColor(QStringLiteral("#21100e"));

    view.setSelectionBackground(selectionBackground);
    view.setSelectionForeground(selectionForeground);
    view.setPreeditBackground(preeditBackground);
    view.setPreeditForeground(preeditForeground);

    QCOMPARE(view.selectionBackground(), selectionBackground);
    QCOMPARE(view.selectionForeground(), selectionForeground);
    QCOMPARE(view.preeditBackground(), preeditBackground);
    QCOMPARE(view.preeditForeground(), preeditForeground);
}

void TerminalKernelTest::keyEncodingUsesRestoredTerminalModes()
{
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        9,
    };
    kodosi::GhosttyTerminalKernel kernel;
    QVERIFY(kernel.installCheckpoint({
        subscription,
        3,
        2,
        12,
        checkpointFor(QByteArrayLiteral("\x1b[?1h"), 12, 2),
    }));

    const auto arrow = kernel.encodeKey(
        subscription,
        {
            .key = kodosi::TerminalKey::ArrowUp,
            .action = kodosi::TerminalKeyAction::Press,
        });
    QVERIFY(arrow);
    QCOMPARE(*arrow, QByteArrayLiteral("\x1bOA"));

    const auto interrupt = kernel.encodeKey(
        subscription,
        {
            .key = kodosi::TerminalKey::Character,
            .action = kodosi::TerminalKeyAction::Press,
            .text = QStringLiteral("c"),
            .unshiftedCodepoint = 'c',
            .modifiers = {.control = true},
        });
    QVERIFY(interrupt);
    QCOMPARE(*interrupt, QByteArrayLiteral("\x03"));

    const auto legacyRelease = kernel.encodeKey(
        subscription,
        {
            .key = kodosi::TerminalKey::ArrowUp,
            .action = kodosi::TerminalKeyAction::Release,
        });
    QVERIFY(legacyRelease);
    QVERIFY(legacyRelease->isEmpty());
}

void TerminalKernelTest::selectionUsesGhosttyTrackedStateAndFormatting()
{
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        11,
    };
    kodosi::GhosttyTerminalKernel kernel;
    QVERIFY(kernel.installCheckpoint({
        subscription,
        1,
        2,
        20,
        checkpointFor(QByteArrayLiteral("hello world"), 20, 2),
    }));

    const auto selected = kernel.select(subscription, 0, 0, 4, 0);
    QVERIFY(selected);
    for (std::uint16_t column = 0; column <= 4; ++column) {
        QVERIFY((*selected)->cell(column, 0)->selected);
    }
    const auto text = kernel.selectedText(subscription);
    QVERIFY(text);
    QCOMPARE(*text, QStringLiteral("hello"));

    const auto cleared = kernel.clearSelection(subscription);
    QVERIFY(cleared);
    QVERIFY(!(*cleared)->cell(0, 0)->selected);
}

void TerminalKernelTest::terminalViewExposesNativeAccessibleTextInterface()
{
    kodosi::installTerminalAccessibility();
    kodosi::TerminalView view;
    auto* accessible = QAccessible::queryAccessibleInterface(&view);

    QVERIFY(accessible != nullptr);
    QCOMPARE(accessible->role(), QAccessible::Terminal);
    QVERIFY(accessible->textInterface() != nullptr);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Terminal session"));
    QVERIFY(accessible->state().disabled);
    QVERIFY(accessible->state().readOnly);
    QVERIFY(!accessible->state().editable);

    view.setTerminalCapabilities(true, true, true, true);
    QVERIFY(!accessible->state().readOnly);
    QVERIFY(accessible->state().editable);
}

void TerminalKernelTest::qmlConstructionEnrollsTerminalInAccessibleTree()
{
    qmlRegisterType<kodosi::TerminalView>(
        "Kodosi.Terminal.Test",
        1,
        0,
        "TerminalView");
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        QByteArrayLiteral(
            "import QtQuick\nimport Kodosi.Terminal.Test\nTerminalView {}"),
        {});
    std::unique_ptr<QObject> view(component.create());

    QVERIFY2(view != nullptr, qPrintable(component.errorString()));
    QObject* attached = nullptr;
    for (auto* child : view->children()) {
        if (QByteArrayView(child->metaObject()->className())
            .contains(QByteArrayView("QQuickAccessibleAttached"))) {
            attached = child;
            break;
        }
    }
    QVERIFY(attached != nullptr);
    QCOMPARE(
        attached->property(QByteArrayLiteral("role")).toInt(),
        static_cast<int>(QAccessible::Terminal));
    QCOMPARE(
        attached->property(QByteArrayLiteral("name")).toString(),
        QStringLiteral("Terminal session"));
}

void TerminalKernelTest::terminalViewGatesDeniedCommandsAndRevocation()
{
    kodosi::TerminalSessionRegistry registry;
    FakeTerminalDispatcher dispatcher;
    kodosi::TerminalView view;
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        7,
    };
    view.setWidth(800);
    view.setHeight(400);
    view.setTerminalCapabilities(false, false, false, false);
    QVERIFY(view.attach(
        registry,
        dispatcher,
        subscription,
        QStringLiteral("incarnation")));
    makeTerminalReady(registry, subscription);

    QKeyEvent deniedPress(
        QEvent::KeyPress,
        Qt::Key_A,
        Qt::NoModifier,
        QStringLiteral("a"));
    QCoreApplication::sendEvent(&view, &deniedPress);
    QKeyEvent deniedRelease(
        QEvent::KeyRelease,
        Qt::Key_A,
        Qt::NoModifier,
        QStringLiteral("a"));
    QCoreApplication::sendEvent(&view, &deniedRelease);
    QInputMethodEvent deniedIme;
    deniedIme.setCommitString(QStringLiteral("denied"));
    QCoreApplication::sendEvent(&view, &deniedIme);
    QFocusEvent deniedFocus(QEvent::FocusIn);
    QCoreApplication::sendEvent(&view, &deniedFocus);
    QCoreApplication::processEvents();

    QCOMPARE(dispatcher.inputCommands.size(), 0);
    QCOMPARE(dispatcher.terminalCommands.size(), 0);

    view.setTerminalCapabilities(true, true, true, false);
    QCOMPARE(dispatcher.terminalCommands.size(), 1);
    QCOMPARE(
        dispatcher.terminalCommands.constLast()
            .value(QStringLiteral("type"))
            .toString(),
        QStringLiteral("session.focus"));
    dispatcher.busyInputCommands = 1;
    QKeyEvent acceptedPress(
        QEvent::KeyPress,
        Qt::Key_A,
        Qt::NoModifier,
        QStringLiteral("a"));
    QCoreApplication::sendEvent(&view, &acceptedPress);
    QFocusEvent acceptedFocus(QEvent::FocusIn);
    QCoreApplication::sendEvent(&view, &acceptedFocus);
    QVERIFY(!dispatcher.inputCommands.isEmpty());
    QCOMPARE(
        dispatcher.terminalCommands.constLast()
            .value(QStringLiteral("type"))
            .toString(),
        QStringLiteral("session.focus"));

    const auto inputAttempts = dispatcher.inputCommands.size();
    view.setTerminalCapabilities(false, false, false, false);
    QCOMPARE(
        dispatcher.terminalCommands.constLast()
            .value(QStringLiteral("type"))
            .toString(),
        QStringLiteral("session.blur"));
    QVERIFY(view.readOnly());
    QVERIFY(!view.canRetainFocus());
    view.setTerminalCapabilities(true, true, true, false);
    QCOMPARE(
        dispatcher.terminalCommands.constLast()
            .value(QStringLiteral("type"))
            .toString(),
        QStringLiteral("session.focus"));
    QCOMPARE(dispatcher.terminalCommands.size(), 3);
    QTest::qWait(40);
    QCOMPARE(dispatcher.inputCommands.size(), inputAttempts);
    QVERIFY(!view.readOnly());
    QVERIFY(view.canRetainFocus());
}

void TerminalKernelTest::terminalViewRetriesRevocationBlurBeforeRefocus()
{
    kodosi::TerminalSessionRegistry registry;
    FakeTerminalDispatcher dispatcher;
    kodosi::TerminalView view;
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        8,
    };
    view.setWidth(800);
    view.setHeight(400);
    view.setTerminalCapabilities(true, true, true, false);
    QVERIFY(view.attach(
        registry,
        dispatcher,
        subscription,
        QStringLiteral("incarnation")));
    makeTerminalReady(registry, subscription);

    QFocusEvent focus(QEvent::FocusIn);
    QCoreApplication::sendEvent(&view, &focus);
    QCOMPARE(dispatcher.terminalCommands.size(), 1);
    dispatcher.busyTerminalCommands = 1;
    view.setTerminalCapabilities(false, false, false, false);
    view.setTerminalCapabilities(true, true, true, false);
    QCOMPARE(dispatcher.terminalCommands.size(), 2);

    QTRY_COMPARE_WITH_TIMEOUT(
        dispatcher.terminalCommands.size(),
        4,
        250);
    const QStringList types {
        dispatcher.terminalCommands.at(0)
            .value(QStringLiteral("type")).toString(),
        dispatcher.terminalCommands.at(1)
            .value(QStringLiteral("type")).toString(),
        dispatcher.terminalCommands.at(2)
            .value(QStringLiteral("type")).toString(),
        dispatcher.terminalCommands.at(3)
            .value(QStringLiteral("type")).toString(),
    };
    QCOMPARE(
        types,
        QStringList({
            QStringLiteral("session.focus"),
            QStringLiteral("session.blur"),
            QStringLiteral("session.blur"),
            QStringLiteral("session.focus"),
        }));
}

void TerminalKernelTest::terminalViewClaimsFocusedResizeExactly()
{
    kodosi::TerminalSessionRegistry registry;
    FakeTerminalDispatcher dispatcher;
    dispatcher.busyTerminalCommands = 1;
    kodosi::TerminalView view;
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        9,
    };
    view.setWidth(800);
    view.setHeight(400);
    view.setTerminalCapabilities(false, false, false, true);
    view.setFocusedSizeAuthority(true);
    QVERIFY(view.attach(
        registry,
        dispatcher,
        subscription,
        QStringLiteral("incarnation")));
    makeTerminalReady(registry, subscription);
    QTRY_VERIFY_WITH_TIMEOUT(
        dispatcher.terminalCommands.size() >= 2,
        250);
    for (const auto& command : std::as_const(dispatcher.terminalCommands)) {
        QCOMPARE(
            command.value(QStringLiteral("type")).toString(),
            QStringLiteral("session.resize"));
        QVERIFY(command.value(QStringLiteral("claim")).toBool());
        QCOMPARE(
            command.value(QStringLiteral("sessionId")).toString(),
            QStringLiteral("session"));
        QCOMPARE(
            command.value(QStringLiteral("subscriptionId")).toString(),
            QStringLiteral("subscription"));
        QCOMPARE(
            command.value(QStringLiteral("subscriptionGeneration"))
                .toInteger(),
            9);
    }

    kodosi::TerminalView denied;
    denied.setWidth(800);
    denied.setHeight(400);
    denied.setTerminalCapabilities(false, false, false, false);
    denied.setFocusedSizeAuthority(true);
    const kodosi::TerminalSubscription deniedSubscription {
        QStringLiteral("denied"),
        QStringLiteral("denied-subscription"),
        10,
    };
    const auto beforeDenied = dispatcher.terminalCommands.size();
    QVERIFY(denied.attach(
        registry,
        dispatcher,
        deniedSubscription,
        QStringLiteral("denied-incarnation")));
    makeTerminalReady(registry, deniedSubscription);
    QTest::qWait(20);
    QCOMPARE(dispatcher.terminalCommands.size(), beforeDenied);
    denied.setTerminalCapabilities(false, false, false, true);
    QTRY_COMPARE_WITH_TIMEOUT(
        dispatcher.terminalCommands.size(),
        beforeDenied + 1,
        250);
    QVERIFY(dispatcher.terminalCommands.constLast()
                .value(QStringLiteral("claim"))
                .toBool());

    FakeTerminalDispatcher gridDispatcher;
    kodosi::TerminalView grid;
    grid.setWidth(800);
    grid.setHeight(400);
    grid.setTerminalCapabilities(false, false, false, false);
    const kodosi::TerminalSubscription gridSubscription {
        QStringLiteral("grid"),
        QStringLiteral("grid-subscription"),
        11,
    };
    QVERIFY(grid.attach(
        registry,
        gridDispatcher,
        gridSubscription,
        QStringLiteral("grid-incarnation")));
    makeTerminalReady(registry, gridSubscription);
    QTest::qWait(20);
    QCOMPARE(gridDispatcher.terminalCommands.size(), 0);
    grid.setTerminalCapabilities(false, false, false, true);
    QTRY_COMPARE_WITH_TIMEOUT(
        gridDispatcher.terminalCommands.size(),
        1,
        250);
    QVERIFY(!gridDispatcher.terminalCommands.constFirst()
                 .value(QStringLiteral("claim"))
                 .toBool());
}

void TerminalKernelTest::terminalViewPreservesClaimAcrossInflightResize()
{
    kodosi::TerminalSessionRegistry registry;
    FakeTerminalDispatcher dispatcher;
    kodosi::TerminalView view;
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("claim-race"),
        QStringLiteral("claim-race-subscription"),
        12,
    };
    view.setWidth(800);
    view.setHeight(400);
    view.setTerminalCapabilities(false, false, false, true);
    QVERIFY(view.attach(
        registry,
        dispatcher,
        subscription,
        QStringLiteral("claim-race-incarnation")));
    makeTerminalReady(registry, subscription);
    QTRY_COMPARE_WITH_TIMEOUT(
        dispatcher.terminalCommands.size(),
        1,
        250);
    QVERIFY(!dispatcher.terminalCommands.constFirst()
                 .value(QStringLiteral("claim"))
                 .toBool());

    view.setFocusedSizeAuthority(true);
    QCoreApplication::processEvents();
    QCOMPARE(dispatcher.terminalCommands.size(), 1);
    completeResize(
        registry,
        subscription,
        dispatcher.terminalCommands.constFirst());
    QTRY_COMPARE_WITH_TIMEOUT(
        dispatcher.terminalCommands.size(),
        2,
        250);
    QVERIFY(dispatcher.terminalCommands.constLast()
                .value(QStringLiteral("claim"))
                .toBool());
}

void TerminalKernelTest::terminalViewDefersResizeWhileEffectivelyHidden()
{
    kodosi::TerminalSessionRegistry registry;
    FakeTerminalDispatcher dispatcher;
    QQuickWindow window;
    window.setGeometry(0, 0, 900, 500);
    window.show();
    QQuickItem container(window.contentItem());
    kodosi::TerminalView view(&container);
    QCoreApplication::processEvents();
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("hidden"),
        QStringLiteral("hidden-subscription"),
        12,
    };
    view.setWidth(800);
    view.setHeight(400);
    view.setTerminalCapabilities(false, false, false, true);
    QVERIFY(view.attach(
        registry,
        dispatcher,
        subscription,
        QStringLiteral("hidden-incarnation")));
    makeTerminalReady(registry, subscription);
    QTRY_COMPARE_WITH_TIMEOUT(
        dispatcher.terminalCommands.size(),
        1,
        250);

    container.setVisible(false);
    QVERIFY(!view.isVisible());
    view.setWidth(640);
    QTest::qWait(20);
    QCOMPARE(dispatcher.terminalCommands.size(), 1);

    container.setVisible(true);
    QVERIFY(view.isVisible());
    QTRY_COMPARE_WITH_TIMEOUT(
        dispatcher.terminalCommands.size(),
        2,
        250);
    QVERIFY(!dispatcher.terminalCommands.constLast()
                 .value(QStringLiteral("claim"))
                 .toBool());
}

void TerminalKernelTest::surfaceControllerFencesSessionIncarnations()
{
    kodosi::TerminalSessionRegistry registry;
    kodosi::RuntimeBridge runtime(registry);
    kodosi::SessionCatalogModel sessions;
    kodosi::TerminalSurfaceController controller(registry, runtime, sessions);
    kodosi::TerminalView view;
    QSignalSpy rejected(
        &controller,
        &kodosi::TerminalSurfaceController::attachmentRejected);

    sessions.ingestAuthEvent(
        QByteArrayLiteral(
            R"({"type":"auth.ready","userId":"user","accountEpoch":1})"));
    sessions.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"session","incarnationId":"inc-1","name":"One","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}]})"));

    QVERIFY(controller.bind(
        &view,
        QStringLiteral("session")));
    QVERIFY(controller.retry(
        &view,
        QStringLiteral("session")));
    QVERIFY(!controller.bind(
        &view,
        QString {}));
    QCOMPARE(rejected.count(), 1);

    sessions.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.removed","sessionId":"session"})"));
    QCOMPARE(rejected.count(), 2);
    QVERIFY(!view.terminalReady());
}

void TerminalKernelTest::surfaceControllerKeepsBindingsIndependent()
{
    kodosi::TerminalSessionRegistry registry;
    FakeRuntimeBridge runtime(registry);
    kodosi::SessionCatalogModel sessions;
    kodosi::TerminalSurfaceController controller(registry, runtime, sessions);
    kodosi::TerminalView firstView;
    kodosi::TerminalView secondView;
    QSignalSpy rejected(
        &controller,
        &kodosi::TerminalSurfaceController::attachmentRejected);

    sessions.ingestAuthEvent(QByteArrayLiteral(
        R"({"type":"auth.ready","userId":"user","accountEpoch":1})"));
    sessions.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"first","incarnationId":"inc-1","name":"First","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"},{"kind":"local","id":"second","incarnationId":"inc-2","name":"Second","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}]})"));

    QVERIFY(controller.bind(&firstView, QStringLiteral("first")));
    QVERIFY(controller.bind(&secondView, QStringLiteral("second")));
    QCOMPARE(runtime.connectCount, 2);
    QVERIFY(firstView.canSendInput());
    QVERIFY(secondView.canSendInput());

    sessions.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.upsert","session":{"kind":"remote","id":"second","incarnationId":"inc-2","name":"Second","project":"/repo","mode":"normal","status":"active","scope":"room","access":"view","ownerUserId":"owner","permissions":1,"connectionState":"connected","accessState":"ready"}})"));
    QVERIFY(firstView.canSendInput());
    QVERIFY(secondView.readOnly());
    QCOMPARE(runtime.connectCount, 2);

    sessions.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.upsert","session":{"kind":"remote","id":"second","incarnationId":"inc-2","name":"Second","project":"/repo","mode":"normal","status":"active","scope":"room","access":"inject","permissions":15,"connectionState":"connected","accessState":"ready"}})"));
    QVERIFY(firstView.canSendInput());
    QVERIFY(secondView.canSendInput());
    QVERIFY(secondView.canResize());
    QCOMPARE(runtime.connectCount, 2);
    controller.detach(&firstView);
    QCOMPARE(runtime.disconnectCount, 1);

    sessions.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.removed","sessionId":"first"})"));
    QCOMPARE(rejected.count(), 0);

    sessions.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.upsert","session":{"kind":"local","id":"second","incarnationId":"inc-3","name":"Second","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}})"));
    QCOMPARE(rejected.count(), 0);
    QCOMPARE(runtime.connectCount, 3);

    sessions.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.removed","sessionId":"second"})"));
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(
        rejected.constFirst().constFirst().toString(),
        QStringLiteral("second"));
    QVERIFY(!firstView.terminalReady());
    QVERIFY(!secondView.terminalReady());
}

void TerminalKernelTest::registryConsumesNormativeControlWireWithoutLosingU64Precision()
{
    constexpr std::uint64_t sequence = 9'007'199'254'740'993ULL;
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        4,
    };
    kodosi::GhosttyTerminalKernel::Frame currentFrame;
    std::optional<kodosi::TerminalFocusOutcome> focusOutcome;
    std::optional<kodosi::TerminalResizeOutcome> resizeOutcome;
    std::optional<kodosi::TerminalNotification> notification;
    std::optional<kodosi::GhosttyTerminalKernel::Failure> failure;
    bool closed = false;
    kodosi::TerminalSessionRegistry registry;
    QVERIFY(registry.registerSession(
        subscription,
        {
            .frameChanged = [&](auto frame) { currentFrame = std::move(frame); },
            .failed = [&](auto value) { failure = std::move(value); },
            .focusCompleted = [&](auto outcome) {
                focusOutcome = std::move(outcome);
            },
            .resizeCompleted = [&](auto outcome) {
                resizeOutcome = std::move(outcome);
            },
            .notificationRequested = [&](auto value) {
                notification = std::move(value);
            },
            .closed = [&] { closed = true; },
        }));
    QVERIFY(registry.installSemanticCheckpoint({
        subscription,
        sequence,
        2,
        12,
        checkpointFor(QByteArrayLiteral("control"), 12, 2),
    }));

    registry.receiveControl({
        subscription,
        QByteArrayLiteral(
            R"({"type":"Resize","rows":4,"cols":24,"at_sequence":9007199254740993})"),
    });
    QVERIFY(currentFrame);
    QCOMPARE(currentFrame->rows, 4);
    QCOMPARE(currentFrame->columns, 24);
    QCOMPARE(currentFrame->nextSequence, sequence);

    registry.receiveControl({
        subscription,
        QByteArrayLiteral(
            R"({"type":"term.focusApplied","sessionId":"session","requestId":"focus-request","runtimeIncarnationId":"incarnation"})"),
    });
    QVERIFY(focusOutcome);
    QVERIFY(focusOutcome->applied);
    QCOMPARE(focusOutcome->requestId, QStringLiteral("focus-request"));

    registry.receiveControl({
        subscription,
        QByteArrayLiteral(
            R"({"type":"term.resizeApplied","sessionId":"session","requestId":"resize-request","expectedRuntimeIncarnationId":"incarnation","subscriptionId":"subscription","subscriptionGeneration":4,"surfaceGeneration":9007199254740993,"cols":24,"rows":4,"widthPixels":216,"heightPixels":72,"cellWidthPixels":9,"cellHeightPixels":18})"),
    });
    QVERIFY(resizeOutcome);
    QVERIFY(resizeOutcome->applied);
    QCOMPARE(resizeOutcome->surfaceGeneration, sequence);
    QCOMPARE(resizeOutcome->widthPixels, 216U);

    registry.receiveControl({
        subscription,
        QByteArrayLiteral(
            R"({"type":"term.notification","sessionId":"session","title":"Build complete","body":"All checks passed"})"),
    });
    QVERIFY(notification);
    QCOMPARE(notification->title, QStringLiteral("Build complete"));
    QCOMPARE(notification->body, QStringLiteral("All checks passed"));

    registry.receiveControl({
        subscription,
        QByteArrayLiteral(
            R"({"type":"term.notification","sessionId":"replacement","body":"stale"})"),
    });
    QVERIFY(failure);
    QVERIFY(failure->message.contains(QStringLiteral("notification")));
    QCOMPARE(notification->body, QStringLiteral("All checks passed"));

    registry.receiveControl({
        subscription,
        QByteArrayLiteral(
            R"({"type":"Closed","reason":"session ended","finalSequence":9007199254740993})"),
    });
    QVERIFY(closed);
}

void TerminalKernelTest::failedCheckpointPreservesPriorFrame()
{
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        1,
    };
    kodosi::GhosttyTerminalKernel kernel;
    const auto accepted = kernel.installCheckpoint({
        subscription,
        4,
        2,
        12,
        checkpointFor(QByteArrayLiteral("safe"), 12, 2),
    });
    QVERIFY(accepted);
    const auto prior = kernel.frame();

    auto invalid = QByteArrayLiteral("{\"schemaVersion\":2}");
    const auto rejected = kernel.installCheckpoint({
        subscription,
        99,
        2,
        12,
        invalid,
    });
    QVERIFY(!rejected);
    QCOMPARE(kernel.frame(), prior);
    QCOMPARE(kernel.frame()->nextSequence, 4);
}

void TerminalKernelTest::resizeRequiresExactSequenceBoundary()
{
    const kodosi::TerminalSubscription subscription {
        QStringLiteral("session"),
        QStringLiteral("subscription"),
        2,
    };
    kodosi::GhosttyTerminalKernel kernel;
    QVERIFY(kernel.installCheckpoint({
        subscription,
        5,
        2,
        12,
        checkpointFor(QByteArrayLiteral("resize"), 12, 2),
    }));

    const auto future = kernel.applyResize(subscription, 6, 4, 24);
    QVERIFY(!future);
    QCOMPARE(
        future.error().code,
        kodosi::GhosttyTerminalKernel::Failure::Code::SequenceMismatch);

    const auto resized = kernel.applyResize(subscription, 5, 4, 24);
    QVERIFY(resized);
    QCOMPARE((*resized)->rows, 4);
    QCOMPARE((*resized)->columns, 24);
    QCOMPARE((*resized)->nextSequence, 5);
}

QTEST_MAIN(TerminalKernelTest)

#include "tst_terminal_kernel.moc"
