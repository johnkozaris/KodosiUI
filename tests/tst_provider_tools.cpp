#include "SessionFixture.hpp"
#include "models/ProviderTools.hpp"
#include <QJsonDocument>
#include <QtTest/QTest>

class ProviderCommands final : public kodosi::CommandDispatcher {
public:
    QList<QJsonObject> values;
    Result send(QByteArrayView bytes) override
    {
        values.append(QJsonDocument::fromJson(bytes.toByteArray()).object());
        return {};
    }
    QJsonObject reply(QJsonObject result, int index = -1) const
    {
        const auto command = values.at(index < 0 ? values.size() - 1 : index);
        return { { QStringLiteral("type"), QStringLiteral("provider.reply") },
            { QStringLiteral("requestId"), command.value(QStringLiteral("requestId")) },
            { QStringLiteral("operation"), command.value(QStringLiteral("type")) },
            { QStringLiteral("result"), result } };
    }
};
class ProviderToolsTest final : public QObject {
    Q_OBJECT
private slots:
    void previewUsesByteCursorAndIgnoresStaleSelection()
    {
        ProviderCommands commands;
        kodosi::ProviderTools tools(commands);
        tools.select(QStringLiteral("claude"), QStringLiteral("/repo"));
        tools.discover();
        const QJsonObject item { { QStringLiteral("provider"), QStringLiteral("claude") },
            { QStringLiteral("workingDirectory"), QStringLiteral("/repo") },
            { QStringLiteral("nativeConversationId"), test::id(1) } };
        tools.apply(commands.reply({ { QStringLiteral("items"), QJsonArray { item } } }));
        QCOMPARE(tools.conversations().size(), 1);
        tools.preview(test::id(1));
        tools.apply(commands.reply(
            { { QStringLiteral("entries"),
                  QJsonArray { QJsonObject { { QStringLiteral("role"), QStringLiteral("user") },
                      { QStringLiteral("content"), QStringLiteral("Hello") } } } },
                { QStringLiteral("nextBeforeByte"), 123 } }));
        QCOMPARE(tools.entries().size(), 1);
        QVERIFY(tools.hasOlder());
        tools.loadOlder();
        QCOMPARE(commands.values.last().value(QStringLiteral("beforeByte")).toInt(), 123);
        QVERIFY(!commands.values.last().contains(QStringLiteral("beforeLine")));
        const auto stale = commands.reply({ { QStringLiteral("entries"), QJsonArray { QJsonObject {} } } });
        tools.select(QStringLiteral("copilot"), QStringLiteral("/other"));
        tools.apply(stale);
        QVERIFY(tools.entries().isEmpty());
        QVERIFY(!tools.busy());
    }
    void historyReplacesPagesAndCommitsNavigationOnlyOnSuccess()
    {
        ProviderCommands commands;
        kodosi::ProviderTools tools(commands);
        tools.select(QStringLiteral("claude"), QStringLiteral("/repo"));
        tools.discover();
        tools.apply(commands.reply({ { QStringLiteral("items"), QJsonArray { QJsonObject {
            { QStringLiteral("provider"), QStringLiteral("claude") },
            { QStringLiteral("workingDirectory"), QStringLiteral("/repo") },
            { QStringLiteral("nativeConversationId"), test::id(1) } } } } }));
        tools.preview(test::id(1));
        QCOMPARE(commands.values.last().value(QStringLiteral("limit")).toInt(), 50);
        QCOMPARE(commands.values.last().value(QStringLiteral("maxBytes")).toInt(), 131072);
        const auto page = [](QString content, int before) {
            return QJsonObject { { QStringLiteral("entries"), QJsonArray { QJsonObject {
                { QStringLiteral("content"), content } } } }, { QStringLiteral("nextBeforeByte"), before } };
        };
        tools.apply(commands.reply(page(QStringLiteral("latest"), 1000)));
        for (int i = 0; i < 45; ++i) {
            tools.loadOlder();
            tools.apply(commands.reply(page(QString::number(i), 999 - i)));
            QCOMPARE(tools.entries().size(), 1);
        }
        QVERIFY(tools.hasNewer());
        tools.loadLatest();
        tools.apply({ { QStringLiteral("type"), QStringLiteral("provider.error") },
            { QStringLiteral("requestId"), commands.values.last().value(QStringLiteral("requestId")) },
            { QStringLiteral("message"), QStringLiteral("Unavailable") } });
        QCOMPARE(tools.entries().first().toMap().value(QStringLiteral("content")).toString(), QStringLiteral("44"));
        QVERIFY(tools.hasNewer());
        tools.loadNewer();
        QCOMPARE(commands.values.last().value(QStringLiteral("beforeByte")).toInt(), 957);
        tools.apply(commands.reply(page(QStringLiteral("43"), 956)));
        tools.loadLatest();
        QVERIFY(!commands.values.last().contains(QStringLiteral("beforeByte")));
        tools.apply(commands.reply(page(QStringLiteral("latest"), 1000)));
        QVERIFY(!tools.hasNewer());
        QCOMPARE(tools.entries().size(), 1);
    }
    void allProjectsPreviewUsesTheSelectedNativeDirectory()
    {
        ProviderCommands commands;
        kodosi::ProviderTools tools(commands);
        tools.select(QStringLiteral("claude"), {});
        tools.discover();
        QVERIFY(!commands.values.last().contains(QStringLiteral("workingDirectory")));
        tools.apply(commands.reply({ { QStringLiteral("items"), QJsonArray { QJsonObject {
            { QStringLiteral("provider"), QStringLiteral("claude") },
            { QStringLiteral("workingDirectory"), QStringLiteral("/another-project") },
            { QStringLiteral("nativeConversationId"), test::id(1) } } } } }));
        tools.preview(test::id(1));
        QCOMPARE(commands.values.last().value(QStringLiteral("workingDirectory")).toString(), QStringLiteral("/another-project"));
    }
    void rejectsConversationFromAnotherProject()
    {
        ProviderCommands commands;
        kodosi::ProviderTools tools(commands);
        tools.select(QStringLiteral("claude"), QStringLiteral("/repo"));
        tools.discover();
        tools.apply(commands.reply({ { QStringLiteral("items"),
            QJsonArray { QJsonObject { { QStringLiteral("provider"), QStringLiteral("claude") },
                { QStringLiteral("workingDirectory"), QStringLiteral("/other") },
                { QStringLiteral("nativeConversationId"), test::id(1) } } } } }));
        QVERIFY(tools.conversations().isEmpty());
        QVERIFY(!tools.error().isEmpty());
    }
    void inspectionOnlyRequestsMetadata()
    {
        ProviderCommands commands;
        kodosi::ProviderTools tools(commands);
        tools.select(QStringLiteral("claude"), QStringLiteral("/repo"));
        tools.inspect();
        QCOMPARE(commands.values.last().value(QStringLiteral("type")).toString(),
            QStringLiteral("provider.inspect"));
        QVERIFY(!commands.values.last().contains(QStringLiteral("content")));
    }
};
QTEST_GUILESS_MAIN(ProviderToolsTest)
#include "tst_provider_tools.moc"
