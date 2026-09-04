#include "models/DeviceActions.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

class FakeDeviceDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    bool reject = false;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (lane != kodosi::CommandLane::Devices || !document.isObject() || reject) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        commands.push_back(document.object());
        return {};
    }
};

class DeviceActionsTest final : public QObject {
    Q_OBJECT

private slots:
    void dispatchesOnlyCurrentDeviceIdentities();
    void normalizesLinkCodesAndStartsSelfLink();
    void clearsRetryStateAcrossAuthorityChanges();
};

namespace {

void seed(kodosi::DevicesModel& devices, const bool enrolled)
{
    devices.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    devices.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\","
        "\"accountEpoch\":1,\"type\":\"devices.list\","
        "\"selfDeviceId\":\"self\",\"localDeviceEnrolled\":")
        + (enrolled ? QByteArrayLiteral("true") : QByteArrayLiteral("false"))
        + QByteArrayLiteral(
            ",\"devices\":[{\"deviceId\":\"self\",\"label\":\"This\","
            "\"certSignerDeviceId\":\"self\",\"certIssuedAtMs\":1},"
            "{\"deviceId\":\"other\",\"label\":\"Other\","
            "\"certSignerDeviceId\":\"self\",\"certIssuedAtMs\":2}]}"));
}

} // namespace

void DeviceActionsTest::dispatchesOnlyCurrentDeviceIdentities()
{
    FakeDeviceDispatcher dispatcher;
    kodosi::DevicesModel devices;
    seed(devices, true);
    kodosi::DeviceActions actions(dispatcher, devices);

    QVERIFY(!actions.revoke(QStringLiteral("self")));
    QVERIFY(!actions.revoke(QStringLiteral("missing")));
    QVERIFY(actions.revoke(QStringLiteral("other")));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("devices.revoke"));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("deviceId")).toString(),
        QStringLiteral("other"));

    devices.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\","
        "\"accountEpoch\":1,\"type\":\"devices.error\","
        "\"operation\":\"refresh\",\"message\":\"offline\"}"));
    QCOMPARE(actions.lastOperation(), QStringLiteral("refresh"));
    QVERIFY(actions.lastUserCode().isEmpty());
    QCOMPARE(actions.lastError(), QStringLiteral("offline"));
    QVERIFY(!actions.revoke(QStringLiteral("other")));
    QVERIFY(!actions.approveLink(QStringLiteral("BCDF-GHJK")));

    devices.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\","
        "\"accountEpoch\":1,\"type\":\"devices.error\","
        "\"operation\":\"link.approve\",\"userCode\":\"bcdf-ghjk\","
        "\"message\":\"approval expired\"}"));
    QCOMPARE(actions.lastOperation(), QStringLiteral("link.approve"));
    QCOMPARE(actions.lastUserCode(), QStringLiteral("BCDF-GHJK"));
    QCOMPARE(actions.lastError(), QStringLiteral("approval expired"));
    actions.clearError();
    QVERIFY(actions.lastOperation().isEmpty());
    QVERIFY(actions.lastUserCode().isEmpty());
}

void DeviceActionsTest::normalizesLinkCodesAndStartsSelfLink()
{
    FakeDeviceDispatcher dispatcher;
    kodosi::DevicesModel devices;
    seed(devices, true);
    kodosi::DeviceActions actions(dispatcher, devices);

    QVERIFY(actions.approveLink(QStringLiteral(" bcdf ghjk ")));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("userCode")).toString(),
        QStringLiteral("BCDF-GHJK"));
    QVERIFY(actions.approvalPending(QStringLiteral("BCDF-GHJK")));
    const auto commandCount = dispatcher.commands.size();
    QVERIFY(!actions.approveLink(QStringLiteral("bcdf-ghjk")));
    QCOMPARE(dispatcher.commands.size(), commandCount);
    devices.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\","
        "\"accountEpoch\":1,\"type\":\"devices.link.resolved\","
        "\"userCode\":\"BCDF-GHJK\",\"outcome\":\"approved\"}"));
    QVERIFY(!actions.approvalPending(QStringLiteral("BCDF-GHJK")));
    QVERIFY(!actions.approveLink(QStringLiteral("ABCD-EFGH")));
    QVERIFY(!actions.startSelfLink());

    kodosi::DevicesModel unenrolled;
    seed(unenrolled, false);
    kodosi::DeviceActions selfActions(dispatcher, unenrolled);
    QVERIFY(selfActions.startSelfLink());
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("devices.link.startSelf"));
}

void DeviceActionsTest::clearsRetryStateAcrossAuthorityChanges()
{
    FakeDeviceDispatcher dispatcher;
    kodosi::DevicesModel devices;
    seed(devices, true);
    kodosi::DeviceActions actions(dispatcher, devices);

    devices.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\","
        "\"accountEpoch\":1,\"type\":\"devices.error\","
        "\"operation\":\"link.approve\",\"userCode\":\"BCDF-GHJK\","
        "\"message\":\"approval expired\"}"));
    QVERIFY(!actions.lastError().isEmpty());
    QCOMPARE(actions.lastUserCode(), QStringLiteral("BCDF-GHJK"));

    devices.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.required\",\"accountEpoch\":2}"));
    QVERIFY(actions.lastError().isEmpty());
    QVERIFY(actions.lastOperation().isEmpty());
    QVERIFY(actions.lastUserCode().isEmpty());

    devices.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.ready\",\"userId\":\"next\",\"accountEpoch\":3}"));
    devices.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"next\","
        "\"accountEpoch\":3,\"type\":\"devices.error\","
        "\"operation\":\"refresh\",\"message\":\"offline\"}"));
    QVERIFY(!actions.lastError().isEmpty());
    devices.resetRuntimeAuthority();
    QVERIFY(actions.lastError().isEmpty());
    QVERIFY(actions.lastOperation().isEmpty());
    QVERIFY(actions.lastUserCode().isEmpty());
}

QTEST_APPLESS_MAIN(DeviceActionsTest)

#include "tst_device_actions.moc"
