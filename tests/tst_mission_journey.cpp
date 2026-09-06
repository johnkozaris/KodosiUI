#include "models/AppearanceModel.hpp"
#include "models/DesktopSettings.hpp"
#include "models/MissionActions.hpp"
#include "models/SteeringModel.hpp"
#include "terminal/GhosttyC.hpp"
#include "terminal/TerminalSurfaceController.hpp"
#include "terminal/TerminalView.hpp"

#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <memory>

namespace {

class RecordingRuntime final : public kodosi::RuntimeBridge {
public:
    bool isRunning() const noexcept override { return true; }
    Result send(kodosi::CommandLane, QByteArrayView json) override
    {
        commands.append(QJsonDocument::fromJson(json.toByteArray()).object());
        return {};
    }
    Result connectTerminal(const kodosi::TerminalSubscription& subscription) override
    {
        ++connections;
        subscriptions.append(subscription);
        return {};
    }
    Result refreshTerminal(const kodosi::TerminalSubscription&) override { return {}; }
    Result disconnectTerminal(const kodosi::TerminalSubscription&) override
    {
        ++disconnections;
        return {};
    }
    QJsonObject last(const QString& type) const
    {
        for (auto it = commands.crbegin(); it != commands.crend(); ++it) {
            if (it->value(QStringLiteral("type")) == type) return *it;
        }
        return {};
    }
    QVector<QJsonObject> commands;
    QVector<kodosi::TerminalSubscription> subscriptions;
    int connections = 0;
    int disconnections = 0;
};

QByteArray checkpoint()
{
    GhosttyTerminal terminal = nullptr;
    if (ghostty_terminal_new(nullptr, &terminal, 120, 40) != GHOSTTY_SUCCESS)
        return {};
    const auto bytes = QByteArrayLiteral("LEFT\x1b[1;116HRIGHT\x1b[40;114HBOTTOM");
    ghostty_terminal_vt_write(terminal, reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()));
    const GhosttyCheckpointEncodeOptions options = GHOSTTY_CHECKPOINT_ENCODE_OPTIONS_INIT;
    GhosttyBuffer buffer {};
    GhosttyCheckpointInfo info = GHOSTTY_INIT_SIZED(GhosttyCheckpointInfo);
    if (ghostty_checkpoint_encode_buf(terminal, &options, &buffer, &info) != GHOSTTY_OUT_OF_SPACE) {
        ghostty_terminal_free(terminal);
        return {};
    }
    QByteArray output(static_cast<qsizetype>(buffer.len), Qt::Uninitialized);
    buffer.ptr = reinterpret_cast<std::uint8_t*>(output.data());
    buffer.cap = static_cast<std::size_t>(output.size());
    buffer.len = 0;
    const auto result = ghostty_checkpoint_encode_buf(terminal, &options, &buffer, &info);
    ghostty_terminal_free(terminal);
    return result == GHOSTTY_SUCCESS ? output : QByteArray {};
}

QByteArray accountEvent(QJsonObject value)
{
    value.insert(QStringLiteral("authority"), QStringLiteral("accountContext"));
    value.insert(QStringLiteral("accountUserId"), QStringLiteral("me"));
    value.insert(QStringLiteral("accountEpoch"), 1);
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}

} // namespace

class MissionJourneyTest final : public QObject {
    Q_OBJECT
private slots:
    void metadataAndOtherInspectorDoNotResetMissionFocus();
};

