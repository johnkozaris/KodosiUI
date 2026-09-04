#include "models/AuthActions.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

class FakeAuthDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QString> commandTypes;
    bool reject = false;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (lane != kodosi::CommandLane::Auth || !document.isObject() || reject) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        commandTypes.push_back(
            document.object().value(QStringLiteral("type")).toString());
        return {};
    }
};

class AuthActionsTest final : public QObject {
    Q_OBJECT

private slots:
    void dispatchesTypedAuthCommands();
    void settlesFromAuthEventsAndRejectsUnsafeUrls();
    void surfacesImmediateRejection();
    void retriesExactOperationAndSwitchesAccount();
};

void AuthActionsTest::dispatchesTypedAuthCommands()
{
    FakeAuthDispatcher dispatcher;
    kodosi::AuthActions actions(dispatcher);

    QVERIFY(actions.beginSignIn());
    QVERIFY(actions.signOut());
    QVERIFY(actions.refresh());
    QVERIFY(actions.resetIdentity());
    QCOMPARE(
        dispatcher.commandTypes,
        QVector<QString>({
            QStringLiteral("auth.login.start"),
            QStringLiteral("auth.logout"),
            QStringLiteral("auth.refresh"),
            QStringLiteral("auth.identity.reset"),
        }));
}

void AuthActionsTest::settlesFromAuthEventsAndRejectsUnsafeUrls()
{
    FakeAuthDispatcher dispatcher;
    kodosi::AuthActions actions(dispatcher);
    QVERIFY(actions.beginSignIn());
    QVERIFY(actions.busy());
    actions.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"me","accountEpoch":1})"));
    QVERIFY(!actions.busy());

    QVERIFY(!actions.openVerificationUrl(QUrl(QStringLiteral("http://example.com"))));
    QVERIFY(!actions.lastError().isEmpty());
    actions.clearError();
    QVERIFY(actions.lastError().isEmpty());
}

void AuthActionsTest::surfacesImmediateRejection()
{
    FakeAuthDispatcher dispatcher;
    dispatcher.reject = true;
    kodosi::AuthActions actions(dispatcher);

    QVERIFY(!actions.beginSignIn());
    QVERIFY(!actions.busy());
    QVERIFY(!actions.lastError().isEmpty());
}

void AuthActionsTest::retriesExactOperationAndSwitchesAccount()
{
    FakeAuthDispatcher dispatcher;
    kodosi::AuthActions actions(dispatcher);
    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.error\",\"operation\":\"logout\","
        "\"message\":\"try later\"}"));
    QCOMPARE(actions.failedOperation(), QStringLiteral("logout"));
    QVERIFY(actions.retry());
    QCOMPARE(dispatcher.commandTypes.back(), QStringLiteral("auth.logout"));
    QVERIFY(actions.busy());
    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.required\",\"reason\":\"signedOut\","
        "\"accountEpoch\":2}"));
    QVERIFY(!actions.busy());

    QVERIFY(actions.useAnotherAccount());
    QCOMPARE(dispatcher.commandTypes.back(), QStringLiteral("auth.logout"));
    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.required\",\"reason\":\"signedOut\","
        "\"accountEpoch\":3}"));
    QCOMPARE(
        dispatcher.commandTypes.back(),
        QStringLiteral("auth.login.start"));
    QVERIFY(actions.busy());

    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.device_code\",\"userCode\":\"CODE\","
        "\"verificationUri\":\"http://example.com\"}"));
    QVERIFY(!actions.busy());
    QCOMPARE(actions.failedOperation(), QStringLiteral("login.start"));
    QVERIFY(actions.lastError().contains(QStringLiteral("invalid")));

    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.error\",\"operation\":\"login.start\","
        "\"message\":\"login failed: device code expired\"}"));
    QCOMPARE(
        actions.lastError(),
        QStringLiteral(
            "This sign-in code expired. Try again to generate a new one."));
    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.required\",\"reason\":\"expired\","
        "\"accountEpoch\":2}"));
    QCOMPARE(actions.failedOperation(), QStringLiteral("login.start"));
    QCOMPARE(
        actions.lastError(),
        QStringLiteral(
            "This sign-in code expired. Try again to generate a new one."));
}

QTEST_GUILESS_MAIN(AuthActionsTest)

#include "tst_auth_actions.moc"
