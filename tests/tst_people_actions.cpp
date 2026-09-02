#include "models/PeopleActions.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

class FakePeopleDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (lane != kodosi::CommandLane::Friends || !document.isObject()) {
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

class PeopleActionsTest final : public QObject {
    Q_OBJECT

private slots:
    void dispatchesRelationshipBoundActions();
    void createsUuidV7RequestIdentity();
};

namespace {

void seed(kodosi::PeopleModel& people)
{
    people.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    people.ingestFriendsEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\","
        "\"accountEpoch\":1,\"type\":\"friends.snapshot\","
        "\"friends\":[{\"userId\":\"friend\",\"handle\":\"alice\","
        "\"displayName\":\"Alice\"}],"
        "\"incoming\":[{\"userId\":\"incoming\",\"handle\":\"bob\","
        "\"displayName\":\"Bob\",\"createdAt\":\"2026-08-31T09:00:00Z\"}],"
        "\"outgoing\":[{\"userId\":\"outgoing\",\"handle\":\"cara\","
        "\"displayName\":\"Cara\",\"createdAt\":\"2026-08-31T09:00:00Z\"}]}"));
}

} // namespace

void PeopleActionsTest::dispatchesRelationshipBoundActions()
{
    FakePeopleDispatcher dispatcher;
    kodosi::PeopleModel people;
    seed(people);
    kodosi::PeopleActions actions(dispatcher, people);

    QVERIFY(actions.accept(QStringLiteral("@bob")));
    QVERIFY(actions.reject(QStringLiteral("bob")));
    QVERIFY(actions.cancel(QStringLiteral("cara")));
    QVERIFY(actions.remove(QStringLiteral("alice")));
    QVERIFY(!actions.accept(QStringLiteral("alice")));
    QCOMPARE(dispatcher.commands.size(), 4);
    QCOMPARE(
        dispatcher.commands.at(0).value(QStringLiteral("type")).toString(),
        QStringLiteral("friends.request.accept"));
    QCOMPARE(
        dispatcher.commands.at(3).value(QStringLiteral("type")).toString(),
        QStringLiteral("friends.remove"));
}

void PeopleActionsTest::createsUuidV7RequestIdentity()
{
    FakePeopleDispatcher dispatcher;
    kodosi::PeopleModel people;
    kodosi::PeopleActions actions(dispatcher, people);

    QVERIFY(actions.sendRequest(QStringLiteral(" @new-person ")));
    const auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("username")).toString(),
        QStringLiteral("new-person"));
    const QUuid requestId(command.value(QStringLiteral("requestId")).toString());
    QCOMPARE(requestId.version(), QUuid::Version::UnixEpoch);
}

QTEST_APPLESS_MAIN(PeopleActionsTest)

#include "tst_people_actions.moc"