void MissionJourneyTest::metadataAndOtherInspectorDoNotResetMissionFocus()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    RecordingRuntime runtime;
    kodosi::MissionDirectoryModel missions(runtime);
    kodosi::PeopleModel people;
    kodosi::SessionCatalogModel sessions;
    kodosi::SteeringModel steering(runtime, sessions);
    kodosi::MissionDetailModel detail(runtime, missions,
        {.people = &people, .sessions = &sessions, .steering = &steering});
    kodosi::MissionActions actions(runtime, missions, detail, people, sessions);
    kodosi::TerminalSessionRegistry registry;
    kodosi::TerminalSurfaceController surfaces(registry, runtime, sessions);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    kodosi::DesktopSettings settings(std::make_unique<QSettings>(temporary.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat));
    kodosi::AppearanceModel appearance(std::make_unique<QSettings>(temporary.filePath(QStringLiteral("appearance.ini")), QSettings::IniFormat),
        QDBusConnection(QStringLiteral("mission-journey")),
        {.colorScheme = [] { return Qt::ColorScheme::Dark; },
         .setColorScheme = [](Qt::ColorScheme) {}, .unsetColorScheme = [] {}}, false);
    appearance.injectReducedMotionForTesting(true);
    const auto auth = QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}");
    missions.ingestAuthEvent(auth);
    people.ingestAuthEvent(auth);
    sessions.ingestAuthEvent(auth);
    steering.ingestAuthEvent(auth);
    detail.ingestAuthEvent(auth);
    actions.ingestAuthEvent(auth);
    missions.ingestRoomEvent(accountEvent({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"), QJsonArray {QJsonObject {
            {QStringLiteral("id"), QStringLiteral("mission")}, {QStringLiteral("name"), QStringLiteral("Ship")},
            {QStringLiteral("slug"), QStringLiteral("ship")}, {QStringLiteral("ownerUserId"), QStringLiteral("me")},
            {QStringLiteral("rosterGeneration"), 1},
        }}},
    }));
    const auto incarnation = QStringLiteral("01900000-0000-7000-8000-000000000123");
    QJsonObject session {
        {QStringLiteral("kind"), QStringLiteral("local")}, {QStringLiteral("id"), QStringLiteral("agent")},
        {QStringLiteral("incarnationId"), incarnation}, {QStringLiteral("name"), QStringLiteral("Builder")},
        {QStringLiteral("project"), QStringLiteral("/repo")}, {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), QStringLiteral("active")}, {QStringLiteral("recovery"), QStringLiteral("live")},
        {QStringLiteral("scope"), QStringLiteral("room")}, {QStringLiteral("access"), QStringLiteral("approve")},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
        {QStringLiteral("semanticActions"), QJsonObject {{QStringLiteral("queue"), false}, {QStringLiteral("steer"), true}, {QStringLiteral("stopAndSend"), false}}},
    };
    sessions.ingestSessionEvent(accountEvent({{QStringLiteral("type"), QStringLiteral("session.list")}, {QStringLiteral("sessions"), QJsonArray {session}}}));
    QVERIFY(detail.openMission(QStringLiteral("mission")));
    const auto presentation = detail.crew()->data(detail.crew()->index(0), kodosi::MissionCrewModel::PresentationIdRole).toString();
    QVERIFY(!presentation.isEmpty());

    const auto qml = temporary.filePath(QStringLiteral("qml"));
    QVERIFY(QDir().mkpath(qml));
    const QDir source(QStringLiteral(KODOSI_SOURCE_DIR "/src/qml"));
    for (const auto& directory : {QStringLiteral("Controls"), QStringLiteral("Theme")}) {
        const QDir folder(source.filePath(directory));
        for (const auto& file : folder.entryList({QStringLiteral("*.qml")}, QDir::Files)) {
            QVERIFY(QFile::copy(folder.filePath(file), QDir(qml).filePath(file)));
        }
    }
    for (const auto& relative : {QStringLiteral("People/MissionDetailView.qml"), QStringLiteral("People/TaskDueDateField.qml"),
             QStringLiteral("Workbench/TerminalViewportControls.qml")}) {
        QVERIFY(QFile::copy(source.filePath(relative), QDir(qml).filePath(QFileInfo(relative).fileName())));
    }
    QFile directory(QDir(qml).filePath(QStringLiteral("qmldir")));
    QVERIFY(directory.open(QIODevice::WriteOnly));
    QVERIFY(directory.write("singleton KodosiTheme 1.0 KodosiTheme.qml\n") > 0);
    directory.close();
    qmlRegisterType<kodosi::TerminalView>("Kodosi.Models", 1, 0, "TerminalView");
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "Missions", &missions);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "MissionDetail", &detail);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "MissionActions", &actions);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "People", &people);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "Steering", &steering);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "TerminalSurfaces", &surfaces);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "DesktopSettings", &settings);
    qmlRegisterSingletonInstance("Kodosi.Models", 1, 0, "Appearance", &appearance);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import "."
        ApplicationWindow {
            width: 1200; height: 900; visible: true
            MissionDetailView { anchors.fill: parent; missionId: "mission"; missionName: "Ship"; page: 2 }
        }
    )", QUrl::fromLocalFile(QDir(qml).filePath(QStringLiteral("journey.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(window);
    QCoreApplication::processEvents();
    QVERIFY(detail.selectCrew(presentation));
    QTRY_VERIFY(runtime.connections > 0);
    const auto query = runtime.last(QStringLiteral("agent.intel.querySteer"));
    QVERIFY(!query.isEmpty());
    steering.ingestAgentIntelEvent(accountEvent({
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), query.value(QStringLiteral("requestId"))},
        {QStringLiteral("payload"), QJsonArray {}},
    }));
    auto* toggle = root->findChild<QQuickItem*>(QStringLiteral("missions.focus.steer"));
    auto* draft = root->findChild<QQuickItem*>(QStringLiteral("missions.focus.steer.text"));
    auto* send = root->findChild<QQuickItem*>(QStringLiteral("missions.focus.steer.send"));
    QVERIFY(toggle && draft && send);
    const auto onScreen = [window](const QQuickItem* item) {
        return item->isVisible() && item->width() > 0 && item->height() > 0
            && QRectF(QPointF(0, 0), window->size()).contains(item->mapRectToScene(item->boundingRect()));
    };
    QTRY_VERIFY_WITH_TIMEOUT(onScreen(toggle), 1'000);
    QVERIFY(toggle->isEnabled());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(onScreen(draft), 1'000);
    QVERIFY(steering.saveDraft(QStringLiteral("agent"), QStringLiteral("Keep this instruction"), QStringLiteral("steer")));
    QTRY_COMPARE(draft->property("text").toString(), QStringLiteral("Keep this instruction"));
    QTRY_VERIFY(send->isEnabled());
    const auto connections = runtime.connections;
    const auto disconnections = runtime.disconnections;
    QSignalSpy focusChanges(&detail, &kodosi::MissionDetailModel::focusChanged);
    session.insert(QStringLiteral("name"), QStringLiteral("Renamed builder"));
    sessions.ingestSessionEvent(accountEvent({{QStringLiteral("type"), QStringLiteral("session.upsert")}, {QStringLiteral("session"), session}}));
    QCoreApplication::processEvents();
    QCOMPARE(detail.selectedSessionDisplayName(), QStringLiteral("Renamed builder"));
    QCOMPARE(focusChanges.count(), 0);
    QCOMPARE(runtime.connections, connections);
    QCOMPARE(runtime.disconnections, disconnections);
    QVERIFY(draft->isVisible());
    QCOMPARE(draft->property("text").toString(), QStringLiteral("Keep this instruction"));
    QVERIFY(steering.inspect(QStringLiteral("agent")));
    steering.clearInspection();
    QCoreApplication::processEvents();
    QVERIFY(draft->isVisible());
    QCOMPARE(draft->property("text").toString(), QStringLiteral("Keep this instruction"));
    QVERIFY(send->isEnabled());
    QCOMPARE(runtime.connections, connections);
    QCOMPARE(runtime.disconnections, disconnections);

    auto* terminal = root->findChild<kodosi::TerminalView*>(QStringLiteral("missions.focus.terminal.native"));
    QVERIFY(terminal);
    QVERIFY(registry.installSemanticCheckpoint({runtime.subscriptions.constLast(), 1, 40, 120, checkpoint()}));
    registry.receiveConnectResult({runtime.subscriptions.constLast(), 0});
    QTRY_VERIFY(terminal->terminalReady());
    window->resize(820, 500);
    QTRY_VERIFY_WITH_TIMEOUT(onScreen(toggle) && onScreen(send) && onScreen(draft), 1'000);
    QVERIFY(terminal->height() > 150);

    session.insert(QStringLiteral("kind"), QStringLiteral("remote"));
    session.insert(QStringLiteral("owner"), QStringLiteral("Other device"));
    session.insert(QStringLiteral("ownerUserId"), QStringLiteral("owner"));
    session.insert(QStringLiteral("connectionState"), QStringLiteral("connected"));
    session.insert(QStringLiteral("accessState"), QStringLiteral("ready"));
    session.insert(QStringLiteral("permissions"), 0x1ff);
    sessions.ingestSessionEvent(accountEvent({{QStringLiteral("type"), QStringLiteral("session.upsert")}, {QStringLiteral("session"), session}}));
    QTRY_VERIFY(!terminal->canResize());
    auto* fit = root->findChild<QQuickItem*>(QStringLiteral("missions.focus.viewport.fit"));
    auto* horizontal = root->findChild<QQuickItem*>(QStringLiteral("missions.focus.viewport.horizontal"));
    auto* vertical = root->findChild<QQuickItem*>(QStringLiteral("missions.focus.viewport.vertical"));
    QVERIFY(fit && horizontal && vertical);
    QTRY_VERIFY_WITH_TIMEOUT(onScreen(fit) && onScreen(toggle) && onScreen(send), 1'000);
    const auto text = terminal->accessibleText();
    const auto right = static_cast<int>(text.indexOf(QStringLiteral("RIGHT")));
    const auto bottom = static_cast<int>(text.indexOf(QStringLiteral("BOTTOM")));
    QVERIFY(right >= 0 && bottom >= 0);
    const auto contentRect = terminal->mapRectToScene(terminal->boundingRect());
    QVERIFY(!contentRect.intersects(fit->mapRectToScene(fit->boundingRect())));
    const auto terminalGlobal = QRect(window->mapToGlobal(contentRect.topLeft().toPoint()), contentRect.size().toSize());
    QVERIFY(terminalGlobal.contains(terminal->accessibleCharacterRect(right + 4).center()));
    QVERIFY(terminalGlobal.contains(terminal->accessibleCharacterRect(bottom + 5).center()));
    const auto fittedGlyphsVisible = [&] {
        const auto image = window->grabWindow();
        const auto ratio = image.devicePixelRatio();
        for (int offset = 0; offset < 4; ++offset) {
            const auto character = terminal->accessibleCharacterRect(offset);
            const auto origin = window->mapFromGlobal(character.topLeft());
            const QRect pixels(QPoint(qRound(origin.x() * ratio), qRound(origin.y() * ratio)),
                QSize(qRound(character.width() * ratio), qRound(character.height() * ratio)));
            const auto clipped = pixels.intersected(image.rect());
            bool visible = false;
            for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
                for (int x = clipped.left(); x <= clipped.right(); ++x) {
                    visible |= image.pixelColor(x, y).lightness() > 30;
                }
            }
            if (!visible) return false;
        }
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(fittedGlyphsVisible(), 1'000);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        fit->mapToScene(QPointF(fit->width() / 2, fit->height() / 2)).toPoint());
    QTRY_VERIFY(!terminal->fitToView());
    QTRY_VERIFY(onScreen(horizontal) && onScreen(vertical));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        horizontal->mapToScene(QPointF(horizontal->width() - 4, horizontal->height() / 2)).toPoint());
    QTRY_VERIFY(terminal->panX() > 0);
    terminal->setFitToView(true);
    QTRY_VERIFY_WITH_TIMEOUT(onScreen(toggle) && onScreen(send) && onScreen(draft), 1'000);
}

QTEST_MAIN(MissionJourneyTest)
#include "tst_mission_journey.moc"
