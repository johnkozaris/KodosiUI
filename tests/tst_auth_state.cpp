#include "models/AuthStateModel.hpp"

#include <QSignalSpy>
#include <QtTest/QTest>

class AuthStateTest final : public QObject {
    Q_OBJECT

private slots:
    void projectsAuthLifecycle();
    void rejectsStaleEpochAndUntrustedVerificationUrl();
    void requiresOneTopLevelExactEpoch();
};

void AuthStateTest::projectsAuthLifecycle()
{
    kodosi::AuthStateModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"user-1\",\"accountEpoch\":7}"));
    QCOMPARE(model.phase(), kodosi::AuthStateModel::Phase::Ready);
    QVERIFY(model.signedIn());
    model.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.identity_health\",\"state\":\"healthy\"}"));
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"user-1\",\"accountEpoch\":7}"));
    QCOMPARE(model.identityHealth(), QStringLiteral("healthy"));

    model.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.device_code\",\"userCode\":\"ABCD-EFGH\","
        "\"verificationUri\":\"https://auth.kodosi.com/activate\"}"));
    QCOMPARE(model.phase(), kodosi::AuthStateModel::Phase::DeviceCode);
    QCOMPARE(model.userCode(), QStringLiteral("ABCD-EFGH"));
    QCOMPARE(model.verificationUrl().scheme(), QStringLiteral("https"));

    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.required\",\"reason\":\"signedOut\",\"accountEpoch\":8}"));
    QCOMPARE(model.phase(), kodosi::AuthStateModel::Phase::SignInRequired);
    QVERIFY(!model.signedIn());
    QVERIFY(model.userCode().isEmpty());
}

void AuthStateTest::rejectsStaleEpochAndUntrustedVerificationUrl()
{
    kodosi::AuthStateModel model;
    QSignalSpy errors(&model, &kodosi::AuthStateModel::decodeError);
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"current\",\"accountEpoch\":9}"));
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"stale\",\"accountEpoch\":8}"));
    QCOMPARE(model.userId(), QStringLiteral("current"));

    model.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.device_code\",\"userCode\":\"CODE\","
        "\"verificationUri\":\"http://example.com\"}"));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(model.phase(), kodosi::AuthStateModel::Phase::Ready);

    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"accountEpoch\":10}"));
    QVERIFY(model.signedIn());
    QVERIFY(model.userId().isEmpty());

    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"bad\",\"accountEpoch\":10.5}"));
    QCOMPARE(errors.count(), 2);
    QVERIFY(model.userId().isEmpty());
}

void AuthStateTest::requiresOneTopLevelExactEpoch()
{
    kodosi::AuthStateModel model;
    QSignalSpy errors(&model, &kodosi::AuthStateModel::decodeError);
    model.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.ready\",\"userId\":\"nested\","
        "\"payload\":{\"accountEpoch\":1}}"));
    model.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.ready\",\"userId\":\"duplicate\","
        "\"accountEpoch\":1,\"account\\u0045poch\":2}"));

    QCOMPARE(errors.count(), 2);
    QVERIFY(!model.signedIn());
}

QTEST_APPLESS_MAIN(AuthStateTest)

#include "tst_auth_state.moc"
