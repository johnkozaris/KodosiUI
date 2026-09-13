#pragma once

#include "ui_probe/AtSpiProbe.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace kodosi::ui_probe {

enum class InputEventKind {
    Key,
    PointerAbsolute,
    PointerRelative,
    Button,
};

struct InputEvent {
    InputEventKind kind = InputEventKind::Key;
    int code = 0;
    bool pressed = false;
    double x = 0;
    double y = 0;
    int delayMs = 0;
};

struct InputPlan {
    QString command;
    QList<InputEvent> events;
    QList<InputEvent> cleanup;
    QJsonObject details;
};

class InputAdapter
{
public:
    virtual ~InputAdapter() = default;

    [[nodiscard]] virtual QString name() const = 0;
    virtual void send(const InputEvent& event) = 0;
    [[nodiscard]] virtual QJsonObject status() const = 0;
};

class InputProtocol final
{
public:
    static constexpr qsizetype MaximumMessageBytes = 64 * 1024;
    static constexpr int MaximumEventsPerRequest = 512;
    static constexpr int MaximumCoordinate = 1000000;
    static constexpr int MaximumDragDurationMs = 30000;
    static constexpr int MaximumEventDelayMs = 1000;
    static constexpr int MaximumEventsPerSecond = 500;

    [[nodiscard]] static int keySym(const QString& name);
    [[nodiscard]] static QString canonicalKeyName(const QString& name);
    [[nodiscard]] static int buttonCode(const QString& name);
    [[nodiscard]] static QJsonObject eventJson(const InputEvent& event);
    [[nodiscard]] static InputEvent eventFromJson(const QJsonObject& object);
    [[nodiscard]] static QByteArray frame(const QJsonObject& object);
    [[nodiscard]] static std::optional<QJsonObject> takeFrame(QByteArray* data);
    [[nodiscard]] static bool peerUidAllowed(quint32 owner, quint32 peer);
};

[[nodiscard]] InputPlan keyPlan(
    const QString& key,
    bool down,
    bool up);
[[nodiscard]] InputPlan shortcutPlan(const QString& keys);
[[nodiscard]] InputPlan pointerPlan(double x, double y, bool relative);
[[nodiscard]] InputPlan buttonPlan(
    const QString& button,
    bool down,
    bool up);
[[nodiscard]] InputPlan dragPlan(
    int fromX,
    int fromY,
    int toX,
    int toY,
    int durationMs);

[[nodiscard]] QJsonObject executeInput(
    const InputPlan& plan,
    const QString& adapterPreference = QStringLiteral("auto"));
[[nodiscard]] QJsonObject inputStatus();
[[nodiscard]] QJsonObject servePortalInput(
    qint64 absoluteDeadlineMs,
    int lifecycleDescriptor,
    qint64 launcherPid,
    const QString& startupToken);
[[nodiscard]] QJsonObject startPortalInput(int timeoutMs);
[[nodiscard]] QJsonObject stopPortalInput(int timeoutMs);
void recordPortalInputStartupFailure(
    const ProbeError& error,
    const QString& startupToken) noexcept;

}
