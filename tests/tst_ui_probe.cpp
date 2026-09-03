#include "ui_probe/AtSpiProbe.hpp"
#include "ui_probe/InputAdapter.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTest>

#include <signal.h>
#include <unistd.h>

class UiProbeTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void handlesAreCanonical();
    void commandsOperateFakeApplication();
    void inspectionToleratesMissingOptionalValueText();
    void inspectionReadsOptionalValueText();
    void rawInputUsesAtSpiController();
    void portalSidecarSerializesInput();
    void portalSidecarCancelsWithLauncher();
    void inputValidationIsBounded();
    void protocolFramesAndPeerIdentityAreBounded();

private:
    QProcess m_fake;
    QString m_runtimeDirectory;
    QString m_inputLog;
    QString m_portalDenyFile;
    QString m_portalDelayFile;
    QString m_portalMethodDelayFile;
    QString m_portalCloseEarlyFile;
    QString m_portalNoStreamFile;
    QString m_atspiMissingMethodFile;
    QString m_atspiOverflowBoundsFile;

    [[nodiscard]] QJsonObject run(
        const QStringList& arguments,
        int expectedExitCode = 0);
};

void UiProbeTest::initTestCase()
{
    m_runtimeDirectory =
        QDir::cleanPath(
            QFileInfo(QStringLiteral(KODOSI_UI_PROBE_PATH))
                .absoluteDir()
                .absoluteFilePath(
                    QStringLiteral("../../../.r-%1")
                        .arg(QCoreApplication::applicationPid())));
    QDir(m_runtimeDirectory).removeRecursively();
    QVERIFY(QDir().mkpath(m_runtimeDirectory));
    QVERIFY(QFile::setPermissions(
        m_runtimeDirectory,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ExeOwner));
    m_inputLog =
        QDir::current().absoluteFilePath(
            QStringLiteral("ui-probe-input-%1.jsonl")
                .arg(QCoreApplication::applicationPid()));
    QFile::remove(m_inputLog);
    m_portalDenyFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("ui-probe-portal-deny-%1")
                .arg(QCoreApplication::applicationPid()));
    m_portalDelayFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("ui-probe-portal-delay-%1")
                .arg(QCoreApplication::applicationPid()));
    m_portalMethodDelayFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("ui-probe-portal-method-delay-%1")
                .arg(QCoreApplication::applicationPid()));
    m_portalCloseEarlyFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("ui-probe-portal-close-early-%1")
                .arg(QCoreApplication::applicationPid()));
    m_portalNoStreamFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("ui-probe-portal-no-stream-%1")
                .arg(QCoreApplication::applicationPid()));
    m_atspiMissingMethodFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("ui-probe-atspi-missing-method-%1")
                .arg(QCoreApplication::applicationPid()));
    m_atspiOverflowBoundsFile =
        QDir::current().absoluteFilePath(
            QStringLiteral("ui-probe-atspi-overflow-bounds-%1")
                .arg(QCoreApplication::applicationPid()));
    for (const auto& path :
         {m_portalDenyFile,
          m_portalDelayFile,
          m_portalMethodDelayFile,
          m_portalCloseEarlyFile,
          m_portalNoStreamFile,
          m_atspiMissingMethodFile,
          m_atspiOverflowBoundsFile}) {
        QFile::remove(path);
    }
    qputenv("XDG_RUNTIME_DIR", m_runtimeDirectory.toUtf8());
    qputenv("KODOSI_FAKE_INPUT_LOG", m_inputLog.toUtf8());
    qputenv(
        "KODOSI_FAKE_PORTAL_DENY_FILE",
        m_portalDenyFile.toUtf8());
    qputenv(
        "KODOSI_FAKE_PORTAL_DELAY_FILE",
        m_portalDelayFile.toUtf8());
    qputenv(
        "KODOSI_FAKE_PORTAL_METHOD_DELAY_FILE",
        m_portalMethodDelayFile.toUtf8());
    qputenv(
        "KODOSI_FAKE_PORTAL_CLOSE_EARLY_FILE",
        m_portalCloseEarlyFile.toUtf8());
    qputenv(
        "KODOSI_FAKE_PORTAL_NO_STREAM_FILE",
        m_portalNoStreamFile.toUtf8());
    qputenv(
        "KODOSI_FAKE_ATSPI_MISSING_METHOD_FILE",
        m_atspiMissingMethodFile.toUtf8());
    qputenv(
        "KODOSI_FAKE_ATSPI_OVERFLOW_BOUNDS_FILE",
        m_atspiOverflowBoundsFile.toUtf8());
    m_fake.setProgram(QStringLiteral(KODOSI_FAKE_ATSPI_PATH));
    m_fake.start();
    QVERIFY2(m_fake.waitForStarted(), qPrintable(m_fake.errorString()));
    QVERIFY2(m_fake.waitForReadyRead(), qPrintable(m_fake.errorString()));
    QCOMPARE(m_fake.readLine(), QByteArray("READY\n"));
}

void UiProbeTest::cleanupTestCase()
{
    static_cast<void>(run(
        {
            QStringLiteral("input-stop"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("1000"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Environment)));
    m_fake.terminate();
    if (!m_fake.waitForFinished(3000)) {
        m_fake.kill();
        QVERIFY(m_fake.waitForFinished(3000));
    }
    QFile::remove(m_inputLog);
    for (const auto& path :
         {m_portalDenyFile,
          m_portalDelayFile,
          m_portalMethodDelayFile,
          m_portalCloseEarlyFile,
          m_portalNoStreamFile,
          m_atspiMissingMethodFile,
          m_atspiOverflowBoundsFile}) {
        QFile::remove(path);
    }
    QDir(m_runtimeDirectory).removeRecursively();
}

QJsonObject UiProbeTest::run(
    const QStringList& arguments,
    const int expectedExitCode)
{
    QProcess process;
    process.setProgram(QStringLiteral(KODOSI_UI_PROBE_PATH));
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted()) {
        QTest::qFail(
            qPrintable(process.errorString()),
            __FILE__,
            __LINE__);
        return {};
    }
    if (!process.waitForFinished(10000)) {
        QTest::qFail(
            qPrintable(process.errorString()),
            __FILE__,
            __LINE__);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != expectedExitCode) {
        const auto message =
            QStringLiteral("exit=%1 stderr=%2")
                .arg(process.exitCode())
                .arg(QString::fromUtf8(process.readAllStandardError()));
        QTest::qFail(qPrintable(message), __FILE__, __LINE__);
        return {};
    }
    QJsonParseError parseError;
    const auto document =
        QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        QTest::qFail(
            qPrintable(parseError.errorString()),
            __FILE__,
            __LINE__);
        return {};
    }
    return document.object();
}

