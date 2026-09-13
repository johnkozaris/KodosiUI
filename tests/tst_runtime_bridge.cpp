#include "RuntimeApiFixture.hpp"
#include "SessionFixture.hpp"
#include "bridge/RuntimeBridge.hpp"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest/QTest>

class RuntimeBridgeTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        runtime_fixture::reset();
        runtime_fixture::allowStart = true;
    }
    void asynchronousStartupCanBeCancelledBeforeInstallation()
    {
        kodosi::RuntimeBridge bridge;
        bool completed = false;
        bridge.startAsync([&](auto) { completed = true; });
        bridge.stop();
        QTest::qWait(100);
        QVERIFY(!bridge.isRunning());
        QVERIFY(!completed);
        bridge.startAsync([&](auto result) { completed = result.has_value(); });
        QTRY_VERIFY(completed);
        QVERIFY(bridge.isRunning());
    }
    void fencesAccountsAndInjectsCommandEnvelope()
    {
        kodosi::RuntimeBridge bridge;
        QSignalSpy events(&bridge, &kodosi::RuntimeBridge::eventReceived);
        QVERIFY(bridge.start());
        auto event = [](const QString& type, int epoch, const QString& user) {
            return QJsonDocument(
                QJsonObject { { QStringLiteral("type"), type }, { QStringLiteral("accountEpoch"), epoch },
                    { QStringLiteral("accountUserId"), user } })
                .toJson(QJsonDocument::Compact);
        };
        runtime_fixture::sendEvent(event(QStringLiteral("auth.ready"), 1, test::id(1)));
        QTRY_COMPARE(events.size(), 1);
        runtime_fixture::sendEvent(event(QStringLiteral("sessions.snapshot"), 0, test::id(1)));
        runtime_fixture::sendEvent(event(QStringLiteral("sessions.snapshot"), 1, test::id(2)));
        runtime_fixture::sendEvent(event(QStringLiteral("sessions.snapshot"), 2, test::id(2)));
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 1);
        QVERIFY(bridge.send(QByteArrayLiteral("{\"type\":\"session.list\"}")));
        const auto command = QJsonDocument::fromJson(runtime_fixture::lastCommand).object();
        QCOMPARE(command.value(QStringLiteral("accountUserId")).toString(), test::id(1));
        QCOMPARE(command.value(QStringLiteral("accountEpoch")).toInt(), 1);
        runtime_fixture::sendEvent(event(QStringLiteral("auth.ready"), 2, test::id(2)));
        QTRY_COMPARE(events.size(), 2);
    }
    void stoppedGenerationDropsQueuedCallbacks()
    {
        kodosi::RuntimeBridge bridge;
        QSignalSpy events(&bridge, &kodosi::RuntimeBridge::eventReceived);
        QVERIFY(bridge.start());
        runtime_fixture::sendEvent(
            QByteArrayLiteral("{\"type\":\"system.ready\",\"accountEpoch\":0,\"accountUserId\":null}"));
        bridge.stop();
        QCoreApplication::processEvents();
        QVERIFY(events.isEmpty());
        QVERIFY(!bridge.send(QByteArrayLiteral("{\"type\":\"session.list\"}")));
    }
    void boundsSmallQueuedEventsAndReportsOnce()
    {
        kodosi::RuntimeBridge bridge;
        QSignalSpy errors(&bridge,&kodosi::RuntimeBridge::eventError);
        QVERIFY(bridge.start());
        const auto event=QByteArrayLiteral("{\"type\":\"system.ready\",\"accountEpoch\":0,\"accountUserId\":null}");
        for(int i=0;i<10000;++i)runtime_fixture::sendEvent(event);
        QTRY_VERIFY(!bridge.isRunning());
        QCOMPARE(errors.size(),1);
    }
    void admissionErrorsAreNotCompletion()
    {
        kodosi::RuntimeBridge bridge;
        QVERIFY(bridge.start());
        runtime_fixture::commandResult = KODOSI_FFI_BUSY;
        QVERIFY(!bridge.send(QByteArrayLiteral("{\"type\":\"session.list\"}")));
    }
};
QTEST_GUILESS_MAIN(RuntimeBridgeTest)
#include "tst_runtime_bridge.moc"
