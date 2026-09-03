#include "ui_probe/AtSpiProbe.hpp"
#include "ui_probe/InputAdapter.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <optional>

namespace {

constexpr qsizetype MaximumOutputBytes = 4 * 1024 * 1024;

void writeStderr(const QString& message)
{
    const auto bytes = message.toUtf8() + '\n';
    static_cast<void>(std::fwrite(
        bytes.constData(),
        1,
        static_cast<std::size_t>(bytes.size()),
        stderr));
    std::fflush(stderr);
}

void writeJson(const QJsonObject& object)
{
    const auto output =
        QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (output.size() > MaximumOutputBytes) {
        throw kodosi::ui_probe::ProbeError(
            kodosi::ui_probe::ExitCode::LimitExceeded,
            QStringLiteral("output-limit"),
            QStringLiteral("JSON output exceeds the 4 MiB limit"));
    }
    static_cast<void>(std::fwrite(
        output.constData(),
        1,
        static_cast<std::size_t>(output.size()),
        stdout));
    std::fflush(stdout);
}

QJsonObject help()
{
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("help")},
        {QStringLiteral("program"), QStringLiteral("kodosi-ui-probe")},
        {QStringLiteral("purpose"),
         QStringLiteral(
             "Inspect and operate a real running Qt application through "
             "official AT-SPI2 and xdg-desktop-portal interfaces.")},
        {QStringLiteral("usage"),
         QJsonArray{
             QStringLiteral("kodosi-ui-probe apps"),
             QStringLiteral(
                 "kodosi-ui-probe tree [--app <name-or-id>] [--pid PID] "
                 "[--depth N]"),
             QStringLiteral(
                 "kodosi-ui-probe find [--app <name-or-id>] [--pid PID] "
                 "[--id exact] [--name exact] [--role role] "
                 "[--contains text]"),
             QStringLiteral("kodosi-ui-probe inspect <handle>"),
             QStringLiteral("kodosi-ui-probe click <handle>"),
             QStringLiteral("kodosi-ui-probe focus <handle>"),
             QStringLiteral(
                 "kodosi-ui-probe set-text <handle> --text <value>"),
             QStringLiteral(
                 "kodosi-ui-probe wait [--app <name-or-id>] [--pid PID] "
                 "<selector> "
                 "[--state state] [--text text] [--timeout-ms N]"),
             QStringLiteral(
                 "kodosi-ui-probe screenshot --output <absolute-path> "
                 "[--interactive] [--timeout-ms N]"),
             QStringLiteral(
                 "kodosi-ui-probe key --key <keysym-or-name> "
                 "[--down|--up|--press] [--adapter auto|atspi|portal]"),
             QStringLiteral(
                 "kodosi-ui-probe shortcut --keys Ctrl+Shift+P "
                 "[--adapter auto|atspi|portal]"),
             QStringLiteral(
                 "kodosi-ui-probe pointer (--x X --y Y | --dx DX --dy DY | "
                 "--element HANDLE [--position center]) "
                 "[--adapter auto|atspi|portal]"),
             QStringLiteral(
                 "kodosi-ui-probe button --button left|middle|right "
                 "[--down|--up|--click] "
                 "[--element HANDLE [--position center]] "
                 "[--adapter auto|atspi|portal]"),
             QStringLiteral(
                 "kodosi-ui-probe drag --from-x X --from-y Y "
                 "--to-x X --to-y Y [--duration-ms N] "
                 "[--adapter auto|atspi|portal]"),
             QStringLiteral("kodosi-ui-probe input-status"),
             QStringLiteral(
                 "kodosi-ui-probe input-start [--timeout-ms N]"),
             QStringLiteral(
                 "kodosi-ui-probe input-stop [--timeout-ms N]"),
             QStringLiteral("kodosi-ui-probe input-serve"),
             QStringLiteral("kodosi-ui-probe doctor"),
         }},
        {QStringLiteral("selectorSyntax"),
         QJsonArray{
             QStringLiteral("id=stable.accessible.id"),
             QStringLiteral("name=Exact accessible name"),
             QStringLiteral("role=button"),
             QStringLiteral("contains=case-insensitive text"),
         }},
        {QStringLiteral("examples"),
         QJsonArray{
             QStringLiteral("kodosi-ui-probe apps"),
             QStringLiteral(
                 "kodosi-ui-probe find --app Kodosi "
                 "--id header.utility.menu"),
             QStringLiteral(
                 "kodosi-ui-probe find --pid 12345 --app Kodosi "
                 "--id header.utility.menu"),
             QStringLiteral(
                 "kodosi-ui-probe click "
                 "\"$(kodosi-ui-probe find --app Kodosi "
                 "--id header.utility.menu | jq -r '.matches[0].handle')\""),
             QStringLiteral(
                 "kodosi-ui-probe wait --app Kodosi "
                 "id=panel.utility.content --state showing --timeout-ms 5000"),
             QStringLiteral(
                 "kodosi-ui-probe set-text <handle> "
                 "--text 'JetBrains Mono'"),
             QStringLiteral(
                 "kodosi-ui-probe screenshot "
                 "--output \"$PWD/kodosi.png\" --interactive"),
         }},
        {QStringLiteral("exitCodes"),
         QJsonObject{
             {QStringLiteral("0"), QStringLiteral("success")},
             {QStringLiteral("2"), QStringLiteral("usage error")},
             {QStringLiteral("3"), QStringLiteral("environment unavailable")},
             {QStringLiteral("4"), QStringLiteral("not found or disappeared")},
             {QStringLiteral("5"), QStringLiteral("ambiguous target")},
             {QStringLiteral("6"), QStringLiteral("unsupported interface")},
             {QStringLiteral("7"), QStringLiteral("D-Bus/remote error")},
             {QStringLiteral("8"), QStringLiteral("bounded timeout")},
             {QStringLiteral("9"), QStringLiteral("invalid handle")},
             {QStringLiteral("10"), QStringLiteral("portal denied/cancelled")},
             {QStringLiteral("11"), QStringLiteral("safety limit exceeded")},
         }},
        {QStringLiteral("notes"),
         QJsonArray{
             QStringLiteral(
                 "Element handles contain only an AT-SPI unique bus name "
                 "and object path; reacquire handles after app changes."),
             QStringLiteral(
                 "Semantic operations never use pixel coordinates as identity."),
             QStringLiteral(
                 "Screenshots always use xdg-desktop-portal and preserve "
                 "portal consent and security policy."),
             QStringLiteral(
                 "Wayland raw input uses one consented RemoteDesktop portal "
                 "session owned by a private per-user Unix sidecar."),
             QStringLiteral(
                 "Coordinates are event parameters only; --element resolves "
                 "fresh AT-SPI screen bounds before sending the event."),
         }},
    };
}

