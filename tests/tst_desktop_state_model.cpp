#include "models/DesktopStateModel.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QWindow>
#include <QtTest/QTest>

#include <memory>
#include <ranges>

namespace {

constexpr auto stateKey = "desktop/state.v1";
constexpr auto currentStateKey = "desktop/state.v2";

class FakeSessionDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;

    Result send(
        const kodosi::CommandLane lane,
        const QByteArrayView json) override
    {
        const auto document =
            QJsonDocument::fromJson(json.toByteArray());
        if (lane != kodosi::CommandLane::Sessions
            || !document.isObject()) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        commands.append(document.object());
        return {};
    }
};

std::unique_ptr<QSettings> settingsFor(
    const QTemporaryDir& directory,
    const QString& fileName = QStringLiteral("settings.ini"))
{
    return std::make_unique<QSettings>(
        directory.filePath(fileName),
        QSettings::IniFormat);
}

QByteArray storedState(
    const QRect geometry = QRect(100, 100, 1'000, 700),
    const bool maximized = false,
    const int activeView = 0,
    const bool sidebarOpen = true,
    const QString& selectedSessionId = {})
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("version"), 1},
        {QStringLiteral("activeView"), activeView},
        {QStringLiteral("sidebarOpen"), sidebarOpen},
        {QStringLiteral("selectedSessionId"), selectedSessionId},
        {
            QStringLiteral("window"),
            QJsonObject {
                {
                    QStringLiteral("normal"),
                    QJsonObject {
                        {QStringLiteral("x"), geometry.x()},
                        {QStringLiteral("y"), geometry.y()},
                        {QStringLiteral("width"), geometry.width()},
                        {QStringLiteral("height"), geometry.height()},
                    },
                },
                {QStringLiteral("maximized"), maximized},
            },
        },
    }).toJson(QJsonDocument::Compact);
}

QJsonObject readStoredState(const QTemporaryDir& directory)
{
    auto storage = settingsFor(directory);
    storage->sync();
    const auto current = storage->value(
        QString::fromLatin1(currentStateKey));
    const auto value = current.isValid()
        ? current
        : storage->value(QString::fromLatin1(stateKey));
    if (value.metaType() != QMetaType::fromType<QByteArray>()) {
        return {};
    }
    return QJsonDocument::fromJson(value.toByteArray()).object();
}

QJsonObject liveSession(
    const QString& id,
    const QString& kind = QStringLiteral("local"),
    const QString& connectionState = {},
    const QString& accessState = {},
    const int permissions = 1,
    const QString& incarnationId = {})
{
    QJsonObject session {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), id},
        {QStringLiteral("incarnationId"),
         incarnationId.isEmpty()
             ? QStringLiteral("incarnation-") + id
             : incarnationId},
        {QStringLiteral("name"), id},
        {QStringLiteral("project"), QStringLiteral("/repo")},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("scope"), QStringLiteral("justMe")},
        {QStringLiteral("access"), QStringLiteral("inject")},
    };
    if (kind == QStringLiteral("local")) {
        session.insert(
            QStringLiteral("recovery"),
            QStringLiteral("live"));
    } else {
        session.insert(QStringLiteral("permissions"), permissions);
        session.insert(
            QStringLiteral("connectionState"),
            connectionState);
        session.insert(
            QStringLiteral("accessState"),
            accessState);
    }
    return session;
}

void activateAccount(
    kodosi::DesktopStateModel& state,
    kodosi::SessionCatalogModel& sessions,
    const QString& userId,
    const quint64 epoch)
{
    const auto type = userId.isEmpty()
        ? QStringLiteral("auth.required")
        : QStringLiteral("auth.ready");
    const auto auth = QJsonDocument(QJsonObject {
        {QStringLiteral("type"), type},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
    }).toJson(QJsonDocument::Compact);
    state.ingestAuthEvent(auth);
    sessions.ingestAuthEvent(auth);
}

void applySnapshot(
    kodosi::SessionCatalogModel& sessions,
    const QString& userId,
    const quint64 epoch,
    const QJsonArray& entries)
{
    sessions.ingestSessionEvent(
        QJsonDocument(QJsonObject {
            {QStringLiteral("authority"),
             QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"), userId},
            {QStringLiteral("accountEpoch"),
             static_cast<qint64>(epoch)},
            {QStringLiteral("type"), QStringLiteral("session.list")},
            {QStringLiteral("sessions"), entries},
        }).toJson(QJsonDocument::Compact));
}

void applyUpsert(
    kodosi::SessionCatalogModel& sessions,
    const QString& userId,
    const quint64 epoch,
    const QJsonObject& entry)
{
    sessions.ingestSessionEvent(
        QJsonDocument(QJsonObject {
            {QStringLiteral("authority"),
             QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"), userId},
            {QStringLiteral("accountEpoch"),
             static_cast<qint64>(epoch)},
            {QStringLiteral("type"), QStringLiteral("session.upsert")},
            {QStringLiteral("session"), entry},
        }).toJson(QJsonDocument::Compact));
}

qsizetype commandCount(
    const FakeSessionDispatcher& dispatcher,
    const QString& type)
{
    return std::ranges::count_if(
        dispatcher.commands,
        [&type](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString()
                == type;
        });
}

QString readSelectedSessionId(const QJsonObject& state)
{
    const auto stage = state.value(QStringLiteral("stage"));
    return stage.isObject()
        ? stage.toObject()
              .value(QStringLiteral("selectedSessionId"))
              .toString()
        : state.value(QStringLiteral("selectedSessionId")).toString();
}

QRect readNormalGeometry(const QJsonObject& state)
{
    const auto normal = state.value(QStringLiteral("window"))
                            .toObject()
                            .value(QStringLiteral("normal"))
                            .toObject();
    return QRect(
        normal.value(QStringLiteral("x")).toInt(),
        normal.value(QStringLiteral("y")).toInt(),
        normal.value(QStringLiteral("width")).toInt(),
        normal.value(QStringLiteral("height")).toInt());
}

QTemporaryDir stateDirectory(const QString& name)
{
    return QTemporaryDir(
        QDir::current().filePath(name + QStringLiteral("-XXXXXX")));
}

const QList<QRect> desktopScreens {
    QRect(0, 0, 1'920, 1'080),
};

} // namespace

class DesktopStateModelTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsShellStateBeforeAttach();
    void defaultCentering();
    void strictMalformedAndTypeRejection();
    void offScreenAndMissingMonitorClamp();
    void oversizedAndUndersizedGeometry();
    void primaryIndexSurvivesInvalidScreenFiltering();
    void minimumSizeFitsSmallTargetScreen();
    void windowSizeOverrideIsAppliedByAttach();
    void maximizedPreservesNormalGeometry();
    void geometryFirstMaximizeOrderingPreservesNormalGeometry();
    void stateFirstMaximizeOrderingPreservesNormalGeometry();
    void mappedMaximizedRestore();
    void pendingMaximizeIgnoresNoStateEcho();
    void maximizeRestoreRetriesAfterDelayedNoState();
    void maximizeRestoreFailureIsBounded();
    void unmaximizeWaitsForSettledNormalGeometry();
    void restoreDownFlushesPreserveConfirmedMaximizedState();
    void screenSizedNormalGeometryPersistsOnSmallScreen();
    void settledScreenSizedRestoreDownPersistsNormal();
    void immediateNonScreenRestoreDownFlushesNormalState();
    void debouncedWritesAndCloseFlush();
    void shellStatePersists();
    void invalidSetterSurfacesError();
    void disabledPersistenceUsesDeterministicDefaults();
    void writeFailureSurfacesError();
    void liveLayoutChangeRenormalizesAndReappliesMaximized();
    void attachedScreenChangeDuringMaximizeRestorePreservesMaximizedState();
    void geometryFirstMaximizeLayoutChangePreservesConfirmedNormal();
    void layoutChangeDuringRestoreDownRestoresMaximizedState();
    void attachedScreenChangeDuringRestoreDownDoesNotStampGeometry();
    void liveLayoutChangePreservesPendingNormalGeometry();
    void liveLayoutChangePreservesMinimizedAndFullScreenState();
    void restoreAfterSpecialStateHotplugAppliesNormalizedGeometry_data();
    void restoreAfterSpecialStateHotplugAppliesNormalizedGeometry();
    void restoreMaximizedAfterSpecialStateHotplug_data();
    void restoreMaximizedAfterSpecialStateHotplug();
    void attachedScreenChangeUsesExactAvailableGeometry();
    void repeatedAttachIgnoresDetachedWindowCallbacks();
    void migratesVersionOneSelectionToVersionTwoStage();
    void stagingFocusAndUnstageMaintainSelection();
    void initialSnapshotAutoStagesAtMostSixLocalSessions();
    void reconciliationWaitsForAuthoritativeSnapshots();
    void selectionRequiresRetainableCatalogPresentation();
    void liveCatalogMutationsPruneStageAndExitFocus();
    void liveUpsertKindControlsDeferredSelection();
    void remoteRestorationIsAccountScopedAndExact();
    void accountSwitchDoesNotCarryOverlappingRemoteStageIds();
    void firstAccountDoesNotInheritPersistedRemoteStage();
    void remoteRestorationRequiresAcceptanceAndCorrelatesCompletion();
    void pendingExplicitRemoteOpenCoalescesAutomaticRestore_data();
    void pendingExplicitRemoteOpenCoalescesAutomaticRestore();
    void reincarnatedRemoteRestoreKeepsCurrentIdentityEligible();
    void stageReadyRemoteDoesNotDispatchAutomaticRestore();
    void failedRemoteRestoreIsSuppressedWithinAuthorityGeneration();
    void remoteRestorationRepeatsForFreshRuntimeAuthority();
    void remoteRestorationTracksConnectableUpsertsOnce();
    void explicitStageMutationsAreNotRestorationCapped();
    void stageLayoutModeIsTransientAcrossRestart();
};

