#include "models/AgentGlobalModel.hpp"

#include "models/AccountContextFence.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

std::optional<QString> requiredString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    if (!value.isString() || value.toString().isEmpty()) {
        return std::nullopt;
    }
    return value.toString();
}

QString optionalString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    return value.isString() ? value.toString() : QString {};
}

bool optionalStringField(
    const QJsonObject& object,
    const QString& key,
    QString& result)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        result.clear();
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    result = value.toString();
    return true;
}

QString entryKey(
    const QString& vendor,
    const QString& cwd,
    const QString& scope,
    const QString& name)
{
    return vendor + QChar::Null + cwd + QChar::Null + scope + QChar::Null + name;
}

} // namespace

AgentCatalogModel::AgentCatalogModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AgentCatalogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant AgentCatalogModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries[index.row()];
    switch (role) {
    case VendorRole:
        return entry.vendor;
    case CwdRole:
        return entry.cwd;
    case KindRole:
        return QVariant::fromValue(entry.kind);
    case NameRole:
        return entry.name;
    case SourceRole:
        return entry.source;
    case ScopeRole:
        return entry.scope;
    case SourcePathRole:
        return entry.sourcePath;
    case ToolsRole:
        return entry.tools;
    default:
        return {};
    }
}

QHash<int, QByteArray> AgentCatalogModel::roleNames() const
{
    return {
        {VendorRole, QByteArrayLiteral("vendor")},
        {CwdRole, QByteArrayLiteral("cwd")},
        {KindRole, QByteArrayLiteral("kind")},
        {NameRole, QByteArrayLiteral("name")},
        {SourceRole, QByteArrayLiteral("source")},
        {ScopeRole, QByteArrayLiteral("scope")},
        {SourcePathRole, QByteArrayLiteral("sourcePath")},
        {ToolsRole, QByteArrayLiteral("tools")},
    };
}

void AgentCatalogModel::replace(
    QString vendor,
    QString cwd,
    QVector<Entry> entries)
{
    auto replacement = m_entries;
    replacement.removeIf([&](const Entry& entry) {
        return entry.vendor == vendor && entry.cwd == cwd;
    });
    replacement += std::move(entries);
    std::ranges::sort(replacement, [](const Entry& lhs, const Entry& rhs) {
        return std::tie(lhs.vendor, lhs.cwd, lhs.kind, lhs.name, lhs.scope)
            < std::tie(rhs.vendor, rhs.cwd, rhs.kind, rhs.name, rhs.scope);
    });
    beginResetModel();
    const auto countChangedValue = m_entries.size() != replacement.size();
    m_entries = std::move(replacement);
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }
}

AgentMcpModel::AgentMcpModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AgentMcpModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant AgentMcpModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries[index.row()];
    switch (role) {
    case VendorRole:
        return entry.vendor;
    case CwdRole:
        return entry.cwd;
    case ScopeRole:
        return entry.scope;
    case NameRole:
        return entry.name;
    case SourcePathRole:
        return entry.sourcePath;
    case HealthKindRole:
        return entry.healthKind;
    case HealthReasonRole:
        return entry.healthReason;
    default:
        return {};
    }
}

QHash<int, QByteArray> AgentMcpModel::roleNames() const
{
    return {
        {VendorRole, QByteArrayLiteral("vendor")},
        {CwdRole, QByteArrayLiteral("cwd")},
        {ScopeRole, QByteArrayLiteral("scope")},
        {NameRole, QByteArrayLiteral("name")},
        {SourcePathRole, QByteArrayLiteral("sourcePath")},
        {HealthKindRole, QByteArrayLiteral("healthKind")},
        {HealthReasonRole, QByteArrayLiteral("healthReason")},
    };
}