QString takeRequiredOption(
    QStringList* arguments,
    const QString& name)
{
    const auto index = arguments->indexOf(name);
    if (index < 0 || index + 1 >= arguments->size()) {
        throw kodosi::ui_probe::ProbeError(
            kodosi::ui_probe::ExitCode::Usage,
            QStringLiteral("missing-option"),
            QStringLiteral("%1 requires a value").arg(name));
    }
    const auto value = arguments->at(index + 1);
    arguments->removeAt(index + 1);
    arguments->removeAt(index);
    return value;
}

QString takeOptionalOption(
    QStringList* arguments,
    const QString& name)
{
    const auto index = arguments->indexOf(name);
    if (index < 0) {
        return {};
    }
    if (index + 1 >= arguments->size()) {
        throw kodosi::ui_probe::ProbeError(
            kodosi::ui_probe::ExitCode::Usage,
            QStringLiteral("missing-option"),
            QStringLiteral("%1 requires a value").arg(name));
    }
    const auto value = arguments->at(index + 1);
    arguments->removeAt(index + 1);
    arguments->removeAt(index);
    return value;
}

bool takeFlag(QStringList* arguments, const QString& name)
{
    const auto index = arguments->indexOf(name);
    if (index < 0) {
        return false;
    }
    arguments->removeAt(index);
    return true;
}

int parseBoundedInt(
    const QString& value,
    const QString& option,
    const int minimum,
    const int maximum)
{
    bool valid = false;
    const auto parsed = value.toInt(&valid);
    if (!valid || parsed < minimum || parsed > maximum) {
        throw kodosi::ui_probe::ProbeError(
            kodosi::ui_probe::ExitCode::Usage,
            QStringLiteral("invalid-integer"),
            QStringLiteral("%1 must be an integer between %2 and %3")
                .arg(option)
                .arg(minimum)
                .arg(maximum));
    }
    return parsed;
}

struct ApplicationSelector {
    QString app;
    std::optional<qint64> processId;
};

