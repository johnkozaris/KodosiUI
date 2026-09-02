#include "models/DevicesModel.hpp"

#include <QSignalSpy>
#include <QtTest/QTest>

class DevicesModelTest final : public QObject {
    Q_OBJECT

private slots:
    void appliesAtomicInventory();
    void fencesAccountContext();
    void rejectsNestedAccountEpoch();
    void projectsLinkLifecycle();
    void rejectsMalformedSnapshotsWithoutDestroyingState();
    void normalizesOnlyAllocatedUserCodes();
};

void DevicesModelTest::appliesAtomicInventory()
{
    kodosi::DevicesModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":4}"));
    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":4,"
        "\"type\":\"devices.list\",\"selfDeviceId\":\"device-self\","
        "\"localDeviceEnrolled\":true,\"devices\":["
        "{\"deviceId\":\"device-other\",\"label\":\"Tablet\","
        "\"certSignerDeviceId\":\"device-self\",\"certIssuedAtMs\":1700000000001},"
        "{\"deviceId\":\"device-self\",\"label\":\"Laptop\","
        "\"certSignerDeviceId\":\"device-self\",\"certIssuedAtMs\":1700000000000}]}"));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.selfDeviceId(), QStringLiteral("device-self"));
    QVERIFY(model.hasEnrollmentState());
    QVERIFY(model.localDeviceEnrolled());
    QCOMPARE(model.inventoryState(), kodosi::DevicesModel::InventoryState::Fresh);
    QCOMPARE(
        model.data(model.index(0), kodosi::DevicesModel::LabelRole).toString(),
        QStringLiteral("Laptop"));
    QVERIFY(model.data(model.index(0), kodosi::DevicesModel::IsSelfRole).toBool());
    QCOMPARE(
        model.data(model.index(0), kodosi::DevicesModel::CertIssuedAtMsRole).toULongLong(),
        1'700'000'000'000ULL);
}

void DevicesModelTest::fencesAccountContext()
{
    kodosi::DevicesModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"old\",\"accountEpoch\":1}"));
    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"new\",\"accountEpoch\":2,"
        "\"type\":\"devices.list\",\"selfDeviceId\":\"new-device\","
        "\"localDeviceEnrolled\":false,\"devices\":[]}"));
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.hasEnrollmentState());

    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"new\",\"accountEpoch\":2}"));
    QCOMPARE(model.selfDeviceId(), QStringLiteral("new-device"));
    QVERIFY(model.hasEnrollmentState());
    QVERIFY(!model.localDeviceEnrolled());

    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"old\",\"accountEpoch\":1,"
        "\"type\":\"devices.list\",\"selfDeviceId\":\"stale\","
        "\"localDeviceEnrolled\":true,\"devices\":[]}"));
    QCOMPARE(model.selfDeviceId(), QStringLiteral("new-device"));
}

void DevicesModelTest::rejectsNestedAccountEpoch()
{
    kodosi::DevicesModel model;
    QSignalSpy errors(&model, &kodosi::DevicesModel::decodeError);
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":4}"));
    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\","
        "\"type\":\"devices.list\",\"selfDeviceId\":\"device-self\","
        "\"localDeviceEnrolled\":true,\"devices\":["
        "{\"deviceId\":\"nested\",\"label\":\"Nested\",\"certSignerDeviceId\":\"nested\","
        "\"certIssuedAtMs\":1,\"accountEpoch\":4}]}"));

    QCOMPARE(errors.count(), 1);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.hasEnrollmentState());
}

void DevicesModelTest::projectsLinkLifecycle()
{
    kodosi::DevicesModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":1,"
        "\"type\":\"devices.link.snapshot\",\"requests\":["
        "{\"userCode\":\"bcdf ghjk\",\"deviceLabel\":\"Phone\","
        "\"expiresAt\":\"2099-08-13T18:00:00Z\"},"
        "{\"userCode\":\"BCDF-2345\",\"deviceLabel\":\"Tablet\","
        "\"expiresAt\":\"2099-08-13T19:00:00Z\"}]}"));

    QCOMPARE(model.pendingLinks()->rowCount(), 2);
    QCOMPARE(
        model.pendingLinks()
            ->data(
                model.pendingLinks()->index(0),
                kodosi::DeviceLinkRequestsModel::UserCodeRole)
            .toString(),
        QStringLiteral("BCDF-GHJK"));

    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":1,"
        "\"type\":\"devices.link.resolved\",\"userCode\":\"bcdf-ghjk\","
        "\"outcome\":\"approved\"}"));
    QCOMPARE(model.pendingLinks()->rowCount(), 1);
    QCOMPARE(model.lastResolvedUserCode(), QStringLiteral("BCDF-GHJK"));
    QCOMPARE(model.lastLinkOutcome(), kodosi::DevicesModel::LinkOutcome::Approved);

    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":1,"
        "\"type\":\"devices.link.selfPending\",\"userCode\":\"MNPQ-2345\","
        "\"expiresAt\":\"2099-08-13T20:00:00Z\"}"));
    QVERIFY(model.hasSelfLinkPending());
    QCOMPARE(model.selfLinkUserCode(), QStringLiteral("MNPQ-2345"));
    model.pruneExpiredLinks(
        QDateTime::fromString(QStringLiteral("2100-01-01T00:00:00Z"), Qt::ISODate));
    QVERIFY(!model.hasSelfLinkPending());
    QCOMPARE(model.selfLinkOutcome(), kodosi::DevicesModel::SelfLinkOutcome::Expired);
}

void DevicesModelTest::rejectsMalformedSnapshotsWithoutDestroyingState()
{
    kodosi::DevicesModel model;
    QSignalSpy errors(&model, &kodosi::DevicesModel::decodeError);
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":1,"
        "\"type\":\"devices.list\",\"selfDeviceId\":\"self\","
        "\"localDeviceEnrolled\":true,\"devices\":["
        "{\"deviceId\":\"safe\",\"label\":\"Safe\",\"certSignerDeviceId\":\"self\","
        "\"certIssuedAtMs\":1700000000000}]}"));
    model.ingestDevicesEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":1,"
        "\"type\":\"devices.list\",\"selfDeviceId\":\"self\","
        "\"localDeviceEnrolled\":true,\"devices\":["
        "{\"deviceId\":\"duplicate\",\"label\":\"First\",\"certSignerDeviceId\":\"self\","
        "\"certIssuedAtMs\":1},"
        "{\"deviceId\":\"duplicate\",\"label\":\"Second\",\"certSignerDeviceId\":\"self\","
        "\"certIssuedAtMs\":2}]}"));

    QCOMPARE(errors.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.data(model.index(0), kodosi::DevicesModel::DeviceIdRole).toString(),
        QStringLiteral("safe"));
}

void DevicesModelTest::normalizesOnlyAllocatedUserCodes()
{
    kodosi::DevicesModel model;
    QCOMPARE(model.normalizeUserCode(QStringLiteral(" bcdf - ghjk ")), QStringLiteral("BCDF-GHJK"));
    QVERIFY(model.isCanonicalUserCode(QStringLiteral("BCDF-GHJK")));
    QVERIFY(model.isCanonicalUserCode(QStringLiteral("MNPQ-2345")));
    QVERIFY(!model.isCanonicalUserCode(model.normalizeUserCode(QStringLiteral("ABCD-EFGH"))));
    QVERIFY(!model.isCanonicalUserCode(model.normalizeUserCode(QStringLiteral("BCDF!GHJ"))));
}

QTEST_APPLESS_MAIN(DevicesModelTest)

#include "tst_devices_model.moc"
