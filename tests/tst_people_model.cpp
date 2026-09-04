#include "models/PeopleModel.hpp"

#include <QSignalSpy>
#include <QtTest/QTest>

class PeopleModelTest final : public QObject {
    Q_OBJECT

private slots:
    void appliesAtomicAccountScopedSnapshot();
    void fencesStaleAndReplaysFutureAccountSnapshot();
    void rejectsUnsafeAvatarWithoutDestroyingPriorSnapshot();
};

void PeopleModelTest::appliesAtomicAccountScopedSnapshot()
{
    kodosi::PeopleModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":3}"));
    model.ingestFriendsEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":3,"
        "\"type\":\"friends.snapshot\","
        "\"friends\":[{\"userId\":\"friend\",\"handle\":\"alice\","
        "\"displayName\":\"Alice\",\"avatarUrl\":\"https://cdn.kodosi.com/a.png\"}],"
        "\"incoming\":[{\"userId\":\"incoming\",\"handle\":\"bob\","
        "\"displayName\":\"Bob\",\"createdAt\":\"2026-08-31T00:00:00Z\"}],"
        "\"outgoing\":[]}"));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.friendCount(), 1);
    QCOMPARE(model.incomingCount(), 1);
    QCOMPARE(model.outgoingCount(), 0);
    QCOMPARE(
        model.data(model.index(0), kodosi::PeopleModel::DisplayNameRole).toString(),
        QStringLiteral("Alice"));
    QCOMPARE(
        model.data(model.index(1), kodosi::PeopleModel::RelationshipRole).value<
            kodosi::PeopleModel::Relationship>(),
        kodosi::PeopleModel::Relationship::IncomingRequest);
    QCOMPARE(
        model.data(model.index(1), kodosi::PeopleModel::RelationshipNameRole)
            .toString(),
        QStringLiteral("incoming"));
}

void PeopleModelTest::fencesStaleAndReplaysFutureAccountSnapshot()
{
    kodosi::PeopleModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"old\",\"accountEpoch\":1}"));
    model.ingestFriendsEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"new\",\"accountEpoch\":2,"
        "\"type\":\"friends.snapshot\",\"friends\":[],\"incoming\":[],"
        "\"outgoing\":[{\"userId\":\"out\",\"handle\":\"eve\",\"displayName\":\"Eve\","
        "\"createdAt\":\"2026-08-31T00:00:00Z\"}]}"));
    QCOMPARE(model.rowCount(), 0);

    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"new\",\"accountEpoch\":2}"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.friendCount(), 0);
    QCOMPARE(model.incomingCount(), 0);
    QCOMPARE(model.outgoingCount(), 1);
    model.ingestFriendsEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"old\",\"accountEpoch\":1,"
        "\"type\":\"friends.snapshot\",\"friends\":[],\"incoming\":[],\"outgoing\":[]}"));
    QCOMPARE(model.rowCount(), 1);
}

void PeopleModelTest::rejectsUnsafeAvatarWithoutDestroyingPriorSnapshot()
{
    kodosi::PeopleModel model;
    QSignalSpy errors(&model, &kodosi::PeopleModel::decodeError);
    model.ingestAuthEvent(
        QByteArrayLiteral("{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    model.ingestFriendsEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":1,"
        "\"type\":\"friends.snapshot\",\"friends\":[{\"userId\":\"safe\","
        "\"handle\":\"safe\",\"displayName\":\"Safe\"}],\"incoming\":[],\"outgoing\":[]}"));
    model.ingestFriendsEvent(QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"me\",\"accountEpoch\":1,"
        "\"type\":\"friends.snapshot\",\"friends\":[{\"userId\":\"bad\","
        "\"handle\":\"bad\",\"displayName\":\"Bad\",\"avatarUrl\":\"file:///etc/passwd\"}],"
        "\"incoming\":[],\"outgoing\":[]}"));

    QCOMPARE(errors.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.data(model.index(0), kodosi::PeopleModel::UserIdRole).toString(),
        QStringLiteral("safe"));
}

QTEST_APPLESS_MAIN(PeopleModelTest)

#include "tst_people_model.moc"
