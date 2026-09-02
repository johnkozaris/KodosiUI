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
    void retriesExactOperationAndBoundsProgress();
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

void AuthActionsTest::retriesExactOperationAndBoundsProgress()
{
    FakeAuthDispatcher dispatcher;
    kodosi::AuthActions actions(dispatcher, 1);
    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.error\",\"operation\":\"logout\","
        "\"message\":\"try later\"}"));
    QCOMPARE(actions.failedOperation(), QStringLiteral("logout"));
    QVERIFY(actions.retry());
    QCOMPARE(dispatcher.commandTypes.back(), QStringLiteral("auth.logout"));
    QTest::qWait(100);
    QVERIFY(!actions.busy());
    QVERIFY(!actions.lastError().isEmpty());

    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.device_code\",\"userCode\":\"CODE\","
        "\"verificationUri\":\"http://example.com\"}"));
    QVERIFY(!actions.busy());
    QCOMPARE(actions.failedOperation(), QStringLiteral("login.start"));
    QVERIFY(actions.lastError().contains(QStringLiteral("invalid")));

    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.error\",\"operation\":\"login.start\","
        "\"message\":\"approval expired\"}"));
    actions.ingestAuthEvent(QByteArrayLiteral(
        "{\"type\":\"auth.required\",\"reason\":\"expired\","
        "\"accountEpoch\":2}"));
    QCOMPARE(actions.failedOperation(), QStringLiteral("login.start"));
    QCOMPARE(actions.lastError(), QStringLiteral("approval expired"));
}

QTEST_GUILESS_MAIN(AuthActionsTest)

#include "tst_auth_actions.moc"
