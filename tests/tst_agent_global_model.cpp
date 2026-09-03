#include "models/AgentGlobalModel.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <utility>

class FakeAgentGlobalDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    bool rejectNext = false;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        if (lane != kodosi::CommandLane::System) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Wrong lane"),
            });
        }
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (!document.isObject()) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Invalid JSON"),
            });
        }
        commands.push_back(document.object());
        if (std::exchange(rejectNext, false)) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        return {};
    }
};

class AgentGlobalModelTest final : public QObject {
    Q_OBJECT

private slots:
    void refreshesCorrelatedWorkspaceCatalogs();
    void rejectsStaleAndGenerationlessStatusesWhileLoading();
    void combinesEmbeddedAndPushMcpHealth();
    void preservesPushedHealthAndMetadataAcrossRefreshes();
    void rejectsMalformedStatusAtomically();
    void settlesImmediateDispatchFailure();
    void acceptsToollessCopilotAgentsAndScopedClaudePlugins();
    void integrationRowsExposeUniqueAccessibleIds();
    void syntheticFixtureStaysPopulatedWithoutDispatch();
};

namespace {

QByteArray statusEvent(
    const QString& vendor,
    const QString& cwd,
    const std::optional<quint64> generation,
    const QString& version,
    const bool degraded = false,
    const bool embeddedHealth = true)
{
    const auto claude = vendor == QStringLiteral("claude");
    QJsonObject mcpServer {
        {QStringLiteral("name"), QStringLiteral("github")},
        {QStringLiteral("scope"), QStringLiteral("user")},
        {QStringLiteral("enabled"), true},
    };
    if (embeddedHealth) {
        mcpServer.insert(
            QStringLiteral("health"),
            QJsonObject {{QStringLiteral("kind"), QStringLiteral("healthy")}});
    }
    QJsonObject status {
        {QStringLiteral("cwd"), cwd},
        {claude ? QStringLiteral("claudeCodeVersion")
                : QStringLiteral("copilotCliVersion"),
         version},
        {QStringLiteral("installedPlugins"),
         QJsonArray {
             claude
                 ? QJsonObject {
                       {QStringLiteral("id"), QStringLiteral("review")},
                       {QStringLiteral("marketplace"), QStringLiteral("official")},
                       {QStringLiteral("scope"), QStringLiteral("user")},
                   }
                 : QJsonObject {
                       {QStringLiteral("name"), QStringLiteral("review")},
                       {QStringLiteral("source"), QStringLiteral("official")},
                   },
         }},
        {QStringLiteral("loadedSkills"),
         QJsonArray {
             claude
                 ? QJsonObject {
                       {QStringLiteral("name"), QStringLiteral("audit")},
                       {QStringLiteral("source"), QStringLiteral("plugin")},
                       {QStringLiteral("sourcePath"), QStringLiteral("/repo/audit.md")},
                       {QStringLiteral("userInvocable"), true},
                   }
                 : QJsonObject {
                       {QStringLiteral("name"), QStringLiteral("audit")},
                       {QStringLiteral("scope"), QStringLiteral("user")},
                   },
         }},
        {QStringLiteral("loadedAgents"),
         QJsonArray {
             claude
                 ? QJsonObject {
                       {QStringLiteral("name"), QStringLiteral("reviewer")},
                       {QStringLiteral("source"), QStringLiteral("plugin")},
                       {QStringLiteral("disallowedTools"),
                        QJsonArray {QStringLiteral("write")}},
                   }
                 : QJsonObject {
                       {QStringLiteral("name"), QStringLiteral("reviewer")},
                       {QStringLiteral("scope"), QStringLiteral("repo")},
                       {QStringLiteral("tools"), QJsonArray {QStringLiteral("read")}},
                   },
         }},
        {QStringLiteral("mcpServers"),
         QJsonArray {mcpServer}},
    };
    if (degraded) {
        status.insert(
            QStringLiteral("notices"),
            QJsonArray {
                QJsonObject {
                    {QStringLiteral("kind"), QStringLiteral("malformedSettings")},
                    {QStringLiteral("path"), QStringLiteral("/repo/settings.json")},
                    {QStringLiteral("message"), QStringLiteral("invalid JSON")},
                },
            });
    }
    QJsonObject event {
        {QStringLiteral("type"),
         claude ? QStringLiteral("agent.global.claude.status")
                : QStringLiteral("agent.global.copilot.status")},
        {QStringLiteral("status"), status},
    };
    if (generation) {
        event.insert(
            QStringLiteral("generation"),
            static_cast<qint64>(*generation));
    }
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

int statusRow(
    const kodosi::AgentGlobalModel& model,
    const QString& vendor,
    const QString& cwd)
{
    for (auto row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), kodosi::AgentGlobalModel::VendorRole)
                    .toString()
                == vendor
            && model.data(model.index(row), kodosi::AgentGlobalModel::CwdRole)
                    .toString()
                == cwd) {
            return row;
        }
    }
    return -1;
}

} // namespace