void UiProbeTest::handlesAreCanonical()
{
    const auto handle = kodosi::ui_probe::AtSpiProbe::encodeHandle(
        QStringLiteral(":1.42"),
        QStringLiteral("/org/example/object"));
    QVERIFY(!handle.isEmpty());
    QString bus;
    QString path;
    QVERIFY(kodosi::ui_probe::AtSpiProbe::decodeHandle(
        handle,
        &bus,
        &path));
    QCOMPARE(bus, QStringLiteral(":1.42"));
    QCOMPARE(path, QStringLiteral("/org/example/object"));
    QVERIFY(!kodosi::ui_probe::AtSpiProbe::decodeHandle(
        handle + QStringLiteral("="),
        nullptr,
        nullptr));
    QVERIFY(!kodosi::ui_probe::AtSpiProbe::decodeHandle(
        QStringLiteral("atspi1_invalid!"),
        nullptr,
        nullptr));
}

void UiProbeTest::commandsOperateFakeApplication()
{
    const auto apps = run({QStringLiteral("apps")});
    QCOMPARE(apps.value(QStringLiteral("count")).toInt(), 2);
    QJsonObject application;
    qint64 secondProcessId = 0;
    for (const auto& value :
         apps.value(QStringLiteral("applications")).toArray()) {
        const auto candidate = value.toObject();
        const auto processId =
            candidate.value(QStringLiteral("processId")).toInteger();
        if (processId == m_fake.processId()) {
            application = candidate;
        } else {
            secondProcessId = processId;
        }


    }
    QVERIFY(!application.isEmpty());
    QVERIFY(secondProcessId > 0);
    QVERIFY(secondProcessId != m_fake.processId());
    QCOMPARE(
        application.value(QStringLiteral("processId")).toInteger(),
        m_fake.processId());
    const auto applicationHandle =
        application.value(QStringLiteral("handle")).toString();
    QVERIFY(!applicationHandle.isEmpty());
    const auto ambiguousApplication = run(
        {
            QStringLiteral("find"),
            QStringLiteral("--app"),
            QStringLiteral("Kodosi Fake"),
            QStringLiteral("--id"),
            QStringLiteral("header.settings"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Ambiguous));
    QVERIFY(!ambiguousApplication.value(QStringLiteral("ok")).toBool());
    const auto tree = run({
        QStringLiteral("tree"),
        QStringLiteral("--pid"),
        QString::number(m_fake.processId()),
        QStringLiteral("--depth"),
        QStringLiteral("4"),
    });
    QVERIFY(tree.value(QStringLiteral("nodeCount")).toInt() >= 3);
    QCOMPARE(
        tree.value(QStringLiteral("app"))
            .toObject()
            .value(QStringLiteral("handle"))
            .toString(),
        applicationHandle);

    const auto settings = run({
        QStringLiteral("find"),
        QStringLiteral("--app"),
        applicationHandle,
        QStringLiteral("--id"),
        QStringLiteral("header.settings"),
    });
    QCOMPARE(settings.value(QStringLiteral("count")).toInt(), 1);
    const auto settingsHandle =
        settings.value(QStringLiteral("matches"))
            .toArray()
            .first()
            .toObject()
            .value(QStringLiteral("handle"))
            .toString();
    QVERIFY(!settingsHandle.isEmpty());
    const auto inspected =
        run({QStringLiteral("inspect"), settingsHandle});
    QCOMPARE(
        inspected.value(QStringLiteral("element"))
            .toObject()
            .value(QStringLiteral("id"))
            .toString(),
        QStringLiteral("header.settings"));

    const auto clicked =
        run({QStringLiteral("click"), settingsHandle});
    QVERIFY(clicked.value(QStringLiteral("ok")).toBool());

    const auto waited = run({
        QStringLiteral("wait"),
        QStringLiteral("--app"),
        applicationHandle,
        QStringLiteral("id=panel.settings"),
        QStringLiteral("--state"),
        QStringLiteral("showing"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("1000"),
    });
    QVERIFY(waited.value(QStringLiteral("ok")).toBool());
    const auto selectorFirstWait = run({
        QStringLiteral("wait"),
        QStringLiteral("--app"),
        applicationHandle,
        QStringLiteral("id=panel.settings"),
        QStringLiteral("--text"),
        QStringLiteral("Desktop settings"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("1000"),
    });
    QVERIFY(selectorFirstWait.value(QStringLiteral("ok")).toBool());

    const auto field = run({
        QStringLiteral("find"),
        QStringLiteral("--app"),
        applicationHandle,
        QStringLiteral("--id"),
        QStringLiteral("panel.settings.terminal.fontFamily"),
    });
    const auto fieldHandle =
        field.value(QStringLiteral("matches"))
            .toArray()
            .first()
            .toObject()
            .value(QStringLiteral("handle"))
            .toString();
    const auto incompleteContains = run(
        {
            QStringLiteral("find"),
            QStringLiteral("--app"),
            applicationHandle,
            QStringLiteral("--contains"),
            QStringLiteral("needle-at-end"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::LimitExceeded));
    QVERIFY(!incompleteContains.value(QStringLiteral("ok")).toBool());
    const auto indeterminateCandidateText = run(
        {
            QStringLiteral("wait"),
            QStringLiteral("--app"),
            applicationHandle,
            QStringLiteral("id=panel.settings.terminal.fontFamily"),
            QStringLiteral("--text"),
            QStringLiteral("needle-at-end"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("1000"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::LimitExceeded));
    QVERIFY(!indeterminateCandidateText.value(QStringLiteral("ok")).toBool());
    const auto stateRejectsTruncatedCandidate = run(
        {
            QStringLiteral("wait"),
            QStringLiteral("--app"),
            applicationHandle,
            QStringLiteral("id=panel.settings.terminal.fontFamily"),
            QStringLiteral("--state"),
            QStringLiteral("active"),
            QStringLiteral("--text"),
            QStringLiteral("needle-at-end"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("20"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Timeout));
    QVERIFY(!stateRejectsTruncatedCandidate.value(QStringLiteral("ok")).toBool());
    const auto focused = run({QStringLiteral("focus"), fieldHandle});
    QVERIFY(focused.value(QStringLiteral("ok")).toBool());
    const auto changed = run({
        QStringLiteral("set-text"),
        fieldHandle,
        QStringLiteral("--text"),
        QStringLiteral("Kodosi Test Mono"),
    });
    QCOMPARE(
        changed.value(QStringLiteral("readback")).toString(),
        QStringLiteral("Kodosi Test Mono"));

    const auto invalid = run(
        {QStringLiteral("inspect"), QStringLiteral("not-a-handle")},
        static_cast<int>(kodosi::ui_probe::ExitCode::InvalidHandle));
    QVERIFY(!invalid.value(QStringLiteral("ok")).toBool());

    QElapsedTimer deadline;
    deadline.start();
    const auto timedOut = run(
        {
            QStringLiteral("wait"),
            QStringLiteral("--pid"),
            QString::number(m_fake.processId()),
            QStringLiteral("id=missing.element"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("20"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Timeout));
    QVERIFY(!timedOut.value(QStringLiteral("ok")).toBool());
    QVERIFY(deadline.elapsed() < 1000);

    const auto wrongPid = run(
        {
            QStringLiteral("find"),
            QStringLiteral("--app"),
            applicationHandle,
            QStringLiteral("--pid"),
            QString::number(m_fake.processId() + 1),
            QStringLiteral("--id"),
            QStringLiteral("header.settings"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::NotFound));
    QVERIFY(!wrongPid.value(QStringLiteral("ok")).toBool());

    const auto oversized = run(
        {
            QStringLiteral("set-text"),
            fieldHandle,
            QStringLiteral("--text"),
            QString(30000, QChar(0x2603)),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::LimitExceeded));
    QVERIFY(!oversized.value(QStringLiteral("ok")).toBool());
}

void UiProbeTest::inspectionToleratesMissingOptionalValueText()
{
    const auto settings = run({
        QStringLiteral("find"),
        QStringLiteral("--pid"),
        QString::number(m_fake.processId()),
        QStringLiteral("--id"),
        QStringLiteral("header.settings"),
    });
    static_cast<void>(run({
        QStringLiteral("click"),
        settings.value(QStringLiteral("matches"))
            .toArray()
            .first()
            .toObject()
            .value(QStringLiteral("handle"))
            .toString(),
    }));
    const auto field = run({
        QStringLiteral("find"),
        QStringLiteral("--pid"),
        QString::number(m_fake.processId()),
        QStringLiteral("--id"),
        QStringLiteral("panel.settings.terminal.lineHeight"),
    });
    const auto inspected = run({
        QStringLiteral("inspect"),
        field.value(QStringLiteral("matches"))
            .toArray()
            .first()
            .toObject()
            .value(QStringLiteral("handle"))
            .toString(),
    });
    const auto value =
        inspected.value(QStringLiteral("element"))
            .toObject()
            .value(QStringLiteral("value"))
            .toObject();
    QCOMPARE(value.value(QStringLiteral("current")).toDouble(), 1.1);
    QCOMPARE(value.value(QStringLiteral("minimum")).toDouble(), 0.8);
    QCOMPARE(value.value(QStringLiteral("maximum")).toDouble(), 2.0);
    QCOMPARE(value.value(QStringLiteral("increment")).toDouble(), 0.1);
    QVERIFY(!value.value(QStringLiteral("textAvailable")).toBool());
    QVERIFY(!value.contains(QStringLiteral("text")));
}

void UiProbeTest::inspectionReadsOptionalValueText()
{
    const auto field = run({
        QStringLiteral("find"),
        QStringLiteral("--pid"),
        QString::number(m_fake.processId()),
        QStringLiteral("--id"),
        QStringLiteral("panel.settings.terminal.fontSize"),
    });
    const auto inspected = run({
        QStringLiteral("inspect"),
        field.value(QStringLiteral("matches"))
            .toArray()
            .first()
            .toObject()
            .value(QStringLiteral("handle"))
            .toString(),
    });
    const auto value =
        inspected.value(QStringLiteral("element"))
            .toObject()
            .value(QStringLiteral("value"))
            .toObject();
    QVERIFY(value.value(QStringLiteral("textAvailable")).toBool());
    QCOMPARE(
        value.value(QStringLiteral("text")).toString(),
        QStringLiteral("14 pt"));
}

void UiProbeTest::rawInputUsesAtSpiController()
{
    struct EnvironmentRestore {
        QByteArray sessionType;
        QByteArray waylandDisplay;

        ~EnvironmentRestore()
        {
            if (sessionType.isNull()) {
                qunsetenv("XDG_SESSION_TYPE");
            } else {
                qputenv("XDG_SESSION_TYPE", sessionType);
            }
            if (waylandDisplay.isNull()) {
                qunsetenv("WAYLAND_DISPLAY");
            } else {
                qputenv("WAYLAND_DISPLAY", waylandDisplay);
            }
        }
    } restore{
        qgetenv("XDG_SESSION_TYPE"),
        qgetenv("WAYLAND_DISPLAY"),
    };
    qputenv("XDG_SESSION_TYPE", "x11");
    qunsetenv("WAYLAND_DISPLAY");

    QFile missingMethod(m_atspiMissingMethodFile);
    QVERIFY(missingMethod.open(QIODevice::WriteOnly));
    missingMethod.close();
    const auto unavailable = run({QStringLiteral("input-status")});
    QVERIFY(
        !unavailable.value(QStringLiteral("atspi"))
             .toObject()
             .value(QStringLiteral("available"))
             .toBool());
    QCOMPARE(
        unavailable.value(QStringLiteral("atspi"))
            .toObject()
            .value(QStringLiteral("errorKind"))
            .toString(),
        QStringLiteral("atspi-controller-methods-unavailable"));
    QFile::remove(m_atspiMissingMethodFile);

    qputenv("XDG_SESSION_TYPE", "wayland");
    const auto forbiddenWaylandAtSpi = run(
        {
            QStringLiteral("key"),
            QStringLiteral("--key"),
            QStringLiteral("A"),
            QStringLiteral("--adapter"),
            QStringLiteral("atspi"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Unsupported));
    QCOMPARE(
        forbiddenWaylandAtSpi.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("kind"))
            .toString(),
        QStringLiteral("wayland-atspi-input-forbidden"));
    qputenv("XDG_SESSION_TYPE", "x11");

    QFile::remove(m_inputLog);
    const auto shortcut = run({
        QStringLiteral("shortcut"),
        QStringLiteral("--keys"),
        QStringLiteral("Ctrl+Shift+P"),
        QStringLiteral("--adapter"),
        QStringLiteral("atspi"),
    });
    QCOMPARE(
        shortcut.value(QStringLiteral("adapter")).toString(),
        QStringLiteral("atspi-device-event-controller"));
    QCOMPARE(shortcut.value(QStringLiteral("eventCount")).toInt(), 6);

    QFile log(m_inputLog);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const auto lines =
        QString::fromUtf8(log.readAll())
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 5);
    QList<int> keys;
    QList<int> states;
    for (const auto& line : lines) {
        const auto event =
            QJsonDocument::fromJson(line.toUtf8()).object();
        keys.append(event.value(QStringLiteral("key")).toInt());
        states.append(event.value(QStringLiteral("state")).toInt());
    }
    QCOMPARE(keys, QList<int>({4, 1, 'p', 1, 4}));
    QCOMPARE(states, QList<int>({5, 5, 3, 6, 6}));
    log.close();

    QFile::remove(m_inputLog);
    const auto settings = run({
        QStringLiteral("find"),
        QStringLiteral("--pid"),
        QString::number(m_fake.processId()),
        QStringLiteral("--id"),
        QStringLiteral("header.settings"),
    });
    static_cast<void>(run({
        QStringLiteral("click"),
        settings.value(QStringLiteral("matches"))
            .toArray()
            .first()
            .toObject()
            .value(QStringLiteral("handle"))
            .toString(),
    }));
    const auto field = run({
        QStringLiteral("find"),
        QStringLiteral("--pid"),
        QString::number(m_fake.processId()),
        QStringLiteral("--id"),
        QStringLiteral("panel.settings.terminal.fontFamily"),
    });
    const auto fieldHandle =
        field.value(QStringLiteral("matches"))
            .toArray()
            .first()
            .toObject()
            .value(QStringLiteral("handle"))
            .toString();
    const auto elementPointer = run({
        QStringLiteral("pointer"),
        QStringLiteral("--element"),
        fieldHandle,
        QStringLiteral("--adapter"),
        QStringLiteral("atspi"),
    });
    QCOMPARE(
        elementPointer.value(QStringLiteral("details"))
            .toObject()
            .value(QStringLiteral("x"))
            .toInt(),
        160);
    QFile overflowBounds(m_atspiOverflowBoundsFile);
    QVERIFY(overflowBounds.open(QIODevice::WriteOnly));
    overflowBounds.close();
    const auto overflowButton = run(
        {
            QStringLiteral("button"),
            QStringLiteral("--button"),
            QStringLiteral("left"),
            QStringLiteral("--element"),
            fieldHandle,
            QStringLiteral("--adapter"),
            QStringLiteral("atspi"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::LimitExceeded));
    QVERIFY(!overflowButton.value(QStringLiteral("ok")).toBool());
    QFile::remove(m_atspiOverflowBoundsFile);

    QFile::remove(m_inputLog);
    const auto failedShortcut = run(
        {
            QStringLiteral("shortcut"),
            QStringLiteral("--keys"),
            QStringLiteral("Ctrl+Shift+F12"),
            QStringLiteral("--adapter"),
            QStringLiteral("atspi"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::RemoteError));
    QVERIFY(!failedShortcut.value(QStringLiteral("ok")).toBool());
    const auto cleanupPlan =
        kodosi::ui_probe::shortcutPlan(QStringLiteral("Ctrl+Shift+F12"));
    QCOMPARE(cleanupPlan.cleanup.size(), 3);
    QCOMPARE(cleanupPlan.cleanup.at(0).code, 0xffc9);
    QCOMPARE(cleanupPlan.cleanup.at(1).code, 0xffe1);
    QCOMPARE(cleanupPlan.cleanup.at(2).code, 0xffe3);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const auto failureLines =
        QString::fromUtf8(log.readAll())
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(failureLines.size() >= 5);
    const auto penultimate =
        QJsonDocument::fromJson(
            failureLines.at(failureLines.size() - 2).toUtf8())
            .object();
    const auto last =
        QJsonDocument::fromJson(failureLines.last().toUtf8()).object();
    QCOMPARE(penultimate.value(QStringLiteral("key")).toInt(), 1);
    QCOMPARE(penultimate.value(QStringLiteral("state")).toInt(), 6);
    QCOMPARE(last.value(QStringLiteral("key")).toInt(), 4);
    QCOMPARE(last.value(QStringLiteral("state")).toInt(), 6);
    log.close();

    QFile::remove(m_inputLog);
    const auto failedDrag = run(
        {
            QStringLiteral("drag"),
            QStringLiteral("--from-x"),
            QStringLiteral("10"),
            QStringLiteral("--from-y"),
            QStringLiteral("20"),
            QStringLiteral("--to-x"),
            QStringLiteral("999999"),
            QStringLiteral("--to-y"),
            QStringLiteral("30"),
            QStringLiteral("--duration-ms"),
            QStringLiteral("0"),
            QStringLiteral("--adapter"),
            QStringLiteral("atspi"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::RemoteError));
    QVERIFY(!failedDrag.value(QStringLiteral("ok")).toBool());
    QVERIFY(log.open(QIODevice::ReadOnly));
    const auto dragLines =
        QString::fromUtf8(log.readAll())
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(!dragLines.isEmpty());
    const auto cleanup =
        QJsonDocument::fromJson(dragLines.last().toUtf8()).object();
    QCOMPARE(
        cleanup.value(QStringLiteral("event")).toString(),
        QStringLiteral("b1r"));
    log.close();
}

void UiProbeTest::portalSidecarSerializesInput()
{
    const auto logContains = [&](const QString& method) {
        QFile log(m_inputLog);
        return log.open(QIODevice::ReadOnly)
            && QString::fromUtf8(log.readAll()).contains(method);
    };
    QFile::remove(m_inputLog);
    QFile deny(m_portalDenyFile);
    QVERIFY(deny.open(QIODevice::WriteOnly));
    deny.close();
    const auto denied = run(
        {
            QStringLiteral("input-start"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("5000"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::PortalDenied));
    QVERIFY(!denied.value(QStringLiteral("ok")).toBool());
    QFile::remove(m_portalDenyFile);

    QFile closeEarly(m_portalCloseEarlyFile);
    QVERIFY(closeEarly.open(QIODevice::WriteOnly));
    closeEarly.close();
    const auto closedDuringSetup = run(
        {
            QStringLiteral("input-start"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("5000"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::PortalDenied));
    QCOMPARE(
        closedDuringSetup.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("kind"))
            .toString(),
        QStringLiteral("portal-session-closed"));
    QFile::remove(m_portalCloseEarlyFile);

    QFile delay(m_portalDelayFile);
    QVERIFY(delay.open(QIODevice::WriteOnly));
    delay.close();
    QFile::remove(m_inputLog);
    const auto timedOutStart = run(
        {
            QStringLiteral("input-start"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("200"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Timeout));
    QVERIFY(!timedOutStart.value(QStringLiteral("ok")).toBool());
    QFile::remove(m_portalDelayFile);
    QTRY_VERIFY_WITH_TIMEOUT(
        logContains(QStringLiteral("\"Request.Close\""))
            && logContains(QStringLiteral("\"Session.Close\"")),
        10'000);
    QFile timeoutLog(m_inputLog);
    QVERIFY(timeoutLog.open(QIODevice::ReadOnly));
    const auto timeoutContents =
        QString::fromUtf8(timeoutLog.readAll());
    QVERIFY(timeoutContents.contains(QStringLiteral("\"Request.Close\"")));
    QVERIFY(timeoutContents.contains(QStringLiteral("\"Session.Close\"")));
    timeoutLog.close();

    QFile methodDelay(m_portalMethodDelayFile);
    QVERIFY(methodDelay.open(QIODevice::WriteOnly));
    methodDelay.close();
    QFile::remove(m_inputLog);
    const auto methodTimedOutStart = run(
        {
            QStringLiteral("input-start"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("200"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Timeout));
    QVERIFY(!methodTimedOutStart.value(QStringLiteral("ok")).toBool());
    QFile::remove(m_portalMethodDelayFile);
    QTRY_VERIFY_WITH_TIMEOUT(
        logContains(QStringLiteral("\"Request.Close\"")),
        10'000);
    QVERIFY(timeoutLog.open(QIODevice::ReadOnly));
    const auto methodTimeoutContents =
        QString::fromUtf8(timeoutLog.readAll());
    QVERIFY(
        methodTimeoutContents.contains(
            QStringLiteral("\"Request.Close\"")));
    timeoutLog.close();

    QVERIFY(delay.open(QIODevice::WriteOnly));
    delay.close();
    QProcess concurrentFirst;
    QProcess concurrentSecond;
    for (auto* process : {&concurrentFirst, &concurrentSecond}) {
        process->setProgram(QStringLiteral(KODOSI_UI_PROBE_PATH));
        process->setArguments({
            QStringLiteral("input-start"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("5000"),
        });
        process->start();
        QVERIFY2(
            process->waitForStarted(),
            qPrintable(process->errorString()));
    }
    QVERIFY(concurrentFirst.waitForFinished(10000));
    QVERIFY(concurrentSecond.waitForFinished(10000));
    const auto concurrentSuccesses =
        static_cast<int>(
            concurrentFirst.exitStatus() == QProcess::NormalExit
            && concurrentFirst.exitCode() == 0)
        + static_cast<int>(
            concurrentSecond.exitStatus() == QProcess::NormalExit
            && concurrentSecond.exitCode() == 0);
    QCOMPARE(concurrentSuccesses, 1);
    QFile::remove(m_portalDelayFile);
    const auto concurrentStatus =
        run({QStringLiteral("input-status")});
    QVERIFY(
        concurrentStatus.value(QStringLiteral("sidecar"))
            .toObject()
            .value(QStringLiteral("running"))
            .toBool());
    const auto concurrentStopped = run({
        QStringLiteral("input-stop"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    QVERIFY(concurrentStopped.value(QStringLiteral("stopped")).toBool());

    const auto started = run({
        QStringLiteral("input-start"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    QVERIFY(started.value(QStringLiteral("ok")).toBool());
    const auto status = run({QStringLiteral("input-status")});
    QVERIFY(
        status.value(QStringLiteral("sidecar"))
            .toObject()
            .value(QStringLiteral("running"))
            .toBool());
    const auto sidecarState =
        status.value(QStringLiteral("sidecar")).toObject();
    const auto activeSession =
        sidecarState.value(QStringLiteral("session")).toObject();
    QVERIFY(
        activeSession.value(QStringLiteral("absolutePointer")).toBool());
    QCOMPARE(
        activeSession.value(QStringLiteral("absolutePointerCapability"))
            .toString(),
        QStringLiteral("available"));
    const QFileInfo socketInfo(
        sidecarState.value(QStringLiteral("socket")).toString());
    QVERIFY(socketInfo.exists());
    QVERIFY(!(socketInfo.permissions()
              & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
                 | QFileDevice::ReadOther | QFileDevice::WriteOther)));

    const auto shortcut = run({
        QStringLiteral("shortcut"),
        QStringLiteral("--keys"),
        QStringLiteral("Ctrl+P"),
        QStringLiteral("--adapter"),
        QStringLiteral("portal"),
    });
    QCOMPARE(
        shortcut.value(QStringLiteral("adapter")).toString(),
        QStringLiteral("xdg-desktop-portal-remote-desktop"));
    const auto pointer = run({
        QStringLiteral("pointer"),
        QStringLiteral("--dx"),
        QStringLiteral("12"),
        QStringLiteral("--dy"),
        QStringLiteral("-4"),
        QStringLiteral("--adapter"),
        QStringLiteral("portal"),
    });
    QCOMPARE(pointer.value(QStringLiteral("eventCount")).toInt(), 1);
    const auto absolute = run({
        QStringLiteral("pointer"),
        QStringLiteral("--x"),
        QStringLiteral("112"),
        QStringLiteral("--y"),
        QStringLiteral("204"),
        QStringLiteral("--adapter"),
        QStringLiteral("portal"),
    });
    QVERIFY(absolute.value(QStringLiteral("ok")).toBool());
    QVERIFY(timeoutLog.open(QIODevice::ReadOnly));
    const auto inputLines =
        QString::fromUtf8(timeoutLog.readAll())
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    timeoutLog.close();
    bool foundAbsolute = false;
    for (const auto& line : inputLines) {
        const auto event =
            QJsonDocument::fromJson(line.toUtf8()).object();
        if (event.value(QStringLiteral("adapter")).toString()
                == QStringLiteral("portal")
            && event.value(QStringLiteral("method")).toString()
                == QStringLiteral("NotifyPointerMotionAbsolute")) {
            const auto arguments =
                event.value(QStringLiteral("arguments")).toArray();
            QCOMPARE(arguments.at(1).toInt(), 42);
            QCOMPARE(arguments.at(2).toInt(), 12);
            QCOMPARE(arguments.at(3).toInt(), 4);
            foundAbsolute = true;
        }
    }
    QVERIFY(foundAbsolute);

    QProcess longDrag;
    longDrag.setProgram(QStringLiteral(KODOSI_UI_PROBE_PATH));
    longDrag.setArguments({
        QStringLiteral("drag"),
        QStringLiteral("--from-x"),
        QStringLiteral("110"),
        QStringLiteral("--from-y"),
        QStringLiteral("210"),
        QStringLiteral("--to-x"),
        QStringLiteral("700"),
        QStringLiteral("--to-y"),
        QStringLiteral("700"),
        QStringLiteral("--duration-ms"),
        QStringLiteral("30000"),
        QStringLiteral("--adapter"),
        QStringLiteral("portal"),
    });
    longDrag.start();
    QVERIFY2(
        longDrag.waitForStarted(),
        qPrintable(longDrag.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            QFile inputLog(m_inputLog);
            return inputLog.open(QIODevice::ReadOnly)
                && QString::fromUtf8(inputLog.readAll())
                       .contains(
                           QStringLiteral("\"NotifyPointerButton\""));
        }(),
        2000);
    QElapsedTimer interruptedStop;
    interruptedStop.start();
    const auto interrupted = run({
        QStringLiteral("input-stop"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    QVERIFY(interrupted.value(QStringLiteral("stopped")).toBool());
    QVERIFY(interruptedStop.elapsed() < 2000);
    QVERIFY(longDrag.waitForFinished(3000));
    QVERIFY(longDrag.exitCode() != 0);
    const auto restartedAfterInterrupt = run({
        QStringLiteral("input-start"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    QVERIFY(restartedAfterInterrupt.value(QStringLiteral("ok")).toBool());

    const auto closeSession = run({
        QStringLiteral("key"),
        QStringLiteral("--key"),
        QStringLiteral("F12"),
        QStringLiteral("--down"),
        QStringLiteral("--adapter"),
        QStringLiteral("portal"),
    });
    QVERIFY(closeSession.value(QStringLiteral("ok")).toBool());
    QTest::qWait(100);
    const auto closed = run({QStringLiteral("input-status")});
    QVERIFY(
        closed.value(QStringLiteral("sidecar"))
            .toObject()
            .value(QStringLiteral("sessionClosed"))
            .toBool());
    const auto stoppedClosed = run({
        QStringLiteral("input-stop"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("1000"),
    });
    QVERIFY(
        stoppedClosed.value(QStringLiteral("sessionAlreadyClosed"))
            .toBool());

    QFile noStream(m_portalNoStreamFile);
    QVERIFY(noStream.open(QIODevice::WriteOnly));
    noStream.close();
    const auto noStreamStarted = run({
        QStringLiteral("input-start"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    QVERIFY(noStreamStarted.value(QStringLiteral("ok")).toBool());
    const auto noStreamStatus = run({QStringLiteral("input-status")});
    const auto noStreamSession =
        noStreamStatus.value(QStringLiteral("sidecar"))
            .toObject()
            .value(QStringLiteral("session"))
            .toObject();
    QVERIFY(
        !noStreamSession.value(QStringLiteral("absolutePointer")).toBool());
    QCOMPARE(
        noStreamSession.value(QStringLiteral("absolutePointerCapability"))
            .toString(),
        QStringLiteral("unavailable-no-stream"));
    const auto unavailableAbsolute = run(
        {
            QStringLiteral("pointer"),
            QStringLiteral("--x"),
            QStringLiteral("112"),
            QStringLiteral("--y"),
            QStringLiteral("204"),
            QStringLiteral("--adapter"),
            QStringLiteral("portal"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Unsupported));
    QCOMPARE(
        unavailableAbsolute.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("kind"))
            .toString(),
        QStringLiteral("portal-absolute-pointer-unavailable"));
    static_cast<void>(run({
        QStringLiteral("input-stop"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    }));
    QFile::remove(m_portalNoStreamFile);

    const auto root =
        QDir(m_runtimeDirectory).filePath(QStringLiteral("kodosi-ui-probe"));
    const auto staleSocket =
        QDir(root).filePath(QStringLiteral("input-stale.sock"));
    QFile socket(staleSocket);
    QVERIFY(socket.open(QIODevice::WriteOnly));
    socket.close();
    QVERIFY(socket.setPermissions(
        QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QFile state(QDir(root).filePath(QStringLiteral("input-state.json")));
    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(state.setPermissions(
        QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    const auto stateBytes =
        QJsonDocument(QJsonObject{
            {QStringLiteral("pid"), 999999},
            {QStringLiteral("processStartTime"), QStringLiteral("1")},
            {QStringLiteral("socket"), staleSocket},
            {QStringLiteral("nonce"),
             QStringLiteral("12345678901234567890123456789012")},
            {QStringLiteral("startupToken"),
             QStringLiteral("abcdefghijklmnopqrstuvwxyzABCDEF")},
            {QStringLiteral("committed"), true},
            {QStringLiteral("sessionClosed"), true},
        }).toJson(QJsonDocument::Compact);
    QCOMPARE(state.write(stateBytes), stateBytes.size());
    state.close();
    const auto restarted = run({
        QStringLiteral("input-start"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    QVERIFY(restarted.value(QStringLiteral("ok")).toBool());
    QVERIFY(!QFileInfo::exists(staleSocket));
    const auto stopped = run({
        QStringLiteral("input-stop"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    QVERIFY(stopped.value(QStringLiteral("stopped")).toBool());
}

void UiProbeTest::portalSidecarCancelsWithLauncher()
{
    constexpr auto portalStartupTimeoutMs = 10'000;
    const auto processStartTime = [](const qint64 processId) {
        QFile statFile(
            QStringLiteral("/proc/%1/stat").arg(processId));
        if (!statFile.open(QIODevice::ReadOnly)) {
            return QString();
        }
        const auto stat = statFile.readAll();
        const auto commandEnd = stat.lastIndexOf(')');
        const auto fields =
            commandEnd < 0
            ? QList<QByteArray>()
            : stat.sliced(commandEnd + 1).simplified().split(' ');
        return fields.size() > 19
            ? QString::fromLatin1(fields.at(19))
            : QString();
    };
    const auto childOf = [](const qint64 processId) {
        QFile children(
            QStringLiteral("/proc/%1/task/%1/children")
                .arg(processId));
        if (!children.open(QIODevice::ReadOnly)) {
            return qint64(-1);
        }
        bool valid = false;
        const auto child =
            children.readAll().trimmed().split(' ').value(0).toLongLong(
                &valid);
        return valid ? child : qint64(-1);
    };
    const auto logContains = [&](const QString& method) {
        QFile log(m_inputLog);
        return log.open(QIODevice::ReadOnly)
            && QString::fromUtf8(log.readAll()).contains(method);
    };
    const auto root =
        QDir(m_runtimeDirectory).filePath(
            QStringLiteral("kodosi-ui-probe"));

    for (const auto signal : {SIGTERM, SIGKILL}) {
        QFile::remove(m_inputLog);
        QFile delay(m_portalDelayFile);
        QVERIFY(delay.open(QIODevice::WriteOnly));
        delay.close();

        QProcess launcher;
        launcher.setProgram(QStringLiteral(KODOSI_UI_PROBE_PATH));
        launcher.setArguments({
            QStringLiteral("input-start"),
            QStringLiteral("--timeout-ms"),
            QStringLiteral("5000"),
        });
        launcher.start();
        QVERIFY2(
            launcher.waitForStarted(),
            qPrintable(launcher.errorString()));

        qint64 childPid = -1;
        QTRY_VERIFY_WITH_TIMEOUT(
            (childPid = childOf(launcher.processId())) > 0,
            portalStartupTimeoutMs);
        const auto childStartTime = processStartTime(childPid);
        QVERIFY(!childStartTime.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(
            logContains(QStringLiteral("\"SelectSources\"")),
            portalStartupTimeoutMs);

        QVERIFY(::kill(launcher.processId(), signal) == 0);
        QVERIFY(launcher.waitForFinished(3000));
        QFile::remove(m_portalDelayFile);
        QTest::qWait(2300);

        const auto statePath =
            QDir(root).filePath(QStringLiteral("input-state.json"));
        QString socketPath;
        QFile state(statePath);
        if (state.open(QIODevice::ReadOnly)) {
            socketPath =
                QJsonDocument::fromJson(state.readAll())
                    .object()
                    .value(QStringLiteral("socket"))
                    .toString();
        }
        const auto survived =
            processStartTime(childPid) == childStartTime;
        const auto stateExists = QFileInfo::exists(statePath);
        const auto socketExists =
            !socketPath.isEmpty() && QFileInfo::exists(socketPath);
        const auto requestClosed =
            logContains(QStringLiteral("\"Request.Close\""));
        const auto sessionClosed =
            logContains(QStringLiteral("\"Session.Close\""));

        if (stateExists) {
            static_cast<void>(run({
                QStringLiteral("input-stop"),
                QStringLiteral("--timeout-ms"),
                QStringLiteral("5000"),
            }));
        } else if (survived) {
            static_cast<void>(::kill(childPid, SIGTERM));
        }

        QVERIFY(!survived);
        QVERIFY(!stateExists);
        QVERIFY(!socketExists);
        QVERIFY(
            QDir(root)
                .entryList(
                    {QStringLiteral("input-*.sock")},
                    QDir::Files)
                .isEmpty());
        QVERIFY(requestClosed);
        QVERIFY(sessionClosed);
    }

    QFile::remove(m_inputLog);
    QFile readyDelay(m_portalDelayFile);
    QVERIFY(readyDelay.open(QIODevice::WriteOnly));
    readyDelay.close();
    QProcess readyLauncher;
    readyLauncher.setProgram(QStringLiteral(KODOSI_UI_PROBE_PATH));
    readyLauncher.setArguments({
        QStringLiteral("input-start"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    readyLauncher.start();
    QVERIFY2(
        readyLauncher.waitForStarted(),
        qPrintable(readyLauncher.errorString()));
    qint64 readyChildPid = -1;
    QTRY_VERIFY_WITH_TIMEOUT(
        (readyChildPid = childOf(readyLauncher.processId())) > 0,
        portalStartupTimeoutMs);
    const auto readyChildStartTime =
        processStartTime(readyChildPid);
    QVERIFY(!readyChildStartTime.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(
        logContains(QStringLiteral("\"SelectSources\"")),
        portalStartupTimeoutMs);
    QVERIFY(::kill(readyLauncher.processId(), SIGSTOP) == 0);
    QFile::remove(m_portalDelayFile);
    const auto statePath =
        QDir(root).filePath(QStringLiteral("input-state.json"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(statePath), 3000);
    QFile provisionalState(statePath);
    QVERIFY(provisionalState.open(QIODevice::ReadOnly));
    const auto provisional =
        QJsonDocument::fromJson(provisionalState.readAll()).object();
    QVERIFY(!provisional.value(QStringLiteral("committed")).toBool());
    const auto provisionalSocket =
        provisional.value(QStringLiteral("socket")).toString();
    QVERIFY(QFileInfo::exists(provisionalSocket));
    QVERIFY(::kill(readyLauncher.processId(), SIGKILL) == 0);
    QVERIFY(readyLauncher.waitForFinished(3000));
    QTRY_VERIFY_WITH_TIMEOUT(
        processStartTime(readyChildPid) != readyChildStartTime,
        3000);
    QVERIFY(!QFileInfo::exists(statePath));
    QVERIFY(!QFileInfo::exists(provisionalSocket));
    QVERIFY(logContains(QStringLiteral("\"Session.Close\"")));

    QFile::remove(m_inputLog);
    QFile delay(m_portalDelayFile);
    QVERIFY(delay.open(QIODevice::WriteOnly));
    delay.close();
    QProcess launcher;
    launcher.setProgram(QStringLiteral(KODOSI_UI_PROBE_PATH));
    launcher.setArguments({
        QStringLiteral("input-start"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    launcher.start();
    QVERIFY2(
        launcher.waitForStarted(),
        qPrintable(launcher.errorString()));
    qint64 childPid = -1;
    QTRY_VERIFY_WITH_TIMEOUT(
        (childPid = childOf(launcher.processId())) > 0,
        portalStartupTimeoutMs);
    const auto childStartTime = processStartTime(childPid);
    QVERIFY(!childStartTime.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(
        logContains(QStringLiteral("\"SelectSources\"")),
        portalStartupTimeoutMs);
    const auto pendingStatus = run({QStringLiteral("input-status")});
    const auto pendingSidecar =
        pendingStatus.value(QStringLiteral("sidecar")).toObject();
    QVERIFY(pendingSidecar.value(QStringLiteral("pending")).toBool());
    QCOMPARE(
        pendingSidecar.value(QStringLiteral("pid")).toInteger(),
        childPid);
    const auto cancelled = run({
        QStringLiteral("input-stop"),
        QStringLiteral("--timeout-ms"),
        QStringLiteral("5000"),
    });
    QVERIFY(cancelled.value(QStringLiteral("pendingCancelled")).toBool());
    QCOMPARE(cancelled.value(QStringLiteral("pid")).toInteger(), childPid);
    QVERIFY(launcher.waitForFinished(3000));
    QFile::remove(m_portalDelayFile);
    QTest::qWait(2300);
    QVERIFY(processStartTime(childPid) != childStartTime);
    QVERIFY(!QFileInfo::exists(
        QDir(root).filePath(QStringLiteral("input-state.json"))));
    QVERIFY(!QFileInfo::exists(
        QDir(root).filePath(QStringLiteral("input-pending.json"))));
    QVERIFY(
        QDir(root)
            .entryList(
                {QStringLiteral("input-*.sock")},
                QDir::Files)
            .isEmpty());
    QVERIFY(logContains(QStringLiteral("\"Request.Close\"")));
    QVERIFY(logContains(QStringLiteral("\"Session.Close\"")));
}

void UiProbeTest::inputValidationIsBounded()
{
    const auto invalidKey = run(
        {
            QStringLiteral("key"),
            QStringLiteral("--key"),
            QStringLiteral("DefinitelyNotAKey"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Usage));
    QVERIFY(!invalidKey.value(QStringLiteral("ok")).toBool());
    const auto invalidPointer = run(
        {
            QStringLiteral("pointer"),
            QStringLiteral("--x"),
            QStringLiteral("1000001"),
            QStringLiteral("--y"),
            QStringLiteral("0"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Usage));
    QVERIFY(!invalidPointer.value(QStringLiteral("ok")).toBool());
    const auto invalidDuration = run(
        {
            QStringLiteral("drag"),
            QStringLiteral("--from-x"),
            QStringLiteral("0"),
            QStringLiteral("--from-y"),
            QStringLiteral("0"),
            QStringLiteral("--to-x"),
            QStringLiteral("1"),
            QStringLiteral("--to-y"),
            QStringLiteral("1"),
            QStringLiteral("--duration-ms"),
            QStringLiteral("30001"),
        },
        static_cast<int>(kodosi::ui_probe::ExitCode::Usage));
    QVERIFY(!invalidDuration.value(QStringLiteral("ok")).toBool());
}

void UiProbeTest::protocolFramesAndPeerIdentityAreBounded()
{
    const QJsonObject message{
        {QStringLiteral("ok"), true},
        {QStringLiteral("value"), 42},
    };
    const auto framed =
        kodosi::ui_probe::InputProtocol::frame(message);
    auto partial = framed.first(3);
    QVERIFY(
        !kodosi::ui_probe::InputProtocol::takeFrame(&partial).has_value());
    partial.append(framed.sliced(3));
    const auto decoded =
        kodosi::ui_probe::InputProtocol::takeFrame(&partial);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->value(QStringLiteral("value")).toInt(), 42);
    QVERIFY(partial.isEmpty());

    QByteArray oversized(4, '\0');
    const auto size =
        static_cast<quint32>(
            kodosi::ui_probe::InputProtocol::MaximumMessageBytes + 1);
    oversized[0] = static_cast<char>((size >> 24U) & 0xffU);
    oversized[1] = static_cast<char>((size >> 16U) & 0xffU);
    oversized[2] = static_cast<char>((size >> 8U) & 0xffU);
    oversized[3] = static_cast<char>(size & 0xffU);
    QVERIFY_EXCEPTION_THROWN(
        [&oversized] {
            const auto ignored =
                kodosi::ui_probe::InputProtocol::takeFrame(&oversized);
            Q_UNUSED(ignored)
        }(),
        kodosi::ui_probe::ProbeError);
    QVERIFY(kodosi::ui_probe::InputProtocol::peerUidAllowed(1000, 1000));
    QVERIFY(!kodosi::ui_probe::InputProtocol::peerUidAllowed(1000, 1001));
}


QTEST_MAIN(UiProbeTest)

#include "tst_ui_probe.moc"