void DesktopStateModelTest::loadsShellStateBeforeAttach()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-preload"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(
                QRect(100, 100, 1'000, 700),
                false,
                2,
                false,
                QStringLiteral("preloaded-session")));
        storage->sync();
    }

    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);

    QCOMPARE(state.activeView(), 0);
    QVERIFY(!state.sidebarOpen());
    QCOMPARE(
        state.selectedSessionId(),
        QStringLiteral("preloaded-session"));
}

void DesktopStateModelTest::defaultCentering()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-default"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        QList<QRect> {QRect(100, 50, 1'920, 1'080)});
    QWindow window;

    state.attachWindow(&window);

    QVERIFY(window.isVisible());
    QCOMPARE(window.geometry(), QRect(440, 190, 1'240, 800));
    QCOMPARE(window.minimumSize(), QSize(820, 560));
    QCOMPARE(state.activeView(), 0);
    QVERIFY(state.sidebarOpen());
    QVERIFY(state.selectedSessionId().isEmpty());
    QVERIFY(state.lastError().isEmpty());
}

void DesktopStateModelTest::strictMalformedAndTypeRejection()
{
    const QList<QVariant> invalidValues {
        QStringLiteral("not a byte array"),
        QByteArrayLiteral("{"),
        QByteArrayLiteral(
            "{\"version\":1,\"activeView\":false,\"sidebarOpen\":true,"
            "\"selectedSessionId\":\"\",\"window\":{\"normal\":{"
            "\"x\":0,\"y\":0,\"width\":1000,\"height\":700},"
            "\"maximized\":false}}"),
        QByteArrayLiteral(
            "{\"version\":1,\"activeView\":1.5,\"sidebarOpen\":true,"
            "\"selectedSessionId\":\"\",\"window\":{\"normal\":{"
            "\"x\":0,\"y\":0,\"width\":1000,\"height\":700},"
            "\"maximized\":false}}"),
        storedState(QRect(0, 0, 0, 700)),
        storedState(
            QRect(0, 0, 1'000, 700),
            false,
            0,
            true,
            QString(1'025, QLatin1Char('x'))),
    };

    for (qsizetype index = 0; index < invalidValues.size(); ++index) {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-invalid-%1").arg(index));
        QVERIFY(directory.isValid());
        {
            auto storage = settingsFor(directory);
            storage->setValue(
                QString::fromLatin1(stateKey),
                invalidValues.at(index));
            storage->sync();
        }

        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        QWindow window;
        state.attachWindow(&window);

        QCOMPARE(window.geometry(), QRect(340, 140, 1'240, 800));
        QCOMPARE(state.activeView(), 0);
        QVERIFY(state.sidebarOpen());
        QVERIFY(state.selectedSessionId().isEmpty());
        QVERIFY2(!state.lastError().isEmpty(), qPrintable(QString::number(index)));
    }
}

void DesktopStateModelTest::offScreenAndMissingMonitorClamp()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-offscreen"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(3'000, 200, 1'000, 700)));
        storage->sync();
    }
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    QCOMPARE(window.geometry(), QRect(920, 200, 1'000, 700));

    auto secondDirectory =
        stateDirectory(QStringLiteral("desktop-state-largest-intersection"));
    QVERIFY(secondDirectory.isValid());
    {
        auto storage = settingsFor(secondDirectory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(1'700, 100, 1'200, 700)));
        storage->sync();
    }
    kodosi::DesktopStateModel secondState(
        settingsFor(secondDirectory),
        QList<QRect> {
            QRect(0, 0, 1'920, 1'080),
            QRect(1'920, 0, 1'600, 900),
        });
    QWindow secondWindow;
    secondState.attachWindow(&secondWindow);
    QCOMPARE(secondWindow.geometry(), QRect(1'920, 100, 1'200, 700));
}

void DesktopStateModelTest::oversizedAndUndersizedGeometry()
{
    const QList<QPair<QRect, QRect>> cases {
        {
            QRect(-500, -400, 5'000, 4'000),
            QRect(0, 0, 1'920, 1'080),
        },
        {
            QRect(1'700, 900, 300, 200),
            QRect(1'100, 520, 820, 560),
        },
    };
    for (qsizetype index = 0; index < cases.size(); ++index) {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-size-%1").arg(index));
        QVERIFY(directory.isValid());
        {
            auto storage = settingsFor(directory);
            storage->setValue(
                QString::fromLatin1(stateKey),
                storedState(cases.at(index).first));
            storage->sync();
        }
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        QWindow window;
        state.attachWindow(&window);
        QCOMPARE(window.size(), cases.at(index).second.size());
        QVERIFY(desktopScreens.constFirst().contains(
            window.geometry().center()));
    }
}

void DesktopStateModelTest::primaryIndexSurvivesInvalidScreenFiltering()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-primary-filter"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        QList<QRect> {
            QRect {},
            QRect(100, 50, 1'920, 1'080),
            QRect(2'020, 50, 1'280, 720),
        },
        1);
    QWindow window;

    state.attachWindow(&window);

    QCOMPARE(window.geometry(), QRect(440, 190, 1'240, 800));
}

void DesktopStateModelTest::minimumSizeFitsSmallTargetScreen()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-small-screen"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        QList<QRect> {QRect(0, 0, 640, 480)});
    QWindow window;

    state.attachWindow(&window);

    QCOMPARE(window.size(), QSize(640, 480));
    QCOMPARE(window.minimumSize(), QSize(640, 480));
}

void DesktopStateModelTest::windowSizeOverrideIsAppliedByAttach()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-size-override"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens,
        0,
        false);
    QWindow window;

    state.attachWindow(&window, QSize(900, 600));

    QVERIFY(window.isVisible());
    QCOMPARE(window.geometry(), QRect(510, 240, 900, 600));
}

void DesktopStateModelTest::maximizedPreservesNormalGeometry()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-maximized"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    window.setGeometry(QRect(150, 120, 1'100, 720));
    QTest::qWait(220);

    window.setWindowState(Qt::WindowMaximized);
    QCoreApplication::processEvents();
    window.setGeometry(QRect(0, 0, 1'920, 1'080));
    window.setWindowState(Qt::WindowFullScreen);
    QCoreApplication::processEvents();
    window.setGeometry(QRect(0, 0, 1'600, 900));
    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(&window, &closeEvent);

    const auto stored = readStoredState(directory);
    QCOMPARE(readNormalGeometry(stored), QRect(150, 120, 1'100, 720));
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());

    kodosi::DesktopStateModel reloaded(
        settingsFor(directory),
        desktopScreens);
    QWindow restoredWindow;
    reloaded.attachWindow(&restoredWindow);
    QTRY_COMPARE(
        restoredWindow.windowState(),
        Qt::WindowMaximized);
}

void DesktopStateModelTest::geometryFirstMaximizeOrderingPreservesNormalGeometry()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-geometry-first"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    window.setGeometry(QRect(160, 130, 1'040, 690));
    QTest::qWait(220);

    window.setGeometry(desktopScreens.constFirst());
    window.setWindowState(Qt::WindowMaximized);
    QTest::qWait(220);

    const auto stored = readStoredState(directory);
    QCOMPARE(
        readNormalGeometry(stored),
        QRect(160, 130, 1'040, 690));
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::stateFirstMaximizeOrderingPreservesNormalGeometry()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-state-first"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    window.setGeometry(QRect(180, 150, 1'020, 680));
    QTest::qWait(220);

    window.setWindowState(Qt::WindowMaximized);
    window.setGeometry(desktopScreens.constFirst());
    QTest::qWait(220);

    const auto stored = readStoredState(directory);
    QCOMPARE(
        readNormalGeometry(stored),
        QRect(180, 150, 1'020, 680));
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::mappedMaximizedRestore()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-mapped-restore"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(140, 110, 1'080, 710), true));
        storage->sync();
    }
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;

    state.attachWindow(&window);

    QVERIFY(window.isVisible());
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
}

void DesktopStateModelTest::pendingMaximizeIgnoresNoStateEcho()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-maximize-echo"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(150, 120, 1'100, 720), true));
        storage->sync();
    }
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;

    state.attachWindow(&window);
    QVERIFY(QMetaObject::invokeMethod(
        &window,
        "windowStateChanged",
        Qt::DirectConnection,
        Q_ARG(Qt::WindowState, Qt::WindowNoState)));

    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    const auto stored = readStoredState(directory);
    QCOMPARE(
        readNormalGeometry(stored),
        QRect(150, 120, 1'100, 720));
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::maximizeRestoreRetriesAfterDelayedNoState()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-maximize-delayed-echo"));
    QVERIFY(directory.isValid());
    const QRect confirmedGeometry(150, 120, 1'100, 720);
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(confirmedGeometry, true));
        storage->sync();
    }
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;

    state.attachWindow(&window);
    QVERIFY(QMetaObject::invokeMethod(
        &window,
        "windowStateChanged",
        Qt::DirectConnection,
        Q_ARG(Qt::WindowState, Qt::WindowNoState)));
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);

    QTimer::singleShot(
        20,
        &window,
        [&window] {
            window.setWindowState(Qt::WindowNoState);
        });
    QTest::qWait(400);

    QCOMPARE(window.windowState(), Qt::WindowMaximized);
    const auto stored = readStoredState(directory);
    QCOMPARE(readNormalGeometry(stored), confirmedGeometry);
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::maximizeRestoreFailureIsBounded()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-maximize-rejected"));
    QVERIFY(directory.isValid());
    const QRect confirmedGeometry(150, 120, 1'100, 720);
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(confirmedGeometry, true));
        storage->sync();
    }
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    QSignalSpy stateChanges(&window, &QWindow::windowStateChanged);
    connect(
        &window,
        &QWindow::windowStateChanged,
        &window,
        [&window](const Qt::WindowState windowState) {
            if (windowState == Qt::WindowMaximized) {
                QTimer::singleShot(
                   0,
                   &window,
                   [&window] {
                       window.setWindowState(Qt::WindowNoState);
                   });
            }
        });

    state.attachWindow(&window);
    QTest::qWait(500);

    QCOMPARE(window.windowState(), Qt::WindowNoState);
    QVERIFY(stateChanges.count() >= 4);
    const auto settledChangeCount = stateChanges.count();
    QTest::qWait(300);
    QCOMPARE(stateChanges.count(), settledChangeCount);
    const auto stored = readStoredState(directory);
    QCOMPARE(readNormalGeometry(stored), confirmedGeometry);
    QVERIFY(!stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::unmaximizeWaitsForSettledNormalGeometry()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-unmaximize"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(130, 100, 1'080, 710), true));
        storage->sync();
    }
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(100);

    window.setWindowState(Qt::WindowNoState);
    window.setGeometry(QRect(200, 170, 1'000, 660));
    window.setGeometry(QRect(230, 190, 980, 650));
    QCoreApplication::processEvents();
    QCOMPARE(
        readNormalGeometry(readStoredState(directory)),
        QRect(130, 100, 1'080, 710));

    QTest::qWait(220);
    const auto stored = readStoredState(directory);
    QCOMPARE(
        readNormalGeometry(stored),
        QRect(230, 190, 980, 650));
    QVERIFY(!stored.value(QStringLiteral("window"))
                 .toObject()
                 .value(QStringLiteral("maximized"))
                 .toBool());
}

void DesktopStateModelTest::restoreDownFlushesPreserveConfirmedMaximizedState()
{
    const QList<QString> flushes {
        QStringLiteral("close"),
        QStringLiteral("hide"),
        QStringLiteral("destroy"),
    };
    const QRect confirmedGeometry(130, 100, 1'080, 710);

    for (const auto& flush : flushes) {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-restore-down-%1").arg(flush));
        QVERIFY(directory.isValid());
        {
            auto storage = settingsFor(directory);
            storage->setValue(
                QString::fromLatin1(stateKey),
                storedState(confirmedGeometry, true));
            storage->sync();
        }
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        auto window = std::make_unique<QWindow>();
        state.attachWindow(window.get());
        window->setGeometry(desktopScreens.constFirst());

        if (flush == QStringLiteral("close")) {
            QCloseEvent closeEvent;
            QCoreApplication::sendEvent(window.get(), &closeEvent);
        } else if (flush == QStringLiteral("hide")) {
            window->hide();
        } else {
            window.reset();
        }

        const auto stored = readStoredState(directory);
        QCOMPARE(readNormalGeometry(stored), confirmedGeometry);
        QVERIFY(stored.value(QStringLiteral("window"))
                    .toObject()
                    .value(QStringLiteral("maximized"))
                    .toBool());
    }
}

void DesktopStateModelTest::screenSizedNormalGeometryPersistsOnSmallScreen()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-small-screen-normal"));
    QVERIFY(directory.isValid());
    const QRect smallScreen(0, 0, 640, 480);
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        QList<QRect> {smallScreen});
    QWindow window;
    state.attachWindow(&window);

    QCOMPARE(window.windowState(), Qt::WindowNoState);
    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(&window, &closeEvent);

    const auto stored = readStoredState(directory);
    QCOMPARE(readNormalGeometry(stored), smallScreen);
    QVERIFY(!stored.value(QStringLiteral("window"))
                 .toObject()
                 .value(QStringLiteral("maximized"))
                 .toBool());
}

void DesktopStateModelTest::settledScreenSizedRestoreDownPersistsNormal()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-screen-sized-restore-down"));
    QVERIFY(directory.isValid());
    const QRect confirmedGeometry(150, 120, 1'100, 720);
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(confirmedGeometry, true));
        storage->sync();
    }
    {
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        QWindow window;
        state.attachWindow(&window);
        QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
        QTest::qWait(100);

        window.setWindowState(Qt::WindowNoState);
        window.setGeometry(desktopScreens.constFirst());
        QTest::qWait(220);

        QCloseEvent closeEvent;
        QCoreApplication::sendEvent(&window, &closeEvent);
    }

    const auto stored = readStoredState(directory);
    QCOMPARE(readNormalGeometry(stored), desktopScreens.constFirst());
    QVERIFY(!stored.value(QStringLiteral("window"))
                 .toObject()
                 .value(QStringLiteral("maximized"))
                 .toBool());

    kodosi::DesktopStateModel reloaded(
        settingsFor(directory),
        desktopScreens);
    QWindow restoredWindow;
    reloaded.attachWindow(&restoredWindow);
    QTest::qWait(100);
    QCOMPARE(restoredWindow.windowState(), Qt::WindowNoState);
}

void DesktopStateModelTest::immediateNonScreenRestoreDownFlushesNormalState()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-immediate-normal-restore-down"));
    QVERIFY(directory.isValid());
    const QRect confirmedGeometry(150, 120, 1'100, 720);
    const QRect restoredGeometry(220, 180, 980, 650);
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(confirmedGeometry, true));
        storage->sync();
    }
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(100);

    window.setWindowState(Qt::WindowNoState);
    window.setGeometry(restoredGeometry);
    QCoreApplication::processEvents();
    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(&window, &closeEvent);

    const auto stored = readStoredState(directory);
    QCOMPARE(readNormalGeometry(stored), restoredGeometry);
    QVERIFY(!stored.value(QStringLiteral("window"))
                 .toObject()
                 .value(QStringLiteral("maximized"))
                 .toBool());
}

void DesktopStateModelTest::debouncedWritesAndCloseFlush()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-debounce"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    window.setGeometry(QRect(200, 160, 1'050, 710));
    QCoreApplication::processEvents();

    QVERIFY(readStoredState(directory).isEmpty());
    QTest::qWait(220);
    QCOMPARE(
        readNormalGeometry(readStoredState(directory)),
        QRect(200, 160, 1'050, 710));

    window.setGeometry(QRect(260, 210, 980, 680));
    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(&window, &closeEvent);
    QCOMPARE(
        readNormalGeometry(readStoredState(directory)),
        QRect(260, 210, 980, 680));

    auto destructionDirectory =
        stateDirectory(QStringLiteral("desktop-state-destruction"));
    QVERIFY(destructionDirectory.isValid());
    kodosi::DesktopStateModel destructionState(
        settingsFor(destructionDirectory),
        desktopScreens);
    auto destroyedWindow = std::make_unique<QWindow>();
    destructionState.attachWindow(destroyedWindow.get());
    destroyedWindow->setGeometry(QRect(310, 240, 940, 640));
    destroyedWindow.reset();
    QCOMPARE(
        readNormalGeometry(readStoredState(destructionDirectory)),
        QRect(310, 240, 940, 640));
}

void DesktopStateModelTest::shellStatePersists()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-shell"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    state.setActiveView(1);
    state.setSidebarOpen(false);
    state.setSelectedSessionId(QStringLiteral("session-0190"));

    QCOMPARE(state.activeView(), 1);
    QVERIFY(!state.sidebarOpen());
    QCOMPARE(state.selectedSessionId(), QStringLiteral("session-0190"));
    QVERIFY(state.lastError().isEmpty());

    kodosi::DesktopStateModel reloaded(
        settingsFor(directory),
        desktopScreens);
    QWindow restoredWindow;
    reloaded.attachWindow(&restoredWindow);
    QCOMPARE(reloaded.activeView(), 1);
    QVERIFY(!reloaded.sidebarOpen());
    QCOMPARE(
        reloaded.selectedSessionId(),
        QStringLiteral("session-0190"));
}

void DesktopStateModelTest::invalidSetterSurfacesError()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-setter"));
    QVERIFY(directory.isValid());
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    QSignalSpy errorChanged(
        &state,
        &kodosi::DesktopStateModel::lastErrorChanged);

    state.setActiveView(3);
    QCOMPARE(state.activeView(), 0);
    QVERIFY(!state.lastError().isEmpty());
    QCOMPARE(errorChanged.count(), 1);

    state.setSelectedSessionId(QStringLiteral("accepted-session"));
    state.setSelectedSessionId(QString(1'025, QLatin1Char('x')));
    QCOMPARE(
        state.selectedSessionId(),
        QStringLiteral("accepted-session"));
    QVERIFY(!state.lastError().isEmpty());
    QCOMPARE(
        readSelectedSessionId(readStoredState(directory)),
        QStringLiteral("accepted-session"));
}

void DesktopStateModelTest::disabledPersistenceUsesDeterministicDefaults()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-disabled"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(40, 40, 900, 600), true, 2, false,
                        QStringLiteral("persisted")));
        storage->sync();
    }
    const auto previous = readStoredState(directory);
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens,
        0,
        false);
    QWindow window;
    state.attachWindow(&window);
    QCOMPARE(window.geometry(), QRect(340, 140, 1'240, 800));
    QCOMPARE(window.windowState(), Qt::WindowNoState);
    QCOMPARE(state.activeView(), 0);
    QVERIFY(state.sidebarOpen());
    QVERIFY(state.selectedSessionId().isEmpty());

    state.setActiveView(1);
    window.setGeometry(QRect(20, 20, 900, 600));
    QTest::qWait(220);
    QCOMPARE(readStoredState(directory), previous);
}

void DesktopStateModelTest::writeFailureSurfacesError()
{
    auto directory = stateDirectory(QStringLiteral("desktop-state-write"));
    QVERIFY(directory.isValid());
    const auto settingsPath =
        directory.filePath(QStringLiteral("settings.ini"));
    const auto previousState = storedState(
        QRect(100, 100, 1'000, 700),
        false,
        0,
        true,
        QStringLiteral("stored-session"));
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            previousState);
        storage->sync();
        QCOMPARE(storage->status(), QSettings::NoError);
    }

    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow window;
    state.attachWindow(&window);
    QVERIFY(QFile::setPermissions(
        settingsPath,
        QFileDevice::ReadOwner));
    QVERIFY(QFile::setPermissions(
        directory.path(),
        QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    auto restorePermissions = qScopeGuard([&] {
        (void)QFile::setPermissions(
            directory.path(),
            QFileDevice::ReadOwner
                | QFileDevice::WriteOwner
                | QFileDevice::ExeOwner);
        (void)QFile::setPermissions(
            settingsPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    });
    QSignalSpy sidebarChanged(
        &state,
        &kodosi::DesktopStateModel::sidebarOpenChanged);
    state.setSidebarOpen(false);

    QVERIFY(!state.sidebarOpen());
    QCOMPARE(sidebarChanged.count(), 1);
    QVERIFY(!state.lastError().isEmpty());
    window.setGeometry(QRect(260, 210, 980, 680));
    QCoreApplication::processEvents();
    QVERIFY(QFile::setPermissions(
        directory.path(),
        QFileDevice::ReadOwner
            | QFileDevice::WriteOwner
            | QFileDevice::ExeOwner));
    QVERIFY(QFile::setPermissions(
        settingsPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    restorePermissions.dismiss();
    QCOMPARE(readStoredState(directory).value(QStringLiteral("sidebarOpen")).toBool(), true);

    QCloseEvent closeEvent;
    QCoreApplication::sendEvent(&window, &closeEvent);

    const auto retried = readStoredState(directory);
    QVERIFY(!retried.value(QStringLiteral("sidebarOpen")).toBool());
    QCOMPARE(readNormalGeometry(retried), QRect(260, 210, 980, 680));
    QVERIFY(state.lastError().isEmpty());
}

void DesktopStateModelTest::liveLayoutChangeRenormalizesAndReappliesMaximized()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-live-layout"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(2'100, 100, 1'000, 700), true));
        storage->sync();
    }
    kodosi::DesktopStateModel::ScreenLayout layout {
        .availableGeometries = {
            QRect(0, 0, 1'920, 1'080),
            QRect(1'920, 0, 1'600, 900),
        },
        .primaryIndex = 0,
    };
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        [&layout] { return layout; },
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(100);

    layout.availableGeometries = {QRect(0, 0, 1'280, 720)};
    emit notifier.layoutChanged();
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(220);

    QCOMPARE(
        readNormalGeometry(readStoredState(directory)),
        QRect(280, 20, 1'000, 700));
    QCOMPARE(window.minimumSize(), QSize(820, 560));
}

void DesktopStateModelTest::
    attachedScreenChangeDuringMaximizeRestorePreservesMaximizedState()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-maximize-screen-gap"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(2'100, 100, 1'000, 700), true));
        storage->sync();
    }
    kodosi::DesktopStateModel::ScreenLayout layout {
        .availableGeometries = {
            QRect(0, 0, 1'920, 1'080),
            QRect(1'920, 0, 1'600, 900),
        },
        .primaryIndex = 0,
    };
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        [&layout] { return layout; },
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(100);

    const QRect remainingScreen(0, 0, 1'280, 720);
    layout.availableGeometries = {remainingScreen};
    emit notifier.layoutChanged();
    QCOMPARE(window.windowState(), Qt::WindowNoState);
    emit notifier.attachedWindowScreenChanged(remainingScreen);

    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(220);
    const auto stored = readStoredState(directory);
    QCOMPARE(
        readNormalGeometry(stored),
        QRect(280, 20, 1'000, 700));
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::
    geometryFirstMaximizeLayoutChangePreservesConfirmedNormal()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-geometry-first-layout"));
    QVERIFY(directory.isValid());
    const QRect confirmedGeometry(160, 130, 1'040, 690);
    kodosi::DesktopStateModel::ScreenLayout layout {
        .availableGeometries = {desktopScreens.constFirst()},
        .primaryIndex = 0,
    };
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        [&layout] { return layout; },
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);
    window.setGeometry(confirmedGeometry);
    QTest::qWait(220);

    window.setGeometry(desktopScreens.constFirst());
    layout.availableGeometries = {QRect(0, 0, 1'280, 720)};
    emit notifier.layoutChanged();
    window.setWindowState(Qt::WindowMaximized);
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(220);

    const auto stored = readStoredState(directory);
    QCOMPARE(
        readNormalGeometry(stored),
        QRect(160, 30, 1'040, 690));
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::
    layoutChangeDuringRestoreDownRestoresMaximizedState()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-restore-down-layout"));
    QVERIFY(directory.isValid());
    const QRect confirmedGeometry(2'100, 100, 1'000, 700);
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(confirmedGeometry, true));
        storage->sync();
    }
    kodosi::DesktopStateModel::ScreenLayout layout {
        .availableGeometries = {
            desktopScreens.constFirst(),
            QRect(1'920, 0, 1'600, 900),
        },
        .primaryIndex = 0,
    };
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        [&layout] { return layout; },
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(100);

    window.setWindowState(Qt::WindowNoState);
    window.setGeometry(layout.availableGeometries.constLast());
    layout.availableGeometries = {QRect(0, 0, 1'280, 720)};
    emit notifier.layoutChanged();

    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(220);
    const auto stored = readStoredState(directory);
    QCOMPARE(
        readNormalGeometry(stored),
        QRect(280, 20, 1'000, 700));
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::
    attachedScreenChangeDuringRestoreDownDoesNotStampGeometry()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-restore-down-screen-change"));
    QVERIFY(directory.isValid());
    const QRect confirmedGeometry(150, 120, 1'100, 720);
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(confirmedGeometry, true));
        storage->sync();
    }
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens,
        0,
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(100);

    window.setWindowState(Qt::WindowNoState);
    window.setGeometry(desktopScreens.constFirst());
    const auto transitionalGeometry = window.geometry();
    emit notifier.attachedWindowScreenChanged(
        QRect(1'920, 0, 900, 600));

    QCOMPARE(window.geometry(), transitionalGeometry);
    QCOMPARE(window.minimumSize(), QSize(820, 560));
    state.setSidebarOpen(false);
    const auto stored = readStoredState(directory);
    QCOMPARE(readNormalGeometry(stored), confirmedGeometry);
    QVERIFY(stored.value(QStringLiteral("window"))
                .toObject()
                .value(QStringLiteral("maximized"))
                .toBool());
}

void DesktopStateModelTest::liveLayoutChangePreservesPendingNormalGeometry()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-live-pending-layout"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(100, 100, 1'000, 700)));
        storage->sync();
    }
    kodosi::DesktopStateModel::ScreenLayout layout {
        .availableGeometries = {
            QRect(0, 0, 1'920, 1'080),
            QRect(1'920, 0, 1'600, 900),
        },
        .primaryIndex = 0,
    };
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        [&layout] { return layout; },
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);

    window.setGeometry(QRect(2'200, 120, 900, 650));
    QCoreApplication::processEvents();
    layout.availableGeometries = {QRect(0, 0, 1'280, 720)};
    emit notifier.layoutChanged();

    const QRect expected(380, 70, 900, 650);
    QCOMPARE(window.geometry(), expected);
    QTest::qWait(220);
    const auto stored = readStoredState(directory);
    QCOMPARE(readNormalGeometry(stored), expected);
    QVERIFY(!stored.value(QStringLiteral("window"))
                 .toObject()
                 .value(QStringLiteral("maximized"))
                 .toBool());
}

void DesktopStateModelTest::liveLayoutChangePreservesMinimizedAndFullScreenState()
{
    const QList<Qt::WindowState> windowStates {
        Qt::WindowMinimized,
        Qt::WindowFullScreen,
    };

    for (const auto windowState : windowStates) {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-live-special-%1")
                .arg(static_cast<int>(windowState)));
        QVERIFY(directory.isValid());
        {
            auto storage = settingsFor(directory);
            storage->setValue(
                QString::fromLatin1(stateKey),
                storedState(QRect(2'100, 100, 1'000, 700), true));
            storage->sync();
        }
        kodosi::DesktopStateModel::ScreenLayout layout {
            .availableGeometries = {
                QRect(0, 0, 1'920, 1'080),
                QRect(1'920, 0, 1'600, 900),
            },
            .primaryIndex = 0,
        };
        kodosi::DesktopScreenLayoutNotifier notifier;
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            [&layout] { return layout; },
            true,
            &notifier);
        QWindow window;
        state.attachWindow(&window);
        QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
        QTest::qWait(100);
        window.setWindowState(windowState);
        QTRY_COMPARE(window.windowState(), windowState);

        layout.availableGeometries = {QRect(0, 0, 1'280, 720)};
        emit notifier.layoutChanged();
        QCoreApplication::processEvents();

        QCOMPARE(window.windowState(), windowState);
        QTest::qWait(220);
        const auto stored = readStoredState(directory);
        QCOMPARE(
            readNormalGeometry(stored),
            QRect(280, 20, 1'000, 700));
        QVERIFY(stored.value(QStringLiteral("window"))
                    .toObject()
                    .value(QStringLiteral("maximized"))
                    .toBool());
    }
}

void DesktopStateModelTest::
    restoreAfterSpecialStateHotplugAppliesNormalizedGeometry_data()
{
    QTest::addColumn<Qt::WindowState>("windowState");
    QTest::newRow("minimized") << Qt::WindowMinimized;
    QTest::newRow("fullscreen") << Qt::WindowFullScreen;
}

void DesktopStateModelTest::
    restoreAfterSpecialStateHotplugAppliesNormalizedGeometry()
{
    QFETCH(Qt::WindowState, windowState);

    auto directory = stateDirectory(
        QStringLiteral("desktop-state-special-restore-%1")
            .arg(static_cast<int>(windowState)));
    QVERIFY(directory.isValid());
    const QRect removedScreenGeometry(2'100, 100, 1'000, 700);
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(removedScreenGeometry));
        storage->sync();
    }
    kodosi::DesktopStateModel::ScreenLayout layout {
        .availableGeometries = {
            QRect(0, 0, 1'920, 1'080),
            QRect(1'920, 0, 1'600, 900),
        },
        .primaryIndex = 0,
    };
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        [&layout] { return layout; },
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);
    QCOMPARE(window.geometry(), removedScreenGeometry);
    window.setWindowState(windowState);
    QTRY_COMPARE(window.windowState(), windowState);
    const auto specialStateGeometry = window.geometry();

    layout.availableGeometries = {QRect(0, 0, 1'280, 720)};
    emit notifier.layoutChanged();
    QCOMPARE(window.windowState(), windowState);
    QCOMPARE(window.geometry(), specialStateGeometry);

    window.setWindowState(Qt::WindowNoState);
    QTRY_COMPARE(window.windowState(), Qt::WindowNoState);
    const QRect expected(280, 20, 1'000, 700);
    QTRY_COMPARE(window.geometry(), expected);
    QTest::qWait(220);
    QCOMPARE(readNormalGeometry(readStoredState(directory)), expected);
}

void DesktopStateModelTest::
    restoreMaximizedAfterSpecialStateHotplug_data()
{
    QTest::addColumn<Qt::WindowState>("windowState");
    QTest::addColumn<bool>("directMaximizedRestore");
    QTest::newRow("minimized-via-normal")
        << Qt::WindowMinimized << false;
    QTest::newRow("fullscreen-via-normal")
        << Qt::WindowFullScreen << false;
    QTest::newRow("minimized-direct-maximized")
        << Qt::WindowMinimized << true;
    QTest::newRow("fullscreen-direct-maximized")
        << Qt::WindowFullScreen << true;
}

void DesktopStateModelTest::
    restoreMaximizedAfterSpecialStateHotplug()
{
    QFETCH(Qt::WindowState, windowState);
    QFETCH(bool, directMaximizedRestore);

    auto directory = stateDirectory(
        QStringLiteral("desktop-state-maximized-special-restore-%1")
            .arg(static_cast<int>(windowState)));
    QVERIFY(directory.isValid());
    const QRect removedScreenGeometry(2'100, 100, 1'000, 700);
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(removedScreenGeometry, true));
        storage->sync();
    }
    kodosi::DesktopStateModel::ScreenLayout layout {
        .availableGeometries = {
            QRect(0, 0, 1'920, 1'080),
            QRect(1'920, 0, 1'600, 900),
        },
        .primaryIndex = 0,
    };
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        [&layout] { return layout; },
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);
    QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
    QTest::qWait(100);
    window.setWindowState(windowState);
    QTRY_COMPARE(window.windowState(), windowState);

    layout.availableGeometries = {QRect(0, 0, 1'280, 720)};
    emit notifier.layoutChanged();
    QCOMPARE(window.windowState(), windowState);

    const QRect expected(280, 20, 1'000, 700);
    if (directMaximizedRestore) {
        window.setWindowState(Qt::WindowNoState);
        QCOMPARE(window.windowState(), Qt::WindowNoState);
        QVERIFY(window.geometry() != expected);
        window.setWindowState(Qt::WindowMaximized);
        QCOMPARE(window.windowState(), Qt::WindowMaximized);
        QCoreApplication::processEvents();
        QCOMPARE(window.windowState(), Qt::WindowMaximized);

        window.setWindowState(Qt::WindowNoState);
        const QRect restored(180, 100, 900, 600);
        window.setGeometry(restored);
        QTRY_COMPARE(window.windowState(), Qt::WindowNoState);
        QTest::qWait(220);

        const auto stored = readStoredState(directory);
        QCOMPARE(readNormalGeometry(stored), restored);
        QVERIFY(!stored.value(QStringLiteral("window"))
                     .toObject()
                     .value(QStringLiteral("maximized"))
                     .toBool());
    } else {
        window.setWindowState(Qt::WindowNoState);
        emit notifier.attachedWindowScreenChanged(
            layout.availableGeometries.constFirst());
        QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
        QTest::qWait(220);

        const auto stored = readStoredState(directory);
        QCOMPARE(readNormalGeometry(stored), expected);
        QVERIFY(stored.value(QStringLiteral("window"))
                    .toObject()
                    .value(QStringLiteral("maximized"))
                    .toBool());
    }
}

void DesktopStateModelTest::attachedScreenChangeUsesExactAvailableGeometry()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-attached-screen"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(100, 100, 1'000, 700)));
        storage->sync();
    }
    kodosi::DesktopStateModel::ScreenLayout layout {
        .availableGeometries = {
            QRect(0, 0, 1'920, 1'080),
            QRect(1'920, 0, 900, 600),
        },
        .primaryIndex = 0,
    };
    kodosi::DesktopScreenLayoutNotifier notifier;
    kodosi::DesktopStateModel state(
        settingsFor(directory),
        [&layout] { return layout; },
        true,
        &notifier);
    QWindow window;
    state.attachWindow(&window);
    QCOMPARE(window.geometry(), QRect(100, 100, 1'000, 700));

    emit notifier.attachedWindowScreenChanged(
        layout.availableGeometries.constLast());
    QCoreApplication::processEvents();

    QCOMPARE(window.minimumSize(), QSize(820, 560));
    QCOMPARE(window.size(), QSize(900, 600));
    QVERIFY(window.x() >= 1'920);
    QTest::qWait(220);
    QCOMPARE(
        readNormalGeometry(readStoredState(directory)),
        QRect(1'920, 0, 900, 600));
}

void DesktopStateModelTest::repeatedAttachIgnoresDetachedWindowCallbacks()
{
    auto directory = stateDirectory(
        QStringLiteral("desktop-state-reattach"));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(stateKey),
            storedState(QRect(120, 90, 1'060, 700), true));
        storage->sync();
    }

    kodosi::DesktopStateModel state(
        settingsFor(directory),
        desktopScreens);
    QWindow firstWindow;
    QWindow secondWindow;
    state.attachWindow(&firstWindow);
    state.attachWindow(&secondWindow);
    QTRY_COMPARE(secondWindow.windowState(), Qt::WindowMaximized);
    QTest::qWait(100);
    QCOMPARE(firstWindow.windowState(), Qt::WindowNoState);

    secondWindow.setWindowState(Qt::WindowNoState);
    secondWindow.setGeometry(QRect(260, 210, 980, 680));
    QTest::qWait(220);

    QVERIFY(secondWindow.isVisible());
    QCOMPARE(
        readNormalGeometry(readStoredState(directory)),
        QRect(260, 210, 980, 680));
}

    void DesktopStateModelTest::migratesVersionOneSelectionToVersionTwoStage()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-v2-migration"));
        QVERIFY(directory.isValid());
        {
            auto storage = settingsFor(directory);
            storage->setValue(
                QString::fromLatin1(stateKey),
                storedState(
                    QRect(120, 90, 1'060, 700),
                    true,
                    2,
                    false,
                    QStringLiteral("migrated-session")));
            storage->sync();
        }

        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);

        QCOMPARE(state.activeView(), 0);
        QVERIFY(!state.sidebarOpen());
        QCOMPARE(
            state.selectedSessionId(),
            QStringLiteral("migrated-session"));
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("migrated-session")});
        QCOMPARE(
            state.stageLayoutMode(),
            kodosi::DesktopStateModel::StageLayoutMode::Grid);
        const auto migrated = readStoredState(directory);
        QCOMPARE(migrated.value(QStringLiteral("version")).toInt(), 2);
        QCOMPARE(migrated.value(QStringLiteral("activeView")).toInt(), 0);
        QCOMPARE(readNormalGeometry(migrated), QRect(120, 90, 1'060, 700));
        QVERIFY(migrated.value(QStringLiteral("window"))
                    .toObject()
                    .value(QStringLiteral("maximized"))
                    .toBool());

        auto emptyDirectory = stateDirectory(
            QStringLiteral("desktop-state-v2-empty-migration"));
        QVERIFY(emptyDirectory.isValid());
        {
            auto storage = settingsFor(emptyDirectory);
            storage->setValue(
                QString::fromLatin1(stateKey),
                storedState());
            storage->sync();
            kodosi::DesktopStateModel migrating(
                settingsFor(emptyDirectory),
                desktopScreens);
            QVERIFY(migrating.stagedSessionIds().isEmpty());
        }
        kodosi::DesktopStateModel reloaded(
            settingsFor(emptyDirectory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        reloaded.attachSessionCatalog(&sessions);
        activateAccount(reloaded, sessions, {}, 1);
        applySnapshot(
            sessions,
            {},
            1,
            QJsonArray {liveSession(QStringLiteral("auto-stage"))});
        QCOMPARE(
            reloaded.stagedSessionIds(),
            QStringList {QStringLiteral("auto-stage")});
    }

    void DesktopStateModelTest::stagingFocusAndUnstageMaintainSelection()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-stage-lifecycle"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        activateAccount(state, sessions, {}, 1);
        applySnapshot(
            sessions,
            {},
            1,
            QJsonArray {
                liveSession(QStringLiteral("one")),
                liveSession(QStringLiteral("two")),
            });

        QVERIFY(state.selectSession(QStringLiteral("one")));
        QVERIFY(state.stageSession(QStringLiteral("two")));
        QVERIFY(state.selectSession(QStringLiteral("two")));
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList({
                QStringLiteral("one"),
                QStringLiteral("two"),
            }));
        state.enterFocusMode();
        QCOMPARE(
            state.stageLayoutMode(),
            kodosi::DesktopStateModel::StageLayoutMode::Focus);
        QVERIFY(state.unstageSession(QStringLiteral("two")));
        QCOMPARE(state.selectedSessionId(), QStringLiteral("one"));
        QCOMPARE(
            state.stageLayoutMode(),
            kodosi::DesktopStateModel::StageLayoutMode::Grid);
        state.selectAdjacentSession(-1);
        QCOMPARE(state.selectedSessionId(), QStringLiteral("one"));
    }

    void DesktopStateModelTest::initialSnapshotAutoStagesAtMostSixLocalSessions()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-auto-stage"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        activateAccount(state, sessions, {}, 1);
        QJsonArray entries;
        for (auto index = 0; index < 8; ++index) {
            entries.append(liveSession(
                QStringLiteral("local-%1").arg(index)));
        }
        entries.append(liveSession(
            QStringLiteral("remote"),
            QStringLiteral("remote"),
            QStringLiteral("connected"),
            QStringLiteral("ready")));
        applySnapshot(sessions, {}, 1, entries);

        QCOMPARE(state.stagedSessionIds().size(), 6);
        for (const auto& sessionId : state.stagedSessionIds()) {
            QVERIFY(sessionId.startsWith(QStringLiteral("local-")));
        }
        QCOMPARE(
            state.selectedSessionId(),
            state.stagedSessionIds().constFirst());
    }

    void DesktopStateModelTest::reconciliationWaitsForAuthoritativeSnapshots()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-authoritative-only"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        activateAccount(state, sessions, {}, 1);
        applySnapshot(
            sessions,
            {},
            1,
            QJsonArray {liveSession(QStringLiteral("one"))});
        QCOMPARE(state.selectedSessionId(), QStringLiteral("one"));

        sessions.ingestSessionEvent(QByteArrayLiteral(
            R"({"authority":"accountContext","accountUserId":"","accountEpoch":1,"type":"session.removed","sessionId":"one"})"));
        QVERIFY(state.selectedSessionId().isEmpty());
        QVERIFY(state.stagedSessionIds().isEmpty());
    }

    void DesktopStateModelTest::
        selectionRequiresRetainableCatalogPresentation()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-selection-authority"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);

        QVERIFY(!state.selectSession(QStringLiteral("missing")));
        QVERIFY(!state.stageSession(QStringLiteral("missing")));

        activateAccount(state, sessions, {}, 1);
        applySnapshot(
            sessions,
            {},
            1,
            QJsonArray {
                liveSession(QStringLiteral("live")),
                QJsonObject {
                    {QStringLiteral("kind"), QStringLiteral("local")},
                    {QStringLiteral("id"), QStringLiteral("stopped")},
                    {QStringLiteral("incarnationId"),
                     QStringLiteral("incarnation-stopped")},
                    {QStringLiteral("name"), QStringLiteral("Stopped")},
                    {QStringLiteral("project"), QStringLiteral("/repo")},
                    {QStringLiteral("mode"), QStringLiteral("normal")},
                    {QStringLiteral("status"), QStringLiteral("stopped")},
                    {QStringLiteral("recovery"), QStringLiteral("resumable")},
                    {QStringLiteral("scope"), QStringLiteral("justMe")},
                    {QStringLiteral("access"), QStringLiteral("inject")},
                },
            });

        QVERIFY(state.selectSession(QStringLiteral("live")));
        QVERIFY(!state.selectSession(QStringLiteral("stopped")));
        QVERIFY(!state.stageSession(QStringLiteral("stopped")));
        QCOMPARE(state.selectedSessionId(), QStringLiteral("live"));
    }

    void DesktopStateModelTest::
        liveCatalogMutationsPruneStageAndExitFocus()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-live-pruning"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        activateAccount(state, sessions, {}, 1);
        applySnapshot(
            sessions,
            {},
            1,
            QJsonArray {
                liveSession(QStringLiteral("fallback")),
                liveSession(QStringLiteral("focused")),
            });
        QVERIFY(state.selectSession(QStringLiteral("fallback")));
        QVERIFY(state.selectSession(QStringLiteral("focused")));
        state.enterFocusMode();

        auto stopped = liveSession(QStringLiteral("focused"));
        stopped.insert(QStringLiteral("status"), QStringLiteral("stopped"));
        stopped.insert(
            QStringLiteral("recovery"),
            QStringLiteral("resumable"));
        applyUpsert(sessions, {}, 1, stopped);

        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("fallback")});
        QCOMPARE(state.selectedSessionId(), QStringLiteral("fallback"));
        QCOMPARE(
            state.stageLayoutMode(),
            kodosi::DesktopStateModel::StageLayoutMode::Grid);

        sessions.ingestSessionEvent(QByteArrayLiteral(
            R"({"authority":"accountContext","accountUserId":"","accountEpoch":1,"type":"session.removed","sessionId":"fallback"})"));
        QVERIFY(state.stagedSessionIds().isEmpty());
        QVERIFY(state.selectedSessionId().isEmpty());
    }

    void DesktopStateModelTest::
        liveUpsertKindControlsDeferredSelection()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-upsert-kind"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(QStringLiteral("changing"))});

        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            liveSession(
                QStringLiteral("changing"),
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready")));
        QVERIFY(state.selectSession(QStringLiteral("changing")));

        activateAccount(
            state,
            sessions,
            QStringLiteral("other"),
            2);
        applySnapshot(sessions, QStringLiteral("other"), 2, QJsonArray {});
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            3);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            3,
            QJsonArray {liveSession(
                QStringLiteral("changing"),
                QStringLiteral("remote"),
                QStringLiteral("connecting"),
                QStringLiteral("awaitingKey"))});

        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("changing")});
        QCOMPARE(state.selectedSessionId(), QStringLiteral("changing"));
    }

    void DesktopStateModelTest::remoteRestorationIsAccountScopedAndExact()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-account-remote"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        QSignalSpy restored(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested);
        connect(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested,
            &state,
            [&state](const QString& sessionId, const QString& incarnationId) {
                state.reportRemoteRestoreDispatch(
                    sessionId,
                    incarnationId,
                    true);
            },
            Qt::DirectConnection);

        activateAccount(state, sessions, QStringLiteral("account-a"), 1);
        applySnapshot(
            sessions,
            QStringLiteral("account-a"),
            1,
            QJsonArray {liveSession(
                QStringLiteral("remote-a"),
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready"))});
        QVERIFY(state.selectSession(QStringLiteral("remote-a")));

        activateAccount(state, sessions, QStringLiteral("account-b"), 2);
        applySnapshot(
            sessions,
            QStringLiteral("account-b"),
            2,
            QJsonArray {liveSession(
                QStringLiteral("remote-b"),
                QStringLiteral("remote"),
                QStringLiteral("connecting"),
                QStringLiteral("awaitingKey"))});
        QVERIFY(state.stagedSessionIds().isEmpty());
        QVERIFY(state.selectSession(QStringLiteral("remote-b")));

        activateAccount(state, sessions, QStringLiteral("account-a"), 3);
        applySnapshot(
            sessions,
            QStringLiteral("account-a"),
            3,
            QJsonArray {
                liveSession(
                    QStringLiteral("remote-a"),
                    QStringLiteral("remote"),
                    QStringLiteral("connecting"),
                    QStringLiteral("awaitingKey")),
                liveSession(
                    QStringLiteral("blocked"),
                    QStringLiteral("remote"),
                    QStringLiteral("offline"),
                    QStringLiteral("accessDenied")),
            });

        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("remote-a")});
        QCOMPARE(state.selectedSessionId(), QStringLiteral("remote-a"));
        QCOMPARE(restored.count(), 1);
        QCOMPARE(
            restored.constFirst().constFirst().toString(),
            QStringLiteral("remote-a"));

        restored.clear();
        activateAccount(state, sessions, {}, 4);
        applySnapshot(
            sessions,
            {},
            4,
            QJsonArray {liveSession(
                QStringLiteral("remote-signed-out"),
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready"))});
        QVERIFY(state.stagedSessionIds().isEmpty());
        QVERIFY(state.selectSession(
            QStringLiteral("remote-signed-out")));

        activateAccount(
            state,
            sessions,
            QStringLiteral("account-b"),
            5);
        applySnapshot(
            sessions,
            QStringLiteral("account-b"),
            5,
            QJsonArray {liveSession(
                QStringLiteral("remote-b"),
                QStringLiteral("remote"),
                QStringLiteral("connecting"),
                QStringLiteral("awaitingKey"))});
        restored.clear();

        activateAccount(state, sessions, {}, 6);
        applySnapshot(
            sessions,
            {},
            6,
            QJsonArray {liveSession(
                QStringLiteral("remote-signed-out"),
                QStringLiteral("remote"),
                QStringLiteral("connecting"),
                QStringLiteral("awaitingKey"))});
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList({
                QStringLiteral("remote-signed-out"),
            }));
        QCOMPARE(restored.count(), 1);
        QCOMPARE(
            restored.constFirst().constFirst().toString(),
            QStringLiteral("remote-signed-out"));
    }

    void DesktopStateModelTest::
        accountSwitchDoesNotCarryOverlappingRemoteStageIds()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-overlapping-account-remote"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);

        activateAccount(state, sessions, QStringLiteral("account-a"), 1);
        applySnapshot(
            sessions,
            QStringLiteral("account-a"),
            1,
            QJsonArray {
                liveSession(QStringLiteral("local")),
                liveSession(
                    QStringLiteral("overlap"),
                    QStringLiteral("remote"),
                    QStringLiteral("connected"),
                    QStringLiteral("ready")),
            });
        QVERIFY(state.selectSession(QStringLiteral("overlap")));
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList({
                QStringLiteral("local"),
                QStringLiteral("overlap"),
            }));

        activateAccount(state, sessions, QStringLiteral("account-b"), 2);
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("local")});
        applySnapshot(
            sessions,
            QStringLiteral("account-b"),
            2,
            QJsonArray {
                liveSession(QStringLiteral("local")),
                liveSession(
                    QStringLiteral("overlap"),
                    QStringLiteral("remote"),
                    QStringLiteral("connected"),
                    QStringLiteral("ready")),
            });
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("local")});
        QVERIFY(state.selectSession(QStringLiteral("overlap")));

        activateAccount(state, sessions, QStringLiteral("account-a"), 3);
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("local")});
        applySnapshot(
            sessions,
            QStringLiteral("account-a"),
            3,
            QJsonArray {
                liveSession(QStringLiteral("local")),
                liveSession(
                    QStringLiteral("overlap"),
                    QStringLiteral("remote"),
                    QStringLiteral("connected"),
                    QStringLiteral("ready")),
            });
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList({
                QStringLiteral("local"),
                QStringLiteral("overlap"),
            }));
        QCOMPARE(
            state.selectedSessionId(),
            QStringLiteral("overlap"));
    }

    void DesktopStateModelTest::
        firstAccountDoesNotInheritPersistedRemoteStage()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-first-account-isolation"));
        QVERIFY(directory.isValid());
        {
            kodosi::DesktopStateModel state(
                settingsFor(directory),
                desktopScreens);
            kodosi::SessionCatalogModel sessions;
            state.attachSessionCatalog(&sessions);
            activateAccount(
                state,
                sessions,
                QStringLiteral("account-a"),
                1);
            applySnapshot(
                sessions,
                QStringLiteral("account-a"),
                1,
                QJsonArray {
                    liveSession(QStringLiteral("local")),
                    liveSession(
                        QStringLiteral("shared"),
                        QStringLiteral("remote"),
                        QStringLiteral("connected"),
                        QStringLiteral("ready")),
                });
            QVERIFY(state.selectSession(QStringLiteral("shared")));
            QCOMPARE(
                state.stagedSessionIds(),
                QStringList({
                    QStringLiteral("local"),
                    QStringLiteral("shared"),
                }));
        }
        QCOMPARE(
            readStoredState(directory)
                .value(QStringLiteral("stage"))
                .toObject()
                .value(QStringLiteral("activeRemoteStageAccountKey"))
                .toString(),
            QStringLiteral("account-a"));

        kodosi::DesktopStateModel reloaded(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        reloaded.attachSessionCatalog(&sessions);
        activateAccount(
            reloaded,
            sessions,
            QStringLiteral("account-b"),
            1);
        QCOMPARE(
            reloaded.stagedSessionIds(),
            QStringList {QStringLiteral("local")});
        applySnapshot(
            sessions,
            QStringLiteral("account-b"),
            1,
            QJsonArray {
                liveSession(QStringLiteral("local")),
                liveSession(
                    QStringLiteral("shared"),
                    QStringLiteral("remote"),
                    QStringLiteral("connected"),
                    QStringLiteral("ready")),
            });
        QCOMPARE(
            reloaded.stagedSessionIds(),
            QStringList {QStringLiteral("local")});
        QCOMPARE(
            reloaded.selectedSessionId(),
            QStringLiteral("local"));
    }

    void DesktopStateModelTest::
        remoteRestorationRequiresAcceptanceAndCorrelatesCompletion()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-acknowledged-remote-restore"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        QSignalSpy restored(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested);
        bool acceptDispatch = false;
        connect(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested,
            &state,
            [&state, &acceptDispatch](
                const QString& sessionId,
                const QString& incarnationId) {
                state.reportRemoteRestoreDispatch(
                    sessionId,
                    incarnationId,
                    acceptDispatch);
            },
            Qt::DirectConnection);

        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(
                QStringLiteral("remote"),
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready"))});
        QVERIFY(state.selectSession(QStringLiteral("remote")));

        const auto connecting = liveSession(
            QStringLiteral("remote"),
            QStringLiteral("remote"),
            QStringLiteral("connecting"),
            QStringLiteral("awaitingKey"));
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            connecting);
        QCOMPARE(restored.count(), 1);
        QVERIFY(state.stagedSessionIds().isEmpty());

        acceptDispatch = true;
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            2);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            2,
            QJsonArray {connecting});
        QCOMPARE(restored.count(), 2);
        QCOMPARE(
            restored.constLast().at(1).toString(),
            QStringLiteral("incarnation-remote"));
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("remote")});

        applyUpsert(
            sessions,
            QStringLiteral("account"),
            2,
            connecting);
        QCOMPARE(restored.count(), 2);

        state.remoteOpenFailed(
            QStringLiteral("remote"),
            QStringLiteral("wrong-incarnation"));
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("remote")});
        state.remoteOpenFailed(
            QStringLiteral("remote"),
            QStringLiteral("incarnation-remote"));
        QVERIFY(state.stagedSessionIds().isEmpty());

        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            3);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            3,
            QJsonArray {connecting});
        QCOMPARE(restored.count(), 3);
        state.remoteOpenSucceeded(
            QStringLiteral("remote"),
            QStringLiteral("incarnation-remote"));
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            3,
            connecting);
        QCOMPARE(restored.count(), 3);

        state.resetRuntimeAuthority();
        sessions.resetRuntimeAuthority();
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {connecting});
        QCOMPARE(restored.count(), 4);
    }

    void DesktopStateModelTest::
        pendingExplicitRemoteOpenCoalescesAutomaticRestore_data()
    {
        QTest::addColumn<bool>("succeeds");
        QTest::newRow("success") << true;
        QTest::newRow("failure") << false;
    }

    void DesktopStateModelTest::
        pendingExplicitRemoteOpenCoalescesAutomaticRestore()
    {
        QFETCH(bool, succeeds);
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-explicit-remote-%1")
                .arg(succeeds ? QStringLiteral("success")
                              : QStringLiteral("failure")));
        QVERIFY(directory.isValid());
        {
            kodosi::DesktopStateModel state(
                settingsFor(directory),
                desktopScreens);
            kodosi::SessionCatalogModel sessions;
            state.attachSessionCatalog(&sessions);
            activateAccount(
                state,
                sessions,
                QStringLiteral("account"),
                1);
            applySnapshot(
                sessions,
                QStringLiteral("account"),
                1,
                QJsonArray {liveSession(
                    QStringLiteral("remote"),
                    QStringLiteral("remote"),
                    QStringLiteral("connected"),
                    QStringLiteral("ready"))});
            QVERIFY(state.selectSession(QStringLiteral("remote")));
        }

        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(
                QStringLiteral("remote"),
                QStringLiteral("remote"),
                QStringLiteral("offline"),
                QStringLiteral("ready"))});
        FakeSessionDispatcher dispatcher;
        kodosi::SessionActions actions(dispatcher, sessions);
        actions.ingestAuthEvent(QByteArrayLiteral(
            R"({"type":"auth.ready","userId":"account","accountEpoch":1})"));
        connect(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested,
            &actions,
            [&state, &actions](
                const QString& sessionId,
                const QString& incarnationId) {
                state.reportRemoteRestoreDispatch(
                    sessionId,
                    incarnationId,
                    actions.restoreRemote(sessionId));
            },
            Qt::DirectConnection);
        connect(
            &actions,
            &kodosi::SessionActions::remoteOpenSucceeded,
            &state,
            &kodosi::DesktopStateModel::remoteOpenSucceeded);
        connect(
            &actions,
            &kodosi::SessionActions::remoteOpenFailed,
            &state,
            &kodosi::DesktopStateModel::remoteOpenFailed);
        QSignalSpy restoreRequested(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested);

        QVERIFY(actions.openRemote(QStringLiteral("remote")));
        QCOMPARE(
            commandCount(
                dispatcher,
                QStringLiteral("session.openRemote")),
            1);
        state.attachSessionCatalog(&sessions);
        QCOMPARE(restoreRequested.count(), 1);
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("remote")});
        QCOMPARE(
            commandCount(
                dispatcher,
                QStringLiteral("session.openRemote")),
            1);

        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            liveSession(QStringLiteral("unrelated")));
        QCOMPARE(restoreRequested.count(), 1);
        QCOMPARE(
            commandCount(
                dispatcher,
                QStringLiteral("session.openRemote")),
            1);

        const auto outcome = succeeds
            ? QJsonObject {
                  {QStringLiteral("authority"),
                   QStringLiteral("accountContext")},
                  {QStringLiteral("accountUserId"),
                   QStringLiteral("account")},
                  {QStringLiteral("accountEpoch"), 1},
                  {QStringLiteral("type"),
                   QStringLiteral("session.opened")},
                  {QStringLiteral("sessionId"),
                   QStringLiteral("remote")},
              }
            : QJsonObject {
                  {QStringLiteral("authority"),
                   QStringLiteral("accountContext")},
                  {QStringLiteral("accountUserId"),
                   QStringLiteral("account")},
                  {QStringLiteral("accountEpoch"), 1},
                  {QStringLiteral("type"),
                   QStringLiteral("session.error")},
                  {QStringLiteral("operation"),
                   QStringLiteral("session.openRemote")},
                  {QStringLiteral("sessionId"),
                   QStringLiteral("remote")},
                  {QStringLiteral("message"),
                   QStringLiteral("open denied")},
              };
        actions.ingestSessionEvent(
            QJsonDocument(outcome).toJson(QJsonDocument::Compact));
        QCOMPARE(
            state.stagedSessionIds(),
            succeeds
                ? QStringList {QStringLiteral("remote")}
                : QStringList {});

        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            liveSession(QStringLiteral("another")));
        QCOMPARE(restoreRequested.count(), 1);
        QCOMPARE(
            commandCount(
                dispatcher,
                QStringLiteral("session.openRemote")),
            1);
    }

    void DesktopStateModelTest::
        reincarnatedRemoteRestoreKeepsCurrentIdentityEligible()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-reincarnated-remote-restore"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(
                QStringLiteral("remote"),
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready"),
                1,
                QStringLiteral("incarnation-a"))});
        QVERIFY(state.selectSession(QStringLiteral("remote")));

        FakeSessionDispatcher dispatcher;
        kodosi::SessionActions actions(dispatcher, sessions);
        actions.ingestAuthEvent(QByteArrayLiteral(
            R"({"type":"auth.ready","userId":"account","accountEpoch":1})"));
        connect(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested,
            &actions,
            [&state, &actions](
                const QString& sessionId,
                const QString& incarnationId) {
                state.reportRemoteRestoreDispatch(
                    sessionId,
                    incarnationId,
                    actions.restoreRemote(sessionId));
            },
            Qt::DirectConnection);
        connect(
            &actions,
            &kodosi::SessionActions::remoteOpenSucceeded,
            &state,
            &kodosi::DesktopStateModel::remoteOpenSucceeded);
        connect(
            &actions,
            &kodosi::SessionActions::remoteOpenFailed,
            &state,
            &kodosi::DesktopStateModel::remoteOpenFailed);
        QSignalSpy restoreRequested(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested);
        QSignalSpy openFailed(
            &actions,
            &kodosi::SessionActions::remoteOpenFailed);

        const auto remoteUpsert = [](const QString& incarnation) {
            return QJsonDocument(QJsonObject {
                {QStringLiteral("authority"),
                 QStringLiteral("accountContext")},
                {QStringLiteral("accountUserId"),
                 QStringLiteral("account")},
                {QStringLiteral("accountEpoch"), 1},
                {QStringLiteral("type"),
                 QStringLiteral("session.upsert")},
                {QStringLiteral("session"),
                 liveSession(
                     QStringLiteral("remote"),
                     QStringLiteral("remote"),
                     QStringLiteral("offline"),
                     QStringLiteral("ready"),
                     1,
                     incarnation)},
            }).toJson(QJsonDocument::Compact);
        };

        const auto incarnationA =
            QStringLiteral("incarnation-a");
        const auto incarnationB =
            QStringLiteral("incarnation-b");
        const auto offlineA = remoteUpsert(incarnationA);
        sessions.ingestSessionEvent(offlineA);
        actions.ingestSessionEvent(offlineA);
        QCOMPARE(restoreRequested.count(), 1);
        QCOMPARE(
            commandCount(
                dispatcher,
                QStringLiteral("session.openRemote")),
            1);

        const auto offlineB = remoteUpsert(incarnationB);
        sessions.ingestSessionEvent(offlineB);
        actions.ingestSessionEvent(offlineB);

        QCOMPARE(openFailed.count(), 1);
        QCOMPARE(openFailed.constFirst().at(1).toString(), incarnationA);
        QCOMPARE(restoreRequested.count(), 2);
        QCOMPARE(
            restoreRequested.constLast().at(1).toString(),
            incarnationB);
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("remote")});
        QCOMPARE(
            commandCount(
                dispatcher,
                QStringLiteral("session.openRemote")),
            2);
    }

    void DesktopStateModelTest::
        stageReadyRemoteDoesNotDispatchAutomaticRestore()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-stage-ready-remote"));
        QVERIFY(directory.isValid());
        {
            kodosi::DesktopStateModel state(
                settingsFor(directory),
                desktopScreens);
            kodosi::SessionCatalogModel sessions;
            state.attachSessionCatalog(&sessions);
            activateAccount(
                state,
                sessions,
                QStringLiteral("account"),
                1);
            applySnapshot(
                sessions,
                QStringLiteral("account"),
                1,
                QJsonArray {liveSession(
                    QStringLiteral("remote"),
                    QStringLiteral("remote"),
                    QStringLiteral("connected"),
                    QStringLiteral("ready"))});
            QVERIFY(state.selectSession(QStringLiteral("remote")));
        }

        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(
                QStringLiteral("remote"),
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready"))});
        FakeSessionDispatcher dispatcher;
        kodosi::SessionActions actions(dispatcher, sessions);
        QSignalSpy restoreRequested(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested);
        connect(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested,
            &actions,
            [&state, &actions](
                const QString& sessionId,
                const QString& incarnationId) {
                state.reportRemoteRestoreDispatch(
                    sessionId,
                    incarnationId,
                    actions.restoreRemote(sessionId));
            },
            Qt::DirectConnection);

        state.attachSessionCatalog(&sessions);

        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("remote")});
        QCOMPARE(restoreRequested.count(), 0);
        QCOMPARE(
            commandCount(
                dispatcher,
                QStringLiteral("session.openRemote")),
            0);
    }

    void DesktopStateModelTest::
        failedRemoteRestoreIsSuppressedWithinAuthorityGeneration()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-restore-suppression"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        QSignalSpy restored(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested);
        bool acceptDispatch = false;
        connect(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested,
            &state,
            [&state, &acceptDispatch](
                const QString& sessionId,
                const QString& incarnationId) {
                state.reportRemoteRestoreDispatch(
                    sessionId,
                    incarnationId,
                    acceptDispatch);
            },
            Qt::DirectConnection);

        const auto connecting = liveSession(
            QStringLiteral("remote"),
            QStringLiteral("remote"),
            QStringLiteral("connecting"),
            QStringLiteral("awaitingKey"));
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(
                QStringLiteral("remote"),
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready"))});
        QVERIFY(state.selectSession(QStringLiteral("remote")));

        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            connecting);
        QCOMPARE(restored.count(), 1);
        QVERIFY(state.stagedSessionIds().isEmpty());
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            liveSession(QStringLiteral("unrelated")));
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            connecting);
        QCOMPARE(restored.count(), 1);
        QVERIFY(state.stagedSessionIds().isEmpty());

        acceptDispatch = true;
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            2);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            2,
            QJsonArray {connecting});
        QCOMPARE(restored.count(), 2);
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("remote")});
        state.remoteOpenFailed(
            QStringLiteral("remote"),
            QStringLiteral("incarnation-remote"));
        QVERIFY(state.stagedSessionIds().isEmpty());
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            2,
            liveSession(QStringLiteral("other")));
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            2,
            connecting);
        QCOMPARE(restored.count(), 2);
        QVERIFY(state.stagedSessionIds().isEmpty());

        const auto reincarnated = liveSession(
            QStringLiteral("remote"),
            QStringLiteral("remote"),
            QStringLiteral("connecting"),
            QStringLiteral("awaitingKey"),
            1,
            QStringLiteral("incarnation-remote-2"));
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            2,
            reincarnated);
        QCOMPARE(restored.count(), 3);
        QCOMPARE(
            restored.constLast().at(1).toString(),
            QStringLiteral("incarnation-remote-2"));
    }

    void DesktopStateModelTest::
        remoteRestorationRepeatsForFreshRuntimeAuthority()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-runtime-remote-restore"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        QSignalSpy restored(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested);
        connect(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested,
            &state,
            [&state](const QString& sessionId, const QString& incarnationId) {
                state.reportRemoteRestoreDispatch(
                    sessionId,
                    incarnationId,
                    true);
            },
            Qt::DirectConnection);

        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(
                QStringLiteral("remote"),
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready"))});
        QVERIFY(state.selectSession(QStringLiteral("remote")));

        state.resetRuntimeAuthority();
        sessions.resetRuntimeAuthority();
        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(
                QStringLiteral("remote"),
                QStringLiteral("remote"),
                QStringLiteral("connecting"),
                QStringLiteral("awaitingKey"))});

        QCOMPARE(restored.count(), 1);
        QCOMPARE(
            restored.constFirst().constFirst().toString(),
            QStringLiteral("remote"));
        QCOMPARE(
            state.stagedSessionIds(),
            QStringList {QStringLiteral("remote")});
    }

    void DesktopStateModelTest::
        remoteRestorationTracksConnectableUpsertsOnce()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-upsert-remote-restore"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        QSignalSpy restored(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested);
        connect(
            &state,
            &kodosi::DesktopStateModel::remoteRestoreRequested,
            &state,
            [&state](const QString& sessionId, const QString& incarnationId) {
                state.reportRemoteRestoreDispatch(
                    sessionId,
                    incarnationId,
                    true);
            },
            Qt::DirectConnection);

        activateAccount(
            state,
            sessions,
            QStringLiteral("account"),
            1);
        applySnapshot(
            sessions,
            QStringLiteral("account"),
            1,
            QJsonArray {liveSession(QStringLiteral("upserted"))});
        QVERIFY(state.selectSession(QStringLiteral("upserted")));

        const auto remote = liveSession(
            QStringLiteral("upserted"),
            QStringLiteral("remote"),
            QStringLiteral("connecting"),
            QStringLiteral("awaitingKey"));
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            remote);
        applyUpsert(
            sessions,
            QStringLiteral("account"),
            1,
            remote);

        QCOMPARE(restored.count(), 1);
        QCOMPARE(
            restored.constFirst().constFirst().toString(),
            QStringLiteral("upserted"));
    }

    void DesktopStateModelTest::
        explicitStageMutationsAreNotRestorationCapped()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-explicit-stage-budget"));
        QVERIFY(directory.isValid());
        kodosi::DesktopStateModel state(
            settingsFor(directory),
            desktopScreens);
        kodosi::SessionCatalogModel sessions;
        state.attachSessionCatalog(&sessions);
        activateAccount(state, sessions, {}, 1);
        QJsonArray entries;
        for (auto index = 0; index < 7; ++index) {
            entries.append(liveSession(
                QStringLiteral("local-%1").arg(index)));
        }
        applySnapshot(sessions, {}, 1, entries);
        QCOMPARE(state.stagedSessionIds().size(), 6);

        QVERIFY(state.stageSession(QStringLiteral("local-6")));
        QCOMPARE(state.stagedSessionIds().size(), 7);
        QVERIFY(state.selectSession(QStringLiteral("local-6")));
        QCOMPARE(state.selectedSessionId(), QStringLiteral("local-6"));
        QCOMPARE(
            readStoredState(directory)
                .value(QStringLiteral("stage"))
                .toObject()
                .value(QStringLiteral("stagedSessionIds"))
                .toArray()
                .size(),
            7);
    }

    void DesktopStateModelTest::
        stageLayoutModeIsTransientAcrossRestart()
    {
        auto directory = stateDirectory(
            QStringLiteral("desktop-state-transient-layout"));
        QVERIFY(directory.isValid());
        {
            kodosi::DesktopStateModel state(
                settingsFor(directory),
                desktopScreens);
            kodosi::SessionCatalogModel sessions;
            state.attachSessionCatalog(&sessions);
            activateAccount(state, sessions, {}, 1);
            applySnapshot(
                sessions,
                {},
                1,
                QJsonArray {liveSession(QStringLiteral("local"))});
            state.enterFocusMode(QStringLiteral("local"));
            QCOMPARE(
                state.stageLayoutMode(),
                kodosi::DesktopStateModel::StageLayoutMode::Focus);
            QCOMPARE(
                readStoredState(directory)
                    .value(QStringLiteral("stage"))
                    .toObject()
                    .value(QStringLiteral("layoutMode"))
                    .toString(),
                QStringLiteral("grid"));
        }
        {
            auto stored = readStoredState(directory);
            auto stage =
                stored.value(QStringLiteral("stage")).toObject();
            stage.insert(
                QStringLiteral("layoutMode"),
                QStringLiteral("focus"));
            stored.insert(QStringLiteral("stage"), stage);
            auto storage = settingsFor(directory);
            storage->setValue(
                QString::fromLatin1(currentStateKey),
                QJsonDocument(stored).toJson(
                    QJsonDocument::Compact));
            storage->sync();
        }

        kodosi::DesktopStateModel reloaded(
            settingsFor(directory),
            desktopScreens);
        QCOMPARE(
            reloaded.stageLayoutMode(),
            kodosi::DesktopStateModel::StageLayoutMode::Grid);
    }

QTEST_MAIN(DesktopStateModelTest)

#include "tst_desktop_state_model.moc"