void AgentMcpModel::replaceEmbedded(
    QString vendor,
    QString cwd,
    QVector<Entry> entries)
{
    QSet<QString> retainedKeys;
    for (const auto& entry : entries) {
        retainedKeys.insert(
            entryKey(entry.vendor, entry.cwd, entry.scope, entry.name));
    }

    m_embeddedEntries.removeIf([&](const Entry& entry) {
        return entry.vendor == vendor
            && (entry.cwd.isEmpty() || entry.cwd == cwd);
    });
    m_embeddedEntries += entries;

    for (auto health = m_healthByKey.begin(); health != m_healthByKey.end();) {
        const auto& entry = health.value();
        if (entry.vendor == vendor
            && (entry.cwd.isEmpty() || entry.cwd == cwd)
            && !retainedKeys.contains(health.key())) {
            health = m_healthByKey.erase(health);
        } else {
            ++health;
        }
    }
    for (auto tombstone = m_tombstones.begin();
         tombstone != m_tombstones.end();) {
        const auto& entry = tombstone.value();
        if (entry.vendor == vendor
            && (entry.cwd.isEmpty() || entry.cwd == cwd)
            && !retainedKeys.contains(tombstone.key())) {
            tombstone = m_tombstones.erase(tombstone);
        } else {
            ++tombstone;
        }
    }
    for (const auto& entry : entries) {
        const auto key =
            entryKey(entry.vendor, entry.cwd, entry.scope, entry.name);
        m_tombstones.remove(key);
        if (entry.healthKind == QStringLiteral("unknown")) {
            m_healthByKey.remove(key);
        } else if (!entry.healthKind.isEmpty()) {
            m_healthByKey.insert(key, entry);
        }
    }
    rebuild();
}

void AgentMcpModel::applyHealth(Entry entry)
{
    const auto normalizedCwd =
        entry.scope == QStringLiteral("user") ? QString {} : entry.cwd;
    entry.cwd = normalizedCwd;
    const auto key = entryKey(entry.vendor, entry.cwd, entry.scope, entry.name);
    if (entry.healthKind == QStringLiteral("unknown")) {
        m_healthByKey.remove(key);
        m_tombstones.insert(key, std::move(entry));
    } else {
        m_tombstones.remove(key);
        m_healthByKey.insert(key, std::move(entry));
    }
    rebuild();
}

void AgentMcpModel::rebuild()
{
    QVector<Entry> projected;
    QSet<QString> embeddedKeys;
    for (auto entry : m_embeddedEntries) {
        const auto key =
            entryKey(entry.vendor, entry.cwd, entry.scope, entry.name);
        embeddedKeys.insert(key);
        if (m_tombstones.contains(key)) {
            continue;
        }
        if (const auto health = m_healthByKey.constFind(key);
            health != m_healthByKey.cend()) {
            entry.healthKind = health->healthKind;
            entry.healthReason = health->healthReason;
        }
        projected.push_back(std::move(entry));
    }
    for (auto health = m_healthByKey.cbegin(); health != m_healthByKey.cend(); ++health) {
        if (!embeddedKeys.contains(health.key())
            && !m_tombstones.contains(health.key())) {
            projected.push_back(health.value());
        }
    }
    std::ranges::sort(projected, [](const Entry& lhs, const Entry& rhs) {
        return std::tie(lhs.vendor, lhs.cwd, lhs.scope, lhs.name)
            < std::tie(rhs.vendor, rhs.cwd, rhs.scope, rhs.name);
    });
    beginResetModel();
    const auto countChangedValue = m_entries.size() != projected.size();
    m_entries = std::move(projected);
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }
}

AgentGlobalModel::AgentGlobalModel(CommandDispatcher& dispatcher, QObject* parent)
    : QAbstractListModel(parent)
    , m_dispatcher(dispatcher)
    , m_catalog(this)
    , m_mcpServers(this)
{
    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, [this] {
        failExpiredRefreshes();
    });
}

int AgentGlobalModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_statuses.size();
}

QVariant AgentGlobalModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_statuses.size()) {
        return {};
    }
    const auto& status = m_statuses[index.row()];
    switch (role) {
    case VendorRole:
        return status.vendor;
    case CwdRole:
        return status.cwd;
    case VersionRole:
        return status.version;
    case RefreshStateRole:
        return QVariant::fromValue(status.refreshState);
    case ErrorRole:
        return status.error;
    case PluginCountRole:
        return status.pluginCount;
    case SkillCountRole:
        return status.skillCount;
    case AgentCountRole:
        return status.agentCount;
    case McpCountRole:
        return status.mcpCount;
    case NoticesRole:
        return status.notices;
    default:
        return {};
    }
}

QHash<int, QByteArray> AgentGlobalModel::roleNames() const
{
    return {
        {VendorRole, QByteArrayLiteral("vendor")},
        {CwdRole, QByteArrayLiteral("cwd")},
        {VersionRole, QByteArrayLiteral("version")},
        {RefreshStateRole, QByteArrayLiteral("refreshState")},
        {ErrorRole, QByteArrayLiteral("error")},
        {PluginCountRole, QByteArrayLiteral("pluginCount")},
        {SkillCountRole, QByteArrayLiteral("skillCount")},
        {AgentCountRole, QByteArrayLiteral("agentCount")},
        {McpCountRole, QByteArrayLiteral("mcpCount")},
        {NoticesRole, QByteArrayLiteral("notices")},
    };
}

