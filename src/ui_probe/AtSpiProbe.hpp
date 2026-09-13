#pragma once

#include <QByteArray>
#include <QDBusConnection>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <exception>
#include <optional>

namespace kodosi::ui_probe {

enum class ExitCode : int {
    Success = 0,
    Usage = 2,
    Environment = 3,
    NotFound = 4,
    Ambiguous = 5,
    Unsupported = 6,
    RemoteError = 7,
    Timeout = 8,
    InvalidHandle = 9,
    PortalDenied = 10,
    LimitExceeded = 11,
};

class ProbeError final : public std::exception
{
public:
    ProbeError(ExitCode code, QString kind, QString message);

    [[nodiscard]] ExitCode code() const noexcept;
    [[nodiscard]] const QString& kind() const noexcept;
    [[nodiscard]] const QString& message() const noexcept;
    [[nodiscard]] const char* what() const noexcept override;

private:
    ExitCode m_code;
    QString m_kind;
    QString m_message;
    QByteArray m_utf8;
};

struct Selector {
    QString id;
    QString name;
    QString role;
    QString contains;

    [[nodiscard]] bool empty() const;
};

class AtSpiProbe final
{
public:
    struct Reference {
        QString busName;
        QString objectPath;
    };

    static constexpr int DefaultDepth = 8;
    static constexpr int MaximumDepth = 32;
    static constexpr int MaximumNodes = 2000;
    static constexpr int MaximumTextBytes = 65536;
    static constexpr int MaximumTraversalBytes = 2 * 1024 * 1024;
    static constexpr int DefaultCallTimeoutMs = 3000;
    static constexpr int MaximumWaitTimeoutMs = 120000;

    explicit AtSpiProbe(
        int callTimeoutMs = DefaultCallTimeoutMs,
        int operationTimeoutMs = -1);
    ~AtSpiProbe();

    AtSpiProbe(const AtSpiProbe&) = delete;
    AtSpiProbe& operator=(const AtSpiProbe&) = delete;

    [[nodiscard]] QJsonObject apps();
    [[nodiscard]] QJsonObject tree(
        const QString& app,
        std::optional<qint64> processId,
        int depth);
    [[nodiscard]] QJsonObject find(
        const QString& app,
        std::optional<qint64> processId,
        const Selector& selector);
    [[nodiscard]] QJsonObject inspectHandle(const QString& handle);
    [[nodiscard]] QJsonObject click(const QString& handle);
    [[nodiscard]] QJsonObject focus(const QString& handle);
    [[nodiscard]] QJsonObject setText(
        const QString& handle,
        const QString& text);
    [[nodiscard]] QJsonObject wait(
        const QString& app,
        std::optional<qint64> processId,
        const Selector& selector,
        const QString& state,
        const QString& text,
        int timeoutMs);
    [[nodiscard]] QJsonObject doctor();
    [[nodiscard]] QPoint elementPoint(
        const QString& handle,
        const QString& position);

    [[nodiscard]] static QString encodeHandle(
        const QString& busName,
        const QString& objectPath);
    [[nodiscard]] static bool decodeHandle(
        const QString& handle,
        QString* busName,
        QString* objectPath);

private:
    class TimeoutBudget {
    public:
        explicit TimeoutBudget(
            int defaultTimeoutMs,
            const QDeadlineTimer* deadline = nullptr);

        void setDeadline(const QDeadlineTimer* deadline);
        [[nodiscard]] operator int() const;

    private:
        int m_defaultTimeoutMs;
        const QDeadlineTimer* m_deadline;
    };

    struct Snapshot {
        Reference reference;
        QString name;
        QString description;
        QString accessibleId;
        QString helpText;
        QString role;
        QStringList interfaces;
        QStringList states;
        QJsonObject attributes;
        int childCount = 0;
        bool hasBounds = false;
        QJsonObject bounds;
    };

    struct WalkResult {
        QJsonArray flat;
        QJsonObject root;
        bool truncated = false;
        int count = 0;
        qsizetype estimatedBytes = 0;
    };

    struct TextSnapshot {
        QString content;
        int characterCount = 0;
        bool truncated = false;
    };

    struct MatchResult {
        QJsonArray matches;
        bool truncated = false;
    };

    QDBusConnection m_sessionBus;
    QDBusConnection m_atspiBus;
    QString m_atspiConnectionName;
    std::optional<QDeadlineTimer> m_operationDeadline;
    TimeoutBudget m_callTimeoutMs;

    [[nodiscard]] Reference resolveApplication(
        const QString& app,
        std::optional<qint64> processId);
    [[nodiscard]] QList<Reference> applications();
    [[nodiscard]] qint64 applicationProcessId(const Reference& reference);
    [[nodiscard]] QJsonObject applicationJson(
        const Reference& reference,
        bool includeBounds);
    [[nodiscard]] Snapshot snapshot(
        const Reference& reference,
        bool includeBounds);
    [[nodiscard]] QJsonObject fullInspection(const Reference& reference);
    [[nodiscard]] WalkResult walk(
        const Reference& root,
        int depth,
        bool includeText);
    [[nodiscard]] QJsonObject walkNode(
        const Reference& reference,
        int remainingDepth,
        bool includeText,
        WalkResult* result);
    [[nodiscard]] QList<Reference> children(const Reference& reference);
    [[nodiscard]] TextSnapshot nodeText(
        const Reference& reference,
        const QStringList& interfaces);
    [[nodiscard]] bool matchesStructural(
        const QJsonObject& node,
        const Selector& selector) const;
    [[nodiscard]] MatchResult matchingNodes(
        const Reference& app,
        const Selector& selector);

    [[nodiscard]] static bool validBusName(const QString& busName);
    [[nodiscard]] static bool validObjectPath(const QString& objectPath);
    [[nodiscard]] static Reference referenceFromHandle(const QString& handle);
    [[nodiscard]] static QJsonObject referenceJson(const Reference& reference);
    [[nodiscard]] static QJsonObject snapshotJson(const Snapshot& snapshot);
    [[nodiscard]] static QStringList decodeStates(const QList<quint32>& words);
};

[[nodiscard]] QJsonObject takePortalScreenshot(
    const QString& absoluteOutputPath,
    bool interactive,
    int timeoutMs);

}
