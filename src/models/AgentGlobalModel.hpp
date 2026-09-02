#pragma once

#include "bridge/RuntimeBridge.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <optional>

namespace kodosi {

class AgentCatalogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum class Kind {
        Plugin,
        Skill,
        Agent,
    };
    Q_ENUM(Kind)

    enum Role {
        VendorRole = Qt::UserRole + 1,
        CwdRole,
        KindRole,
        NameRole,
        SourceRole,
        ScopeRole,
        SourcePathRole,
        ToolsRole,
    };
    Q_ENUM(Role)

    explicit AgentCatalogModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    friend class AgentGlobalModel;

    struct Entry {
        QString vendor;
        QString cwd;
        Kind kind;
        QString name;
        QString source;
        QString scope;
        QString sourcePath;
        QStringList tools;
    };

    QVector<Entry> m_entries;

    void replace(QString vendor, QString cwd, QVector<Entry> entries);
};

class AgentMcpModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        VendorRole = Qt::UserRole + 1,
        CwdRole,
        ScopeRole,
        NameRole,
        SourcePathRole,
        HealthKindRole,
        HealthReasonRole,
    };
    Q_ENUM(Role)

    explicit AgentMcpModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    friend class AgentGlobalModel;

    struct Entry {
        QString vendor;
        QString cwd;
        QString scope;
        QString name;
        QString sourcePath;
        QString healthKind;
        QString healthReason;
    };

    QVector<Entry> m_entries;
    QVector<Entry> m_embeddedEntries;
    QHash<QString, Entry> m_healthByKey;
    QHash<QString, Entry> m_tombstones;

    void replaceEmbedded(QString vendor, QString cwd, QVector<Entry> entries);
    void applyHealth(Entry entry);
    void rebuild();
};

class AgentGlobalModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(kodosi::AgentCatalogModel* catalog READ catalog CONSTANT)
    Q_PROPERTY(kodosi::AgentMcpModel* mcpServers READ mcpServers CONSTANT)

public:
    enum class RefreshState {
        Idle,
        Loading,
        Loaded,
        Degraded,
        Failed,
    };
    Q_ENUM(RefreshState)

    enum Role {
        VendorRole = Qt::UserRole + 1,
        CwdRole,
        VersionRole,
        RefreshStateRole,
        ErrorRole,
        PluginCountRole,
        SkillCountRole,
        AgentCountRole,
        McpCountRole,
        NoticesRole,
    };
    Q_ENUM(Role)

    explicit AgentGlobalModel(CommandDispatcher& dispatcher, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] AgentCatalogModel* catalog() noexcept;
    [[nodiscard]] AgentMcpModel* mcpServers() noexcept;

    Q_INVOKABLE [[nodiscard]] bool refresh(const QString& cwd = {});

public slots:
    void ingestAgentGlobalEvent(QByteArray json);

signals:
    void countChanged();
    void decodeError(QString message);

private:
    struct Status {
        QString vendor;
        QString cwd;
        QString version;
        QString error;
        QStringList notices;
        quint32 pluginCount = 0;
        quint32 skillCount = 0;
        quint32 agentCount = 0;
        quint32 mcpCount = 0;
        RefreshState refreshState = RefreshState::Idle;
    };

    struct Refresh {
        quint64 generation;
        QDateTime deadline;
    };

    struct DecodedStatus {
        Status status;
        QVector<AgentCatalogModel::Entry> catalog;
        QVector<AgentMcpModel::Entry> mcpServers;
    };

    static constexpr qint64 refreshTimeoutMs = 10'000;

    CommandDispatcher& m_dispatcher;
    AgentCatalogModel m_catalog;
    AgentMcpModel m_mcpServers;
    QVector<Status> m_statuses;
    QHash<QString, Refresh> m_refreshes;
    QTimer m_timeoutTimer;
    quint64 m_nextGeneration = 0;

    [[nodiscard]] static QString normalizeCwd(const QString& cwd);
    [[nodiscard]] static std::optional<DecodedStatus> decodeStatus(
        QString vendor,
        const QJsonObject& object);
    [[nodiscard]] static std::optional<AgentMcpModel::Entry> decodeMcp(
        QString vendor,
        QString cwd,
        const QJsonObject& object);
    [[nodiscard]] static QStringList decodeNotices(const QJsonValue& value);
    [[nodiscard]] static std::optional<quint64> decodeGeneration(
        const QByteArray& json,
        const QJsonObject& object);
    [[nodiscard]] bool shouldAccept(
        const QString& cwd,
        std::optional<quint64> generation) const;
    void applyStatus(DecodedStatus decoded, std::optional<quint64> generation);
    void failExpiredRefreshes();
    void scheduleTimeout();
    void upsertStatus(Status status);
    [[nodiscard]] Status* findStatus(const QString& vendor, const QString& cwd);
    [[nodiscard]] const Status* findStatus(
        const QString& vendor,
        const QString& cwd) const;
};

} // namespace kodosi