AgentCatalogModel* AgentGlobalModel::catalog() noexcept
{
    return &m_catalog;
}

AgentMcpModel* AgentGlobalModel::mcpServers() noexcept
{
    return &m_mcpServers;
}

bool AgentGlobalModel::refresh(const QString& cwd)
{
    constexpr quint64 maximumExactJsonInteger = 9'007'199'254'740'991ULL;
    if (m_nextGeneration >= maximumExactJsonInteger) {
        return false;
    }
    const auto normalizedCwd = normalizeCwd(cwd);
    const auto generation = ++m_nextGeneration;
    m_refreshes.insert(normalizedCwd, Refresh {
        .generation = generation,
        .deadline = QDateTime::currentDateTimeUtc().addMSecs(refreshTimeoutMs),
    });
    for (const auto& vendor : {QStringLiteral("claude"), QStringLiteral("copilot")}) {
        auto* status = findStatus(vendor, normalizedCwd);
        if (status == nullptr) {
            upsertStatus(Status {
                .vendor = vendor,
                .cwd = normalizedCwd,
                .version = {},
                .error = {},
                .notices = {},
                .pluginCount = 0,
                .skillCount = 0,
                .agentCount = 0,
                .mcpCount = 0,
                .refreshState = RefreshState::Loading,
            });
        } else {
            status->refreshState = RefreshState::Loading;
            status->error.clear();
            const auto row = static_cast<int>(status - m_statuses.data());
            emit dataChanged(index(row), index(row), {RefreshStateRole, ErrorRole});
        }
    }
    scheduleTimeout();

    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("claude.global.refresh")},
        {QStringLiteral("generation"), static_cast<qint64>(generation)},
    };
    if (!normalizedCwd.isEmpty()) {
        command.insert(QStringLiteral("cwd"), normalizedCwd);
    }
    const auto json = QJsonDocument(command).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::System, json)) {
        return true;
    }
    auto refresh = m_refreshes.find(normalizedCwd);
    if (refresh != m_refreshes.end() && refresh->generation == generation) {
        refresh->deadline = QDateTime::currentDateTimeUtc();
        failExpiredRefreshes();
    }
    return false;
}

void AgentGlobalModel::ingestAgentGlobalEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Agent-global event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        emit decodeError(QStringLiteral("Agent-global event has no type."));
        return;
    }
    if (*type == QStringLiteral("agent.global.mcp.health")) {
        const auto vendor = requiredString(object, QStringLiteral("vendor"));
        const auto scope = requiredString(object, QStringLiteral("scope"));
        const auto name = requiredString(object, QStringLiteral("serverName"));
        const auto health = object.value(QStringLiteral("health"));
        if (!vendor || !scope || !name || !health.isObject()) {
            emit decodeError(QStringLiteral("MCP health event is invalid."));
            return;
        }
        const auto healthKind =
            requiredString(health.toObject(), QStringLiteral("kind"));
        QString reason;
        if (!healthKind
            || !optionalStringField(
                health.toObject(),
                QStringLiteral("reason"),
                reason)) {
            emit decodeError(QStringLiteral("MCP health event is invalid."));
            return;
        }
        m_mcpServers.applyHealth({
            .vendor = *vendor,
            .cwd = {},
            .scope = *scope,
            .name = *name,
            .sourcePath = {},
            .healthKind = *healthKind,
            .healthReason = reason,
        });
        return;
    }
    QString vendor;
    if (*type == QStringLiteral("agent.global.claude.status")) {
        vendor = QStringLiteral("claude");
    } else if (*type == QStringLiteral("agent.global.copilot.status")) {
        vendor = QStringLiteral("copilot");
    } else {
        return;
    }
    const auto statusValue = object.value(QStringLiteral("status"));
    const auto generation = decodeGeneration(json, object);
    if (!statusValue.isObject()
        || (object.contains(QStringLiteral("generation"))
            && !object.value(QStringLiteral("generation")).isNull()
            && !generation)) {
        emit decodeError(QStringLiteral("Agent-global status envelope is invalid."));
        return;
    }
    auto decoded = decodeStatus(vendor, statusValue.toObject());
    if (!decoded) {
        emit decodeError(QStringLiteral("Agent-global status is incomplete or invalid."));
        return;
    }
    if (!shouldAccept(decoded->status.cwd, generation)) {
        return;
    }
    applyStatus(std::move(*decoded), generation);
}