void AgentGlobalModelTest::refreshesCorrelatedWorkspaceCatalogs()
{
    FakeAgentGlobalDispatcher dispatcher;
    kodosi::AgentGlobalModel model(dispatcher);
    QVERIFY(model.refresh(QStringLiteral("/repo/./")));
    QCOMPARE(dispatcher.commands.size(), 1);
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("cwd")).toString(),
        QStringLiteral("/repo"));
    const auto generation =
        dispatcher.commands.back().value(QStringLiteral("generation")).toInteger();

    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("claude"),
        QStringLiteral("/repo"),
        generation,
        QStringLiteral("2.1.0"),
        true));
    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("copilot"),
        QStringLiteral("/repo"),
        generation,
        QStringLiteral("1.0.0")));

    const auto claudeRow =
        statusRow(model, QStringLiteral("claude"), QStringLiteral("/repo"));
    const auto copilotRow =
        statusRow(model, QStringLiteral("copilot"), QStringLiteral("/repo"));
    QVERIFY(claudeRow >= 0);
    QVERIFY(copilotRow >= 0);
    QCOMPARE(
        model.data(model.index(claudeRow), kodosi::AgentGlobalModel::RefreshStateRole)
            .value<kodosi::AgentGlobalModel::RefreshState>(),
        kodosi::AgentGlobalModel::RefreshState::Degraded);
    QCOMPARE(
        model.data(model.index(copilotRow), kodosi::AgentGlobalModel::VersionRole)
            .toString(),
        QStringLiteral("1.0.0"));
    QCOMPARE(model.catalog()->rowCount(), 6);
    QCOMPARE(model.mcpServers()->rowCount(), 2);
}

void AgentGlobalModelTest::rejectsStaleAndGenerationlessStatusesWhileLoading()
{
    FakeAgentGlobalDispatcher dispatcher;
    kodosi::AgentGlobalModel model(dispatcher);
    QVERIFY(model.refresh(QStringLiteral("/repo")));
    const auto first =
        dispatcher.commands.back().value(QStringLiteral("generation")).toInteger();
    QVERIFY(model.refresh(QStringLiteral("/repo")));
    const auto current =
        dispatcher.commands.back().value(QStringLiteral("generation")).toInteger();

    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("claude"),
        QStringLiteral("/repo"),
        first,
        QStringLiteral("stale")));
    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("claude"),
        QStringLiteral("/repo"),
        std::nullopt,
        QStringLiteral("uncorrelated")));
    const auto row =
        statusRow(model, QStringLiteral("claude"), QStringLiteral("/repo"));
    QVERIFY(model.data(model.index(row), kodosi::AgentGlobalModel::VersionRole)
                .toString()
            .isEmpty());

    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("claude"),
        QStringLiteral("/repo"),
        current,
        QStringLiteral("current")));
    QCOMPARE(
        model.data(model.index(row), kodosi::AgentGlobalModel::VersionRole).toString(),
        QStringLiteral("current"));
}