ApplicationSelector takeApplicationSelector(QStringList* arguments)
{
    ApplicationSelector selector;
    selector.app = takeOptionalOption(arguments, QStringLiteral("--app"));
    const auto processId =
        takeOptionalOption(arguments, QStringLiteral("--pid"));
    if (!processId.isEmpty()) {
        selector.processId = parseBoundedInt(
            processId,
            QStringLiteral("--pid"),
            1,
            std::numeric_limits<int>::max());
    }
    if (selector.app.isEmpty() && !selector.processId.has_value()) {
        throw kodosi::ui_probe::ProbeError(
            kodosi::ui_probe::ExitCode::Usage,
            QStringLiteral("missing-app"),
            QStringLiteral("--app or --pid is required"));
    }
    return selector;
}

kodosi::ui_probe::Selector takeSelector(QStringList* arguments)
{
    kodosi::ui_probe::Selector selector;
    selector.id = takeOptionalOption(arguments, QStringLiteral("--id"));
    selector.name = takeOptionalOption(arguments, QStringLiteral("--name"));
    selector.role = takeOptionalOption(arguments, QStringLiteral("--role"));
    selector.contains =
        takeOptionalOption(arguments, QStringLiteral("--contains"));
    return selector;
}

void rejectRemaining(const QStringList& arguments)
{
    if (!arguments.isEmpty()) {
        throw kodosi::ui_probe::ProbeError(
            kodosi::ui_probe::ExitCode::Usage,
            QStringLiteral("unexpected-argument"),
            QStringLiteral("Unexpected argument '%1'").arg(arguments.first()));
    }
}

QJsonObject executeWait(QStringList arguments)
{
    const auto application = takeApplicationSelector(&arguments);
    auto selector = takeSelector(&arguments);
    if (!arguments.isEmpty()
        && !arguments.first().startsWith(QStringLiteral("--"))) {
        const auto shorthand = arguments.takeFirst();
        const auto separator = shorthand.indexOf(QLatin1Char('='));
        if (separator <= 0 || separator == shorthand.size() - 1) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("invalid-selector"),
                QStringLiteral(
                    "wait selector must be id=, name=, role=, or contains="));
        }
        const auto key = shorthand.first(separator);
        const auto value = shorthand.sliced(separator + 1);
        if (key == QStringLiteral("id") && selector.id.isEmpty()) {
            selector.id = value;
        } else if (key == QStringLiteral("name")
                   && selector.name.isEmpty()) {
            selector.name = value;
        } else if (key == QStringLiteral("role")
                   && selector.role.isEmpty()) {
            selector.role = value;
        } else if (key == QStringLiteral("contains")
                   && selector.contains.isEmpty()) {
            selector.contains = value;
        } else {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("invalid-selector"),
                QStringLiteral("Unsupported or duplicate wait selector"));
        }
    }
    const auto state =
        takeOptionalOption(&arguments, QStringLiteral("--state"));
    const auto text =
        takeOptionalOption(&arguments, QStringLiteral("--text"));
    const auto timeoutValue =
        takeOptionalOption(&arguments, QStringLiteral("--timeout-ms"));
    const auto timeout =
        timeoutValue.isEmpty()
        ? 30000
        : parseBoundedInt(
              timeoutValue,
              QStringLiteral("--timeout-ms"),
              0,
              kodosi::ui_probe::AtSpiProbe::MaximumWaitTimeoutMs);
    rejectRemaining(arguments);
    kodosi::ui_probe::AtSpiProbe probe(
        kodosi::ui_probe::AtSpiProbe::DefaultCallTimeoutMs,
        timeout);
    return probe.wait(
        application.app,
        application.processId,
        selector,
        state,
        text,
        timeout);
}