QString AgentGlobalModel::normalizeCwd(const QString& cwd)
{
    const auto trimmed = cwd.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(trimmed);
}

std::optional<AgentGlobalModel::DecodedStatus> AgentGlobalModel::decodeStatus(
    QString vendor,
    const QJsonObject& object)
{
    QString rawCwd;
    QString version;
    const auto versionKey = vendor == QStringLiteral("claude")
        ? QStringLiteral("claudeCodeVersion")
        : QStringLiteral("copilotCliVersion");
    if (!optionalStringField(object, QStringLiteral("cwd"), rawCwd)
        || !optionalStringField(object, versionKey, version)) {
        return std::nullopt;
    }
    const auto cwd = normalizeCwd(rawCwd);
    const auto plugins = object.value(QStringLiteral("installedPlugins"));
    const auto skills = object.value(QStringLiteral("loadedSkills"));
    const auto agents = object.value(QStringLiteral("loadedAgents"));
    const auto servers = object.value(QStringLiteral("mcpServers"));
    if (!plugins.isArray() || !skills.isArray() || !agents.isArray()
        || !servers.isArray()
        || (!object.value(QStringLiteral("notices")).isUndefined()
            && !object.value(QStringLiteral("notices")).isNull()
            && !object.value(QStringLiteral("notices")).isArray())) {
        return std::nullopt;
    }

    QVector<AgentCatalogModel::Entry> catalog;
    QSet<QString> catalogKeys;
    const auto decodeCatalog = [&](
                                   const QJsonArray& values,
                                   const AgentCatalogModel::Kind kind) {
        for (const auto& value : values) {
            if (!value.isObject()) {
                return false;
            }
            const auto row = value.toObject();
            QString name;
            QString source;
            QString scope;
            QString sourcePath;
            QStringList tools;
            if (kind == AgentCatalogModel::Kind::Plugin) {
                if (vendor == QStringLiteral("claude")) {
                    name = optionalString(row, QStringLiteral("id"));
                    source = optionalString(row, QStringLiteral("marketplace"));
                    scope = optionalString(row, QStringLiteral("scope"));
                    if (name.isEmpty() || source.isEmpty() || scope.isEmpty()) {
                        return false;
                    }
                } else {
                    name = optionalString(row, QStringLiteral("name"));
                    source = optionalString(row, QStringLiteral("source"));
                }
            } else {
                name = optionalString(row, QStringLiteral("name"));
                source = optionalString(row, QStringLiteral("source"));
                if (source.isEmpty()) {
                    source = optionalString(row, QStringLiteral("scope"));
                }
                scope = optionalString(row, QStringLiteral("scope"));
                sourcePath = optionalString(row, QStringLiteral("sourcePath"));
                if (source.isEmpty()) {
                    return false;
                }
                if (kind == AgentCatalogModel::Kind::Skill) {
                    if (vendor == QStringLiteral("claude")
                        && !row.value(QStringLiteral("userInvocable")).isBool()) {
                        return false;
                    }
                } else {
                    const auto toolsValue = vendor == QStringLiteral("claude")
                        ? row.value(QStringLiteral("disallowedTools"))
                        : row.value(QStringLiteral("tools"));
                    if (toolsValue.isUndefined()
                        && vendor == QStringLiteral("copilot")) {
                        tools.clear();
                    } else if (!toolsValue.isArray()) {
                        return false;
                    } else {
                        for (const auto& tool : toolsValue.toArray()) {
                            if (!tool.isString()) {
                                return false;
                            }
                            tools.push_back(tool.toString());
                        }
                    }
                }
            }
            if (name.isEmpty()) {
                return false;
            }
            const auto key = QString::number(static_cast<int>(kind))
                + QChar::Null + name + QChar::Null + source + QChar::Null
                + scope + QChar::Null + sourcePath;
            if (catalogKeys.contains(key)) {
                return false;
            }
            catalogKeys.insert(key);
            catalog.push_back({
                .vendor = vendor,
                .cwd = cwd,
                .kind = kind,
                .name = name,
                .source = source,
                .scope = scope,
                .sourcePath = sourcePath,
                .tools = tools,
            });
        }
        return true;
    };
    if (!decodeCatalog(plugins.toArray(), AgentCatalogModel::Kind::Plugin)
        || !decodeCatalog(skills.toArray(), AgentCatalogModel::Kind::Skill)
        || !decodeCatalog(agents.toArray(), AgentCatalogModel::Kind::Agent)) {
        return std::nullopt;
    }

    QVector<AgentMcpModel::Entry> mcpServers;
    QSet<QString> mcpKeys;
    for (const auto& value : servers.toArray()) {
        if (!value.isObject()) {
            return std::nullopt;
        }
        auto server = decodeMcp(vendor, cwd, value.toObject());
        if (!server) {
            return std::nullopt;
        }
        const auto key = entryKey(
            server->vendor,
            server->cwd,
            server->scope,
            server->name);
        if (mcpKeys.contains(key)) {
            return std::nullopt;
        }
        mcpKeys.insert(key);
        mcpServers.push_back(std::move(*server));
    }
    return DecodedStatus {
        .status = {
            .vendor = vendor,
            .cwd = cwd,
            .version = version,
            .error = {},
            .notices = decodeNotices(object.value(QStringLiteral("notices"))),
            .pluginCount = static_cast<quint32>(plugins.toArray().size()),
            .skillCount = static_cast<quint32>(skills.toArray().size()),
            .agentCount = static_cast<quint32>(agents.toArray().size()),
            .mcpCount = static_cast<quint32>(servers.toArray().size()),
            .refreshState = RefreshState::Idle,
        },
        .catalog = std::move(catalog),
        .mcpServers = std::move(mcpServers),
    };
}