void AgentGlobalModelTest::combinesEmbeddedAndPushMcpHealth()
{
    FakeAgentGlobalDispatcher dispatcher;
    kodosi::AgentGlobalModel model(dispatcher);
    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("copilot"),
        QString {},
        std::nullopt,
        QStringLiteral("1.0.0")));
    QCOMPARE(model.mcpServers()->rowCount(), 1);

    model.ingestAgentGlobalEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("agent.global.mcp.health")},
        {QStringLiteral("vendor"), QStringLiteral("copilot")},
        {QStringLiteral("scope"), QStringLiteral("user")},
        {QStringLiteral("serverName"), QStringLiteral("github")},
        {QStringLiteral("health"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("unreachable")},
             {QStringLiteral("reason"), QStringLiteral("connection refused")},
         }},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(
        model.mcpServers()
            ->data(model.mcpServers()->index(0), kodosi::AgentMcpModel::HealthKindRole)
            .toString(),
        QStringLiteral("unreachable"));

    model.ingestAgentGlobalEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("agent.global.mcp.health")},
        {QStringLiteral("vendor"), QStringLiteral("copilot")},
        {QStringLiteral("scope"), QStringLiteral("user")},
        {QStringLiteral("serverName"), QStringLiteral("github")},
        {QStringLiteral("health"),
         QJsonObject {{QStringLiteral("kind"), QStringLiteral("unknown")}}},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(model.mcpServers()->rowCount(), 0);
}

void AgentGlobalModelTest::preservesPushedHealthAndMetadataAcrossRefreshes()
{
    FakeAgentGlobalDispatcher dispatcher;
    kodosi::AgentGlobalModel model(dispatcher);
    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("copilot"),
        QString {},
        std::nullopt,
        QStringLiteral("1.0.0")));
    model.ingestAgentGlobalEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("agent.global.mcp.health")},
        {QStringLiteral("vendor"), QStringLiteral("copilot")},
        {QStringLiteral("scope"), QStringLiteral("user")},
        {QStringLiteral("serverName"), QStringLiteral("github")},
        {QStringLiteral("health"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("unreachable")},
             {QStringLiteral("reason"), QStringLiteral("offline")},
         }},
    }).toJson(QJsonDocument::Compact));
    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("copilot"),
        QString {},
        std::nullopt,
        QStringLiteral("1.0.1"),
        false,
        false));
    QCOMPARE(model.mcpServers()->rowCount(), 1);
    QCOMPARE(
        model.mcpServers()
            ->data(model.mcpServers()->index(0), kodosi::AgentMcpModel::HealthKindRole)
            .toString(),
        QStringLiteral("unreachable"));

    model.ingestAgentGlobalEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("agent.global.mcp.health")},
        {QStringLiteral("vendor"), QStringLiteral("copilot")},
        {QStringLiteral("scope"), QStringLiteral("user")},
        {QStringLiteral("serverName"), QStringLiteral("github")},
        {QStringLiteral("health"),
         QJsonObject {{QStringLiteral("kind"), QStringLiteral("unknown")}}},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(model.mcpServers()->rowCount(), 0);
    model.ingestAgentGlobalEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("agent.global.mcp.health")},
        {QStringLiteral("vendor"), QStringLiteral("copilot")},
        {QStringLiteral("scope"), QStringLiteral("user")},
        {QStringLiteral("serverName"), QStringLiteral("github")},
        {QStringLiteral("health"),
         QJsonObject {{QStringLiteral("kind"), QStringLiteral("healthy")}}},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(model.mcpServers()->rowCount(), 1);
    QCOMPARE(
        model.mcpServers()
            ->data(model.mcpServers()->index(0), kodosi::AgentMcpModel::NameRole)
            .toString(),
        QStringLiteral("github"));
}

void AgentGlobalModelTest::rejectsMalformedStatusAtomically()
{
    FakeAgentGlobalDispatcher dispatcher;
    kodosi::AgentGlobalModel model(dispatcher);
    QSignalSpy errors(&model, &kodosi::AgentGlobalModel::decodeError);
    model.ingestAgentGlobalEvent(statusEvent(
        QStringLiteral("claude"),
        QString {},
        std::nullopt,
        QStringLiteral("known")));
    const auto row = statusRow(model, QStringLiteral("claude"), QString {});

    model.ingestAgentGlobalEvent(QByteArrayLiteral(
        "{\"type\":\"agent.global.claude.status\",\"status\":{"
        "\"claudeCodeVersion\":\"bad\",\"installedPlugins\":[],"
        "\"loadedSkills\":[],\"loadedAgents\":[]}}"));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(
        model.data(model.index(row), kodosi::AgentGlobalModel::VersionRole).toString(),
        QStringLiteral("known"));
}