QJsonObject execute(QStringList arguments)
{
    if (arguments.isEmpty()
        || arguments.first() == QStringLiteral("--help")
        || arguments.first() == QStringLiteral("-h")
        || arguments.first() == QStringLiteral("help")) {
        return help();
    }

    const auto command = arguments.takeFirst();
    if (command == QStringLiteral("input-serve")) {
        const auto deadlineValue = takeRequiredOption(
            &arguments,
            QStringLiteral("--deadline-monotonic-ms"));
        const auto startupToken = takeRequiredOption(
            &arguments,
            QStringLiteral("--startup-token"));
        const auto lifecycleDescriptorValue = takeRequiredOption(
            &arguments,
            QStringLiteral("--lifecycle-fd"));
        const auto launcherPidValue = takeRequiredOption(
            &arguments,
            QStringLiteral("--launcher-pid"));
        bool validDeadline = false;
        const auto deadline = deadlineValue.toLongLong(&validDeadline);
        if (!validDeadline || deadline <= 0) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("invalid-sidecar-deadline"),
                QStringLiteral(
                    "--deadline-monotonic-ms must be a positive integer"));
        }
        if (startupToken.size() != 32
            || std::ranges::any_of(
                startupToken,
                [](const QChar character) {
                    return !character.isLetterOrNumber()
                        && character != QLatin1Char('_')
                        && character != QLatin1Char('-');
                })) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("invalid-startup-token"),
                QStringLiteral(
                    "--startup-token must be a 32-character safe token"));
        }
        const auto lifecycleDescriptor = parseBoundedInt(
            lifecycleDescriptorValue,
            QStringLiteral("--lifecycle-fd"),
            3,
            std::numeric_limits<int>::max());
        bool validLauncherPid = false;
        const auto launcherPid =
            launcherPidValue.toLongLong(&validLauncherPid);
        if (!validLauncherPid || launcherPid <= 0) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("invalid-launcher-pid"),
                QStringLiteral("--launcher-pid must be a positive integer"));
        }
        rejectRemaining(arguments);
        try {
            return kodosi::ui_probe::servePortalInput(
                deadline,
                lifecycleDescriptor,
                launcherPid,
                startupToken);
        } catch (const kodosi::ui_probe::ProbeError& error) {
            if (error.kind()
                != QStringLiteral("sidecar-start-cancelled")) {
                kodosi::ui_probe::recordPortalInputStartupFailure(
                    error,
                    startupToken);
            }
            throw;
        }
    }
    if (command == QStringLiteral("input-start")
        || command == QStringLiteral("input-stop")) {
        const auto timeoutValue =
            takeOptionalOption(&arguments, QStringLiteral("--timeout-ms"));
        const auto timeout =
            timeoutValue.isEmpty()
            ? 120000
            : parseBoundedInt(
                  timeoutValue,
                  QStringLiteral("--timeout-ms"),
                  100,
                  kodosi::ui_probe::AtSpiProbe::MaximumWaitTimeoutMs);
        rejectRemaining(arguments);
        return command == QStringLiteral("input-start")
            ? kodosi::ui_probe::startPortalInput(timeout)
            : kodosi::ui_probe::stopPortalInput(timeout);
    }
    if (command == QStringLiteral("input-status")) {
        rejectRemaining(arguments);
        return kodosi::ui_probe::inputStatus();
    }
    if (command == QStringLiteral("key")) {
        const auto key =
            takeRequiredOption(&arguments, QStringLiteral("--key"));
        const auto down = takeFlag(&arguments, QStringLiteral("--down"));
        const auto up = takeFlag(&arguments, QStringLiteral("--up"));
        const auto press = takeFlag(&arguments, QStringLiteral("--press"));
        const auto adapter =
            takeOptionalOption(&arguments, QStringLiteral("--adapter"));
        if (static_cast<int>(down) + static_cast<int>(up)
                + static_cast<int>(press)
            > 1) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("conflicting-key-state"),
                QStringLiteral("Use only one of --down, --up, or --press"));
        }
        rejectRemaining(arguments);
        return kodosi::ui_probe::executeInput(
            kodosi::ui_probe::keyPlan(
                key,
                down || press || (!down && !up && !press),
                up || press || (!down && !up)),
            adapter.isEmpty() ? QStringLiteral("auto") : adapter);
    }
    if (command == QStringLiteral("shortcut")) {
        const auto keys =
            takeRequiredOption(&arguments, QStringLiteral("--keys"));
        const auto adapter =
            takeOptionalOption(&arguments, QStringLiteral("--adapter"));
        rejectRemaining(arguments);
        return kodosi::ui_probe::executeInput(
            kodosi::ui_probe::shortcutPlan(keys),
            adapter.isEmpty() ? QStringLiteral("auto") : adapter);
    }
    if (command == QStringLiteral("pointer")) {
        const auto x =
            takeOptionalOption(&arguments, QStringLiteral("--x"));
        const auto y =
            takeOptionalOption(&arguments, QStringLiteral("--y"));
        const auto dx =
            takeOptionalOption(&arguments, QStringLiteral("--dx"));
        const auto dy =
            takeOptionalOption(&arguments, QStringLiteral("--dy"));
        const auto element =
            takeOptionalOption(&arguments, QStringLiteral("--element"));
        auto position =
            takeOptionalOption(&arguments, QStringLiteral("--position"));
        const auto adapter =
            takeOptionalOption(&arguments, QStringLiteral("--adapter"));
        kodosi::ui_probe::InputPlan plan;
        if (!element.isEmpty()) {
            if (!x.isEmpty() || !y.isEmpty() || !dx.isEmpty()
                || !dy.isEmpty()) {
                throw kodosi::ui_probe::ProbeError(
                    kodosi::ui_probe::ExitCode::Usage,
                    QStringLiteral("conflicting-pointer-target"),
                    QStringLiteral(
                        "--element cannot be combined with coordinates"));
            }
            if (position.isEmpty()) {
                position = QStringLiteral("center");
            }
            kodosi::ui_probe::AtSpiProbe probe;
            const auto point = probe.elementPoint(element, position);
            plan = kodosi::ui_probe::pointerPlan(
                point.x(),
                point.y(),
                false);
            plan.details.insert(QStringLiteral("element"), element);
            plan.details.insert(QStringLiteral("position"), position);
        } else {
            const auto absolute = !x.isEmpty() || !y.isEmpty();
            const auto relative = !dx.isEmpty() || !dy.isEmpty();
            if (absolute == relative
                || (absolute && (x.isEmpty() || y.isEmpty()))
                || (relative && (dx.isEmpty() || dy.isEmpty()))) {
                throw kodosi::ui_probe::ProbeError(
                    kodosi::ui_probe::ExitCode::Usage,
                    QStringLiteral("invalid-pointer-coordinates"),
                    QStringLiteral(
                        "Use exactly --x/--y, --dx/--dy, or --element"));
            }
            plan = kodosi::ui_probe::pointerPlan(
                parseBoundedInt(
                    absolute ? x : dx,
                    absolute ? QStringLiteral("--x")
                             : QStringLiteral("--dx"),
                    -kodosi::ui_probe::InputProtocol::MaximumCoordinate,
                    kodosi::ui_probe::InputProtocol::MaximumCoordinate),
                parseBoundedInt(
                    absolute ? y : dy,
                    absolute ? QStringLiteral("--y")
                             : QStringLiteral("--dy"),
                    -kodosi::ui_probe::InputProtocol::MaximumCoordinate,
                    kodosi::ui_probe::InputProtocol::MaximumCoordinate),
                relative);
        }
        rejectRemaining(arguments);
        return kodosi::ui_probe::executeInput(
            plan,
            adapter.isEmpty() ? QStringLiteral("auto") : adapter);
    }
    if (command == QStringLiteral("button")) {
        const auto button =
            takeRequiredOption(&arguments, QStringLiteral("--button"));
        const auto element =
            takeOptionalOption(&arguments, QStringLiteral("--element"));
        auto position =
            takeOptionalOption(&arguments, QStringLiteral("--position"));
        const auto down = takeFlag(&arguments, QStringLiteral("--down"));
        const auto up = takeFlag(&arguments, QStringLiteral("--up"));
        const auto click = takeFlag(&arguments, QStringLiteral("--click"));
        const auto adapter =
            takeOptionalOption(&arguments, QStringLiteral("--adapter"));
        if (static_cast<int>(down) + static_cast<int>(up)
                + static_cast<int>(click)
            > 1) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("conflicting-button-state"),
                QStringLiteral("Use only one of --down, --up, or --click"));
        }
        auto plan = kodosi::ui_probe::buttonPlan(
            button,
            down || click || (!down && !up),
            up || click || (!down && !up));
        if (!element.isEmpty()) {
            if (position.isEmpty()) {
                position = QStringLiteral("center");
            }
            kodosi::ui_probe::AtSpiProbe probe;
            const auto point = probe.elementPoint(element, position);
            const auto pointer =
                kodosi::ui_probe::pointerPlan(
                    point.x(),
                    point.y(),
                    false);
            plan.events.prepend(pointer.events.first());
            plan.details.insert(QStringLiteral("element"), element);
            plan.details.insert(QStringLiteral("position"), position);
        } else if (!position.isEmpty()) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("position-without-element"),
                QStringLiteral("--position requires --element"));
        }
        rejectRemaining(arguments);
        return kodosi::ui_probe::executeInput(
            plan,
            adapter.isEmpty() ? QStringLiteral("auto") : adapter);
    }
    if (command == QStringLiteral("drag")) {
        const auto fromX = takeRequiredOption(
            &arguments,
            QStringLiteral("--from-x"));
        const auto fromY = takeRequiredOption(
            &arguments,
            QStringLiteral("--from-y"));
        const auto toX =
            takeRequiredOption(&arguments, QStringLiteral("--to-x"));
        const auto toY =
            takeRequiredOption(&arguments, QStringLiteral("--to-y"));
        const auto durationValue =
            takeOptionalOption(&arguments, QStringLiteral("--duration-ms"));
        const auto adapter =
            takeOptionalOption(&arguments, QStringLiteral("--adapter"));
        rejectRemaining(arguments);
        const auto coordinate = [](const QString& value, const QString& name) {
            return parseBoundedInt(
                value,
                name,
                -kodosi::ui_probe::InputProtocol::MaximumCoordinate,
                kodosi::ui_probe::InputProtocol::MaximumCoordinate);
        };
        return kodosi::ui_probe::executeInput(
            kodosi::ui_probe::dragPlan(
                coordinate(fromX, QStringLiteral("--from-x")),
                coordinate(fromY, QStringLiteral("--from-y")),
                coordinate(toX, QStringLiteral("--to-x")),
                coordinate(toY, QStringLiteral("--to-y")),
                durationValue.isEmpty()
                    ? 250
                    : parseBoundedInt(
                          durationValue,
                          QStringLiteral("--duration-ms"),
                          0,
                          kodosi::ui_probe::InputProtocol::
                              MaximumDragDurationMs)),
            adapter.isEmpty() ? QStringLiteral("auto") : adapter);
    }
    if (command == QStringLiteral("screenshot")) {
        const auto output =
            takeRequiredOption(&arguments, QStringLiteral("--output"));
        const auto interactive =
            takeFlag(&arguments, QStringLiteral("--interactive"));
        const auto timeoutValue =
            takeOptionalOption(&arguments, QStringLiteral("--timeout-ms"));
        const auto timeout =
            timeoutValue.isEmpty()
            ? 30000
            : parseBoundedInt(
                  timeoutValue,
                  QStringLiteral("--timeout-ms"),
                  100,
                  kodosi::ui_probe::AtSpiProbe::MaximumWaitTimeoutMs);
        rejectRemaining(arguments);
        return kodosi::ui_probe::takePortalScreenshot(
            output,
            interactive,
            timeout);
    }

    if (command == QStringLiteral("doctor")) {
        rejectRemaining(arguments);
        try {
            kodosi::ui_probe::AtSpiProbe probe(
                kodosi::ui_probe::AtSpiProbe::DefaultCallTimeoutMs,
                30000);
            auto result = probe.doctor();
            result.insert(
                QStringLiteral("input"),
                kodosi::ui_probe::inputStatus());
            return result;
        } catch (const kodosi::ui_probe::ProbeError& error) {
            const auto sessionBus = QDBusConnection::sessionBus();
            const auto busInterface = sessionBus.interface();
            const auto portalRegistered =
                busInterface != nullptr
                && busInterface
                       ->isServiceRegistered(
                           QStringLiteral(
                               "org.freedesktop.portal.Desktop"))
                       .value();
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("command"), QStringLiteral("doctor")},
                {QStringLiteral("sessionBus"),
                 QJsonObject{
                     {QStringLiteral("connected"),
                      sessionBus.isConnected()},
                     {QStringLiteral("baseService"),
                      sessionBus.baseService()},
                 }},
                {QStringLiteral("accessibility"),
                 QJsonObject{
                     {QStringLiteral("busConnected"), false},
                     {QStringLiteral("registryVisible"), false},
                     {QStringLiteral("errorKind"), error.kind()},
                     {QStringLiteral("error"), error.message()},
                 }},
                {QStringLiteral("portal"),
                 QJsonObject{
                     {QStringLiteral("registered"), portalRegistered},
                     {QStringLiteral("screenshotAvailable"), false},
                 }},
                 {QStringLiteral("input"),
                  kodosi::ui_probe::inputStatus()},
                {QStringLiteral("desktop"),
                 QJsonObject{
                     {QStringLiteral("sessionType"),
                      qEnvironmentVariable("XDG_SESSION_TYPE")},
                     {QStringLiteral("currentDesktop"),
                      qEnvironmentVariable("XDG_CURRENT_DESKTOP")},
                     {QStringLiteral("display"),
                      qEnvironmentVariable("DISPLAY")},
                     {QStringLiteral("waylandDisplay"),
                      qEnvironmentVariable("WAYLAND_DISPLAY")},
                 }},
                {QStringLiteral("gaps"),
                 QJsonArray{
                     error.message(),
                     QStringLiteral(
                         "Ensure AT-SPI2 is enabled and org.a11y.Bus is "
                         "available in this desktop session."),
                 }},
            };
        }
    }

    if (command == QStringLiteral("wait")) {
        return executeWait(arguments);
    }

    kodosi::ui_probe::AtSpiProbe probe(
        kodosi::ui_probe::AtSpiProbe::DefaultCallTimeoutMs,
        30000);
    if (command == QStringLiteral("apps")) {
        rejectRemaining(arguments);
        return probe.apps();
    }
    if (command == QStringLiteral("tree")) {
        const auto application = takeApplicationSelector(&arguments);
        const auto depthValue =
            takeOptionalOption(&arguments, QStringLiteral("--depth"));
        const auto depth =
            depthValue.isEmpty()
            ? kodosi::ui_probe::AtSpiProbe::DefaultDepth
            : parseBoundedInt(
                  depthValue,
                  QStringLiteral("--depth"),
                  0,
                  kodosi::ui_probe::AtSpiProbe::MaximumDepth);
        rejectRemaining(arguments);
        return probe.tree(
            application.app,
            application.processId,
            depth);
    }
    if (command == QStringLiteral("find")) {
        const auto application = takeApplicationSelector(&arguments);
        const auto selector = takeSelector(&arguments);
        rejectRemaining(arguments);
        return probe.find(
            application.app,
            application.processId,
            selector);
    }
    if (command == QStringLiteral("inspect")
        || command == QStringLiteral("click")
        || command == QStringLiteral("focus")) {
        if (arguments.size() != 1) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("handle-required"),
                QStringLiteral("%1 requires exactly one element handle")
                    .arg(command));
        }
        if (command == QStringLiteral("inspect")) {
            return probe.inspectHandle(arguments.first());
        }
        if (command == QStringLiteral("click")) {
            return probe.click(arguments.first());
        }
        return probe.focus(arguments.first());
    }
    if (command == QStringLiteral("set-text")) {
        if (arguments.isEmpty()) {
            throw kodosi::ui_probe::ProbeError(
                kodosi::ui_probe::ExitCode::Usage,
                QStringLiteral("handle-required"),
                QStringLiteral("set-text requires an element handle"));
        }
        const auto handle = arguments.takeFirst();
        const auto text =
            takeRequiredOption(&arguments, QStringLiteral("--text"));
        rejectRemaining(arguments);
        return probe.setText(handle, text);
    }
    throw kodosi::ui_probe::ProbeError(
        kodosi::ui_probe::ExitCode::Usage,
        QStringLiteral("unknown-command"),
        QStringLiteral("Unknown command '%1'; use --help").arg(command));
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kodosi-ui-probe"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    try {
        auto arguments = application.arguments();
        arguments.removeFirst();
        writeJson(execute(arguments));
        return static_cast<int>(kodosi::ui_probe::ExitCode::Success);
    } catch (const kodosi::ui_probe::ProbeError& error) {
        writeStderr(error.message());
        try {
            writeJson({
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QJsonObject{
                     {QStringLiteral("kind"), error.kind()},
                     {QStringLiteral("message"), error.message()},
                     {QStringLiteral("exitCode"),
                      static_cast<int>(error.code())},
                 }},
            });
        } catch (...) {
            writeStderr(QStringLiteral("Failed to serialize error output"));
        }
        return static_cast<int>(error.code());
    } catch (const std::exception& error) {
        const auto message =
            QStringLiteral("Unexpected failure: %1")
                .arg(QString::fromUtf8(error.what()));
        writeStderr(message);
        try {
            writeJson({
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QJsonObject{
                     {QStringLiteral("kind"),
                      QStringLiteral("unexpected-error")},
                     {QStringLiteral("message"), message},
                     {QStringLiteral("exitCode"),
                      static_cast<int>(
                          kodosi::ui_probe::ExitCode::RemoteError)},
                 }},
            });
        } catch (...) {
        }
        return static_cast<int>(kodosi::ui_probe::ExitCode::RemoteError);
    }
}