std::optional<AgentMcpModel::Entry> AgentGlobalModel::decodeMcp(
    QString vendor,
    QString cwd,
    const QJsonObject& object)
{
    const auto name = requiredString(object, QStringLiteral("name"));
    const auto scope = requiredString(object, QStringLiteral("scope"));
    QString sourcePath;
    if (!name || !scope
        || (vendor == QStringLiteral("claude")
            && !object.value(QStringLiteral("enabled")).isBool())
        || !optionalStringField(
            object,
            QStringLiteral("sourcePath"),
            sourcePath)) {
        return std::nullopt;
    }
    QString healthKind;
    QString healthReason;
    const auto health = object.value(QStringLiteral("health"));
    if (!health.isUndefined() && !health.isNull()) {
        if (!health.isObject()) {
            return std::nullopt;
        }
        const auto kind =
            requiredString(health.toObject(), QStringLiteral("kind"));
        if (!kind
            || !optionalStringField(
                health.toObject(),
                QStringLiteral("reason"),
                healthReason)) {
            return std::nullopt;
        }
        healthKind = *kind;
    }
    return AgentMcpModel::Entry {
        .vendor = std::move(vendor),
        .cwd = *scope == QStringLiteral("user") ? QString {} : std::move(cwd),
        .scope = *scope,
        .name = *name,
        .sourcePath = sourcePath,
        .healthKind = healthKind,
        .healthReason = healthReason,
    };
}

QStringList AgentGlobalModel::decodeNotices(const QJsonValue& value)
{
    QStringList notices;
    if (value.isUndefined() || value.isNull()) {
        return notices;
    }
    if (!value.isArray()) {
        return notices;
    }
    for (const auto& entry : value.toArray()) {
        if (!entry.isObject()) {
            continue;
        }
        const auto object = entry.toObject();
        const auto kind = requiredString(object, QStringLiteral("kind"));
        if (!kind) {
            continue;
        }
        const auto message = optionalString(object, QStringLiteral("message"));
        const auto path = optionalString(object, QStringLiteral("path"));
        if (!message.isEmpty()) {
            notices.push_back(
                path.isEmpty() ? message : path + QStringLiteral(": ") + message);
        } else {
            notices.push_back(*kind);
        }
    }
    return notices;
}

std::optional<quint64> AgentGlobalModel::decodeGeneration(
    const QByteArray& json,
    const QJsonObject& object)
{
    const auto value = object.value(QStringLiteral("generation"));
    if (value.isUndefined() || value.isNull()) {
        return std::nullopt;
    }
    return exactUnsignedJsonField(json, QByteArrayLiteral("generation"));
}

bool AgentGlobalModel::shouldAccept(
    const QString& cwd,
    const std::optional<quint64> generation) const
{
    const auto refresh = m_refreshes.constFind(cwd);
    if (refresh == m_refreshes.cend()) {
        return true;
    }
    if (generation) {
        return *generation == refresh->generation;
    }
    for (const auto& vendor : {QStringLiteral("claude"), QStringLiteral("copilot")}) {
        const auto* status = findStatus(vendor, cwd);
        if (status != nullptr && status->refreshState == RefreshState::Loading) {
            return false;
        }
    }
    return true;
}