void AgentGlobalModelTest::settlesImmediateDispatchFailure()
{
    FakeAgentGlobalDispatcher dispatcher;
    dispatcher.rejectNext = true;
    kodosi::AgentGlobalModel model(dispatcher);
    QVERIFY(!model.refresh());
    for (const auto& vendor : {QStringLiteral("claude"), QStringLiteral("copilot")}) {
        const auto row = statusRow(model, vendor, QString {});
        QCOMPARE(
            model.data(model.index(row), kodosi::AgentGlobalModel::RefreshStateRole)
                .value<kodosi::AgentGlobalModel::RefreshState>(),
            kodosi::AgentGlobalModel::RefreshState::Failed);
    }
}

void AgentGlobalModelTest::acceptsToollessCopilotAgentsAndScopedClaudePlugins()
{
    FakeAgentGlobalDispatcher dispatcher;
    kodosi::AgentGlobalModel model(dispatcher);
    auto copilot = QJsonDocument::fromJson(statusEvent(
        QStringLiteral("copilot"),
        QString {},
        std::nullopt,
        QStringLiteral("1.0.0")))
                       .object();
    auto copilotStatus = copilot.value(QStringLiteral("status")).toObject();
    copilotStatus.insert(
        QStringLiteral("loadedAgents"),
        QJsonArray {
            QJsonObject {
                {QStringLiteral("name"), QStringLiteral("default")},
                {QStringLiteral("scope"), QStringLiteral("user")},
            },
        });
    copilot.insert(QStringLiteral("status"), copilotStatus);
    model.ingestAgentGlobalEvent(QJsonDocument(copilot).toJson(QJsonDocument::Compact));

    auto claude = QJsonDocument::fromJson(statusEvent(
        QStringLiteral("claude"),
        QString {},
        std::nullopt,
        QStringLiteral("2.0.0")))
                      .object();
    auto claudeStatus = claude.value(QStringLiteral("status")).toObject();
    claudeStatus.insert(
        QStringLiteral("installedPlugins"),
        QJsonArray {
            QJsonObject {
                {QStringLiteral("id"), QStringLiteral("review")},
                {QStringLiteral("marketplace"), QStringLiteral("official")},
                {QStringLiteral("scope"), QStringLiteral("user")},
            },
            QJsonObject {
                {QStringLiteral("id"), QStringLiteral("review")},
                {QStringLiteral("marketplace"), QStringLiteral("official")},
                {QStringLiteral("scope"), QStringLiteral("project")},
            },
        });
    claude.insert(QStringLiteral("status"), claudeStatus);
    model.ingestAgentGlobalEvent(QJsonDocument(claude).toJson(QJsonDocument::Compact));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.catalog()->rowCount(), 7);
}

void AgentGlobalModelTest::integrationRowsExposeUniqueAccessibleIds()
{
    QFile file(
        QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/Devices/AgentIntegrationsPanel.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto qml = file.readAll();
    QVERIFY(qml.contains(QByteArrayLiteral(
        "objectName: visible\n"
        "                    ? \"integrations.vendor.\" + integration.vendor\n"
        "                    : \"\"\n"
        "                Accessible.id: objectName\n"
        "                Accessible.name: integration.vendor === \"claude\"\n"
        "                    ? qsTr(\"Claude Code integration\")\n"
        "                    : qsTr(\"GitHub Copilot CLI integration\")\n"
        "                Accessible.role: Accessible.ListItem")));
}

void AgentGlobalModelTest::syntheticFixtureStaysPopulatedWithoutDispatch()
{
    FakeAgentGlobalDispatcher dispatcher;
    kodosi::AgentGlobalModel model(dispatcher);

    model.installSyntheticFixture();

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.mcpServers()->rowCount(), 2);
    QVERIFY(model.refresh());
    QVERIFY(dispatcher.commands.isEmpty());
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentGlobalModel::RefreshStateRole)
            .value<kodosi::AgentGlobalModel::RefreshState>(),
        kodosi::AgentGlobalModel::RefreshState::Loaded);
}

QTEST_GUILESS_MAIN(AgentGlobalModelTest)

#include "tst_agent_global_model.moc"