void AgentGlobalModel::applyStatus(
    DecodedStatus decoded,
    const std::optional<quint64> generation)
{
    if (!generation) {
        if (const auto* prior = findStatus(decoded.status.vendor, decoded.status.cwd);
            prior != nullptr && decoded.status.version.isEmpty()) {
            decoded.status.version = prior->version;
        }
    }
    decoded.status.refreshState = decoded.status.notices.isEmpty()
        ? RefreshState::Loaded
        : RefreshState::Degraded;
    m_catalog.replace(
        decoded.status.vendor,
        decoded.status.cwd,
        std::move(decoded.catalog));
    m_mcpServers.replaceEmbedded(
        decoded.status.vendor,
        decoded.status.cwd,
        std::move(decoded.mcpServers));
    const auto cwd = decoded.status.cwd;
    upsertStatus(std::move(decoded.status));

    const auto* claude = findStatus(QStringLiteral("claude"), cwd);
    const auto* copilot = findStatus(QStringLiteral("copilot"), cwd);
    if (claude != nullptr && copilot != nullptr
        && claude->refreshState != RefreshState::Loading
        && copilot->refreshState != RefreshState::Loading) {
        m_refreshes.remove(cwd);
        scheduleTimeout();
    }
}

void AgentGlobalModel::failExpiredRefreshes()
{
    const auto now = QDateTime::currentDateTimeUtc();
    for (auto refresh = m_refreshes.begin(); refresh != m_refreshes.end();) {
        if (refresh->deadline > now) {
            ++refresh;
            continue;
        }
        const auto cwd = refresh.key();
        for (const auto& vendor : {QStringLiteral("claude"), QStringLiteral("copilot")}) {
            auto* status = findStatus(vendor, cwd);
            if (status != nullptr && status->refreshState == RefreshState::Loading) {
                status->refreshState = RefreshState::Failed;
                status->error = QStringLiteral("Integration status refresh failed.");
                const auto row = static_cast<int>(status - m_statuses.data());
                emit dataChanged(
                    index(row),
                    index(row),
                    {RefreshStateRole, ErrorRole});
            }
        }
        refresh = m_refreshes.erase(refresh);
    }
    scheduleTimeout();
}

void AgentGlobalModel::scheduleTimeout()
{
    if (m_refreshes.isEmpty()) {
        m_timeoutTimer.stop();
        return;
    }
    auto deadline = m_refreshes.cbegin()->deadline;
    for (auto refresh = m_refreshes.cbegin(); refresh != m_refreshes.cend(); ++refresh) {
        deadline = std::min(deadline, refresh->deadline);
    }
    const auto delay = std::clamp<qint64>(
        QDateTime::currentDateTimeUtc().msecsTo(deadline),
        0,
        std::numeric_limits<int>::max());
    m_timeoutTimer.start(static_cast<int>(delay));
}

void AgentGlobalModel::upsertStatus(Status status)
{
    const auto found = std::ranges::find_if(m_statuses, [&](const Status& candidate) {
        return candidate.vendor == status.vendor && candidate.cwd == status.cwd;
    });
    if (found == m_statuses.end()) {
        const auto row = m_statuses.size();
        beginInsertRows({}, row, row);
        m_statuses.push_back(std::move(status));
        endInsertRows();
        emit countChanged();
        return;
    }
    const auto row = static_cast<int>(std::distance(m_statuses.begin(), found));
    *found = std::move(status);
    emit dataChanged(index(row), index(row));
}

AgentGlobalModel::Status* AgentGlobalModel::findStatus(
    const QString& vendor,
    const QString& cwd)
{
    const auto found = std::ranges::find_if(m_statuses, [&](const Status& status) {
        return status.vendor == vendor && status.cwd == cwd;
    });
    return found == m_statuses.end() ? nullptr : &*found;
}

const AgentGlobalModel::Status* AgentGlobalModel::findStatus(
    const QString& vendor,
    const QString& cwd) const
{
    const auto found = std::ranges::find_if(m_statuses, [&](const Status& status) {
        return status.vendor == vendor && status.cwd == cwd;
    });
    return found == m_statuses.end() ? nullptr : &*found;
}

} // namespace kodosi
