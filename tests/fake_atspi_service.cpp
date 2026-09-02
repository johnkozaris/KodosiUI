#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <csignal>
#include <cstdio>
#include <limits>
#include <sys/prctl.h>

namespace fake_types {

struct IntPair {
    int first = 0;
    int second = 0;
};

struct PortalStream {
    quint32 nodeId = 0;
    QVariantMap properties;
};

using PortalStreams = QList<PortalStream>;

QDBusArgument& operator<<(QDBusArgument& argument, const IntPair& pair)
{
    argument.beginStructure();
    argument << pair.first << pair.second;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, IntPair& pair)
{
    argument.beginStructure();
    argument >> pair.first >> pair.second;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(
    QDBusArgument& argument,
    const PortalStream& stream)
{
    argument.beginStructure();
    argument << stream.nodeId << stream.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(
    const QDBusArgument& argument,
    PortalStream& stream)
{
    argument.beginStructure();
    argument >> stream.nodeId >> stream.properties;
    argument.endStructure();
    return argument;
}

} // namespace fake_types

Q_DECLARE_METATYPE(fake_types::IntPair)
Q_DECLARE_METATYPE(fake_types::PortalStream)
Q_DECLARE_METATYPE(fake_types::PortalStreams)

namespace {

struct ObjectReference {
    QString busName;
    QDBusObjectPath path;
};

struct Action {
    QString name;
    QString description;
    QString keyBinding;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

using ObjectReferences = QList<ObjectReference>;
using Actions = QList<Action>;

QDBusArgument& operator<<(
    QDBusArgument& argument,
    const ObjectReference& reference)
{
    argument.beginStructure();
    argument << reference.busName << reference.path;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(
    const QDBusArgument& argument,
    ObjectReference& reference)
{
    argument.beginStructure();
    argument >> reference.busName >> reference.path;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const Action& action)
{
    argument.beginStructure();
    argument << action.name << action.description << action.keyBinding;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, Action& action)
{
    argument.beginStructure();
    argument >> action.name >> action.description >> action.keyBinding;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const Rect& rect)
{
    argument.beginStructure();
    argument << rect.x << rect.y << rect.width << rect.height;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, Rect& rect)
{
    argument.beginStructure();
    argument >> rect.x >> rect.y >> rect.width >> rect.height;
    argument.endStructure();
    return argument;
}

class FakeAtSpi final : public QDBusVirtualObject
{
public:
    explicit FakeAtSpi(QString busName, QString secondBusName = {})
        : m_busName(std::move(busName))
        , m_secondBusName(std::move(secondBusName))
    {
    }

    [[nodiscard]] QString introspect(const QString& path) const override
    {
        if (path
            == QStringLiteral(
                "/org/a11y/atspi/registry/deviceeventcontroller")) {
            const auto keyboardMethod =
                QFileInfo::exists(qEnvironmentVariable(
                    "KODOSI_FAKE_ATSPI_MISSING_METHOD_FILE"))
                ? QString()
                : QStringLiteral(
                      "<method name=\"GenerateKeyboardEvent\"/>");
            return QStringLiteral(
                       "<node><interface name=\"org.a11y.atspi."
                       "DeviceEventController\">%1"
                       "<method name=\"GenerateMouseEvent\"/>"
                       "</interface></node>")
                .arg(keyboardMethod);
        }
        if (path == QStringLiteral("/font_size")) {
            return QStringLiteral(
                "<node><interface name=\"org.a11y.atspi.Value\">"
                "<property name=\"Text\" type=\"s\" access=\"read\"/>"
                "</interface></node>");
        }
        return QStringLiteral("<node/>");
    }

    bool handleMessage(
        const QDBusMessage& message,
        const QDBusConnection& connection) override
    {
        if (message.path() == QStringLiteral("/org/a11y/bus")
            && message.interface() == QStringLiteral("org.a11y.Bus")
            && message.member() == QStringLiteral("GetAddress")) {
            connection.send(message.createReply(
                qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS")));
            return true;
        }
        if (message.interface()
                == QStringLiteral("org.freedesktop.DBus.Introspectable")
            && message.member() == QStringLiteral("Introspect")) {
            connection.send(
                message.createReply(introspect(message.path())));
            return true;
        }

        if (message.interface()
                == QStringLiteral("org.freedesktop.DBus.Properties")
            && (message.member() == QStringLiteral("GetAll")
                || message.member() == QStringLiteral("Get"))) {
            if (!message.arguments().isEmpty()
                && (message.arguments().first().toString()
                        == QStringLiteral(
                            "org.freedesktop.portal.RemoteDesktop")
                    || message.arguments().first().toString()
                        == QStringLiteral(
                            "org.freedesktop.portal.ScreenCast"))) {
                return portalProperties(message, connection);
            }
            return properties(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.freedesktop.portal.RemoteDesktop")) {
            return remoteDesktop(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.freedesktop.portal.ScreenCast")) {
            return screenCast(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.freedesktop.portal.Request")) {
            return portalRequest(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.freedesktop.portal.Session")) {
            return portalSession(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.a11y.atspi.DeviceEventController")) {
            return deviceController(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.a11y.atspi.Accessible")) {
            return accessible(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.a11y.atspi.Component")) {
            return component(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.a11y.atspi.Action")) {
            return action(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.a11y.atspi.Text")) {
            return text(message, connection);
        }
        if (message.interface()
            == QStringLiteral("org.a11y.atspi.EditableText")) {
            return editableText(message, connection);
        }
        connection.send(message.createErrorReply(
            QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
            QStringLiteral("Unsupported fake method")));
        return true;
    }

private:
    QString m_busName;
    QString m_secondBusName;
    bool m_panelVisible = false;
    QString m_text =
        QString(70000, QLatin1Char('x'))
        + QStringLiteral("needle-at-end");
    QString m_portalSession =
        QStringLiteral("/org/freedesktop/portal/desktop/session/fake/session");
    bool m_sourcesSelected = false;

    void logInput(const QJsonObject& event)
    {
        const auto path = qEnvironmentVariable("KODOSI_FAKE_INPUT_LOG");
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            auto line =
                QJsonDocument(event).toJson(QJsonDocument::Compact);
            line.append('\n');
            static_cast<void>(file.write(line));
        }
    }

    static QString requestPath(
        const QDBusMessage& message,
        const QVariantMap& options)
    {
        auto sender = message.service();
        sender.remove(QLatin1Char(':'));
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        return QStringLiteral(
                   "/org/freedesktop/portal/desktop/request/%1/%2")
            .arg(
                sender,
                options.value(QStringLiteral("handle_token")).toString());
    }

    static QVariantMap variantMap(const QVariant& value)
    {
        if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
            return value.toMap();
        }
        return qdbus_cast<QVariantMap>(value);
    }

    static void sendPortalResponse(
        const QDBusConnection& connection,
        const QString& path,
        const QVariantMap& results,
        const quint32 code = 0U,
        const int delayMs = 0)
    {
        const auto send =
            [connection, path, results, code] {
                auto response = QDBusMessage::createSignal(
                    path,
                    QStringLiteral("org.freedesktop.portal.Request"),
                    QStringLiteral("Response"));
                response.setArguments(
                    {QVariant::fromValue<quint32>(code), results});
                connection.send(response);
            };
        if (delayMs == 0) {
            send();
            return;
        }
        QTimer::singleShot(
            delayMs,
            send);
    }

    bool portalProperties(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        const auto interface = message.arguments().first().toString();
        QVariantMap properties;
        if (interface
            == QStringLiteral("org.freedesktop.portal.ScreenCast")) {
            properties = {
                {QStringLiteral("version"),
                 QVariant::fromValue<quint32>(5U)},
                {QStringLiteral("AvailableSourceTypes"),
                 QVariant::fromValue<quint32>(3U)},
            };
        } else {
            properties = {
                {QStringLiteral("version"),
                 QVariant::fromValue<quint32>(2U)},
                {QStringLiteral("AvailableDeviceTypes"),
                 QVariant::fromValue<quint32>(3U)},
            };
        }
        if (message.member() == QStringLiteral("Get")) {
            connection.send(message.createReply(
                QVariant::fromValue(QDBusVariant(
                    properties.value(
                        message.arguments().at(1).toString())))));
        } else {
            connection.send(message.createReply(properties));
        }
        return true;
    }

    bool remoteDesktop(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        logInput({
            {QStringLiteral("adapter"), QStringLiteral("portal-control")},
            {QStringLiteral("method"), message.member()},
        });
        if (message.member() == QStringLiteral("CreateSession")) {
            const auto options = variantMap(message.arguments().first());
            const auto request = requestPath(message, options);
            const auto reply = [connection,
                                message,
                                request,
                                session = m_portalSession] {
                connection.send(
                    message.createReply(QDBusObjectPath(request)));
                sendPortalResponse(
                    connection,
                    request,
                    {{QStringLiteral("session_handle"),
                      QVariant::fromValue(QDBusObjectPath(session))}});
            };
            if (QFileInfo::exists(qEnvironmentVariable(
                    "KODOSI_FAKE_PORTAL_METHOD_DELAY_FILE"))) {
                QTimer::singleShot(2000, reply);
            } else {
                reply();
            }
            return true;
        }
        if (message.member() == QStringLiteral("SelectDevices")) {
            const auto options = variantMap(message.arguments().at(1));
            const auto request = requestPath(message, options);
            connection.send(
                message.createReply(QDBusObjectPath(request)));
            if (QFileInfo::exists(qEnvironmentVariable(
                    "KODOSI_FAKE_PORTAL_CLOSE_EARLY_FILE"))) {
                auto closed = QDBusMessage::createSignal(
                    m_portalSession,
                    QStringLiteral("org.freedesktop.portal.Session"),
                    QStringLiteral("Closed"));
                connection.send(closed);
            }
            sendPortalResponse(connection, request, {});
            return true;
        }
        if (message.member() == QStringLiteral("Start")) {
            if (!m_sourcesSelected) {
                connection.send(message.createErrorReply(
                    QStringLiteral("org.example.MissingSelectSources"),
                    QStringLiteral(
                        "ScreenCast.SelectSources must precede Start")));
                return true;
            }
            const auto options = variantMap(message.arguments().at(2));
            const auto request = requestPath(message, options);
            connection.send(
                message.createReply(QDBusObjectPath(request)));
            QVariantMap results{
                {QStringLiteral("devices"),
                 QVariant::fromValue<quint32>(3U)},
                {QStringLiteral("restore_token"),
                 QStringLiteral("fake-restore-token")},
            };
            if (!QFileInfo::exists(qEnvironmentVariable(
                    "KODOSI_FAKE_PORTAL_NO_STREAM_FILE"))) {
                results.insert(
                    QStringLiteral("streams"),
                    QVariant::fromValue(fake_types::PortalStreams{
                        fake_types::PortalStream{
                            42U,
                            {
                                {QStringLiteral("position"),
                                 QVariant::fromValue(
                                     fake_types::IntPair{100, 200})},
                                {QStringLiteral("logical_size"),
                                 QVariant::fromValue(
                                     fake_types::IntPair{800, 600})},
                            },
                        },
                    }));
            }
            sendPortalResponse(
                connection,
                request,
                results,
                QFileInfo::exists(
                    qEnvironmentVariable(
                        "KODOSI_FAKE_PORTAL_DENY_FILE"))
                    ? 2U
                    : 0U);
            return true;
        }
        if (message.member().startsWith(QStringLiteral("Notify"))) {
            QJsonArray arguments;
            for (const auto& argument : message.arguments()) {
                if (argument.metaType()
                    == QMetaType::fromType<QDBusObjectPath>()) {
                    arguments.append(
                        qvariant_cast<QDBusObjectPath>(argument).path());
                } else if (argument.canConvert<double>()) {
                    arguments.append(argument.toDouble());
                }
            }
            logInput({
                {QStringLiteral("adapter"), QStringLiteral("portal")},
                {QStringLiteral("method"), message.member()},
                {QStringLiteral("arguments"), arguments},
            });
            connection.send(message.createReply());
            if (message.member() == QStringLiteral("NotifyKeyboardKeysym")
                && message.arguments().size() >= 4
                && message.arguments().at(2).toInt() == 0xffc9
                && message.arguments().at(3).toUInt() == 1U) {
                QTimer::singleShot(
                    0,
                    [connection, session = m_portalSession] {
                        auto closed = QDBusMessage::createSignal(
                            session,
                            QStringLiteral(
                                "org.freedesktop.portal.Session"),
                            QStringLiteral("Closed"));
                        connection.send(closed);
                    });
            }
            return true;
        }
        connection.send(message.createErrorReply(
            QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
            QStringLiteral("Unknown RemoteDesktop method")));
        return true;
    }

    bool screenCast(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        logInput({
            {QStringLiteral("adapter"), QStringLiteral("portal-control")},
            {QStringLiteral("method"), message.member()},
        });
        if (message.member() == QStringLiteral("SelectSources")) {
            const auto options = variantMap(message.arguments().at(1));
            const auto request = requestPath(message, options);
            connection.send(message.createReply(QDBusObjectPath(request)));
            m_sourcesSelected = true;
            sendPortalResponse(
                connection,
                request,
                {},
                0U,
                QFileInfo::exists(qEnvironmentVariable(
                    "KODOSI_FAKE_PORTAL_DELAY_FILE"))
                    ? 2000
                    : 0);
            return true;
        }
        connection.send(message.createErrorReply(
            QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
            QStringLiteral("Unknown ScreenCast method")));
        return true;
    }

    bool portalRequest(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.member() == QStringLiteral("Close")) {
            logInput({
                {QStringLiteral("adapter"),
                 QStringLiteral("portal-control")},
                {QStringLiteral("method"),
                 QStringLiteral("Request.Close")},
            });
            connection.send(message.createReply());
            return true;
        }
        connection.send(message.createErrorReply(
            QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
            QStringLiteral("Unknown portal request method")));
        return true;
    }

    bool portalSession(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.member() == QStringLiteral("Close")) {
            logInput({
                {QStringLiteral("adapter"),
                 QStringLiteral("portal-control")},
                {QStringLiteral("method"),
                 QStringLiteral("Session.Close")},
            });
            connection.send(message.createReply());
            return true;
        }
        connection.send(message.createErrorReply(
            QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
            QStringLiteral("Unknown portal session method")));
        return true;
    }

    bool deviceController(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.member() == QStringLiteral("GenerateKeyboardEvent")) {
            logInput({
                {QStringLiteral("adapter"), QStringLiteral("atspi")},
                {QStringLiteral("method"), message.member()},
                {QStringLiteral("key"), message.arguments().at(0).toInt()},
                {QStringLiteral("state"),
                 static_cast<qint64>(
                     message.arguments().at(2).toUInt())},
            });
            if (message.arguments().at(0).toInt() == 0xffc9
                && message.arguments().at(2).toUInt() == 3U) {
                connection.send(message.createErrorReply(
                    QStringLiteral("org.example.FakeInputFailure"),
                    QStringLiteral("Synthetic key failure")));
                return true;
            }
            connection.send(message.createReply());
            return true;
        }
        if (message.member() == QStringLiteral("GenerateMouseEvent")) {
            logInput({
                {QStringLiteral("adapter"), QStringLiteral("atspi")},
                {QStringLiteral("method"), message.member()},
                {QStringLiteral("x"), message.arguments().at(0).toInt()},
                {QStringLiteral("y"), message.arguments().at(1).toInt()},
                {QStringLiteral("event"),
                 message.arguments().at(2).toString()},
            });
            if (message.arguments().at(0).toInt() == 999999
                && message.arguments().at(2).toString()
                    == QStringLiteral("abs")) {
                connection.send(message.createErrorReply(
                    QStringLiteral("org.example.FakeInputFailure"),
                    QStringLiteral("Synthetic pointer failure")));
                return true;
            }
            connection.send(message.createReply());
            return true;
        }
        connection.send(message.createErrorReply(
            QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
            QStringLiteral("Unknown DeviceEventController method")));
        return true;
    }

    [[nodiscard]] ObjectReference reference(const QString& path) const
    {
        return {m_busName, QDBusObjectPath(path)};
    }

    [[nodiscard]] ObjectReference secondReference(const QString& path) const
    {
        return {m_secondBusName, QDBusObjectPath(path)};
    }

    [[nodiscard]] QStringList interfaces(const QString& path) const
    {
        QStringList result{
            QStringLiteral("org.a11y.atspi.Accessible"),
            QStringLiteral("org.a11y.atspi.Component"),
        };
        if (path == QStringLiteral("/settings_button")) {
            result.append(QStringLiteral("org.a11y.atspi.Action"));
        }
        if (path == QStringLiteral("/font_family")) {
            result.append(QStringLiteral("org.a11y.atspi.Text"));
            result.append(QStringLiteral("org.a11y.atspi.EditableText"));
        }
        if (path == QStringLiteral("/font_size")
            || path == QStringLiteral("/line_height")) {
            result.append(QStringLiteral("org.a11y.atspi.Value"));
        }
        return result;
    }

    [[nodiscard]] ObjectReferences children(const QString& path) const
    {
        if (path == QStringLiteral("/org/a11y/atspi/accessible/root")) {
            ObjectReferences result{reference(QStringLiteral("/app"))};
            if (!m_secondBusName.isEmpty()) {
                result.append(secondReference(QStringLiteral("/app_two")));
            }
            return result;
        }
        if (path == QStringLiteral("/app")) {
            return {reference(QStringLiteral("/window"))};
        }
        if (path == QStringLiteral("/window")) {
            ObjectReferences result{
                reference(QStringLiteral("/settings_button")),
            };
            if (m_panelVisible) {
                result.append(reference(QStringLiteral("/settings_panel")));
            }
            return result;
        }
        if (path == QStringLiteral("/settings_panel")) {
            return {
                reference(QStringLiteral("/font_family")),
                reference(QStringLiteral("/font_size")),
                reference(QStringLiteral("/line_height")),
            };
        }
        return {};
    }

    [[nodiscard]] QVariantMap propertiesFor(const QString& path) const
    {
        QString name;
        QString id;
        QString description;
        if (path == QStringLiteral("/app")
            || path == QStringLiteral("/app_two")) {
            name = QStringLiteral("Kodosi Fake");
            id = path == QStringLiteral("/app")
                ? QStringLiteral("app.fake")
                : QStringLiteral("app.fake.two");
        } else if (path == QStringLiteral("/window")) {
            name = QStringLiteral("Kodosi");
            id = QStringLiteral("window.main");
        } else if (path == QStringLiteral("/settings_button")) {
            name = QStringLiteral("Settings");
            id = QStringLiteral("header.settings");
        } else if (path == QStringLiteral("/settings_panel")) {
            name = QStringLiteral("Desktop settings");
            id = QStringLiteral("panel.settings");
        } else if (path == QStringLiteral("/font_family")) {
            name = QStringLiteral("Terminal font family");
            id = QStringLiteral("panel.settings.terminal.fontFamily");
        } else if (path == QStringLiteral("/font_size")) {
            name = QStringLiteral("Terminal font size");
            id = QStringLiteral("panel.settings.terminal.fontSize");
        } else if (path == QStringLiteral("/line_height")) {
            name = QStringLiteral("Terminal line height");
            id = QStringLiteral("panel.settings.terminal.lineHeight");
        }
        QVariantMap properties;
        properties.insert(QStringLiteral("Name"), name);
        properties.insert(QStringLiteral("Description"), description);
        properties.insert(QStringLiteral("AccessibleId"), id);
        properties.insert(QStringLiteral("HelpText"), QString());
        properties.insert(
            QStringLiteral("ChildCount"),
            static_cast<int>(children(path).size()));
        return properties;
    }

    [[nodiscard]] QString role(const QString& path) const
    {
        if (path == QStringLiteral("/app")
            || path == QStringLiteral("/app_two")) {
            return QStringLiteral("application");
        }

        if (path == QStringLiteral("/window")) {
            return QStringLiteral("frame");
        }
        if (path == QStringLiteral("/settings_button")) {
            return QStringLiteral("push button");
        }
        if (path == QStringLiteral("/settings_panel")) {
            return QStringLiteral("dialog");
        }
        if (path == QStringLiteral("/font_size")) {
            return QStringLiteral("spin button");
        }
        if (path == QStringLiteral("/line_height")) {
            return QStringLiteral("slider");
        }
        return QStringLiteral("text");
    }

    [[nodiscard]] quint32 roleValue(const QString& path) const
    {
        if (path == QStringLiteral("/app")
            || path == QStringLiteral("/app_two")) {
            return 75;
        }
        if (path == QStringLiteral("/window")) {
            return 23;
        }
        if (path == QStringLiteral("/settings_button")) {
            return 43;
        }
        if (path == QStringLiteral("/settings_panel")) {
            return 16;
        }
        if (path == QStringLiteral("/font_size")) {
            return 52;
        }
        if (path == QStringLiteral("/line_height")) {
            return 51;
        }
        return 61;
    }

    bool properties(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.arguments().isEmpty()) {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownInterface"),
                QStringLiteral("Unknown fake interface")));
            return true;
        }
        const auto interface = message.arguments().first().toString();
        QVariantMap properties;
        if (interface == QStringLiteral("org.a11y.atspi.Accessible")) {
            properties = propertiesFor(message.path());
        } else if (
            interface == QStringLiteral("org.a11y.atspi.Text")
            && message.path() == QStringLiteral("/font_family")) {
            properties.insert(
                QStringLiteral("CharacterCount"),
                static_cast<int>(m_text.size()));
            properties.insert(
                QStringLiteral("CaretOffset"),
                static_cast<int>(m_text.size()));
        } else if (
            interface == QStringLiteral("org.a11y.atspi.Value")
            && message.path() == QStringLiteral("/font_size")) {
            properties.insert(QStringLiteral("CurrentValue"), 14.0);
            properties.insert(QStringLiteral("MinimumValue"), 8.0);
            properties.insert(QStringLiteral("MaximumValue"), 32.0);
            properties.insert(QStringLiteral("MinimumIncrement"), 1.0);
            properties.insert(QStringLiteral("Text"), QStringLiteral("14 pt"));
        } else if (
            interface == QStringLiteral("org.a11y.atspi.Value")
            && message.path() == QStringLiteral("/line_height")) {
            properties.insert(QStringLiteral("CurrentValue"), 1.1);
            properties.insert(QStringLiteral("MinimumValue"), 0.8);
            properties.insert(QStringLiteral("MaximumValue"), 2.0);
            properties.insert(QStringLiteral("MinimumIncrement"), 0.1);
        } else {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownInterface"),
                QStringLiteral("Unknown fake interface")));
            return true;
        }
        if (message.member() == QStringLiteral("Get")) {
            if (message.arguments().size() != 2
                || !properties.contains(
                    message.arguments().at(1).toString())) {
                connection.send(message.createErrorReply(
                    QStringLiteral(
                        "org.freedesktop.DBus.Error.UnknownProperty"),
                    QStringLiteral("Unknown fake property")));
                return true;
            }
            connection.send(message.createReply(
                QVariant::fromValue(QDBusVariant(
                    properties.value(
                        message.arguments().at(1).toString())))));
        } else {
            connection.send(message.createReply(
                QVariant::fromValue(properties)));
        }
        return true;
    }

    bool accessible(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.member() == QStringLiteral("GetChildren")) {
            connection.send(message.createReply(
                QVariant::fromValue(children(message.path()))));
        } else if (message.member() == QStringLiteral("GetRole")) {
            connection.send(message.createReply(
                QVariant::fromValue(roleValue(message.path()))));
        } else if (message.member() == QStringLiteral("GetRoleName")) {
            connection.send(message.createReply(role(message.path())));
        } else if (message.member() == QStringLiteral("GetInterfaces")) {
            connection.send(message.createReply(interfaces(message.path())));
        } else if (message.member() == QStringLiteral("GetState")) {
            quint32 states = (quint32{1} << 8) | (quint32{1} << 24)
                | (quint32{1} << 25) | (quint32{1} << 30);
            if (message.path() == QStringLiteral("/font_family")) {
                states |= (quint32{1} << 7) | (quint32{1} << 11);
            }
            connection.send(message.createReply(
                QVariant::fromValue(QList<quint32>{states, 0})));
        } else if (message.member() == QStringLiteral("GetAttributes")) {
            QMap<QString, QString> attributes;
            if (message.path() == QStringLiteral("/app")
                || message.path() == QStringLiteral("/app_two")) {
                attributes.insert(
                    QStringLiteral("toolkit"),
                    QStringLiteral("Qt"));
            }
            connection.send(message.createReply(
                QVariant::fromValue(attributes)));
        } else {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownMethod"),
                QStringLiteral("Unknown Accessible method")));
        }
        return true;
    }

    bool component(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.member() == QStringLiteral("GetExtents")) {
            const auto rect =
                QFileInfo::exists(qEnvironmentVariable(
                    "KODOSI_FAKE_ATSPI_OVERFLOW_BOUNDS_FILE"))
                ? Rect{
                      std::numeric_limits<int>::max() - 2,
                      20,
                      300,
                      80}
                : Rect{10, 20, 300, 80};
            connection.send(message.createReply(
                QVariant::fromValue(rect)));
        } else if (message.member() == QStringLiteral("GrabFocus")) {
            connection.send(message.createReply(true));
        } else {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownMethod"),
                QStringLiteral("Unknown Component method")));
        }
        return true;
    }

    bool action(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.path() != QStringLiteral("/settings_button")) {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownObject"),
                QStringLiteral("No action")));
        } else if (message.member() == QStringLiteral("GetActions")) {
            connection.send(message.createReply(
                QVariant::fromValue(Actions{
                    {QStringLiteral("click"),
                     QStringLiteral("Open settings"),
                     QString()},
                })));
        } else if (message.member() == QStringLiteral("DoAction")) {
            m_panelVisible = true;
            connection.send(message.createReply(true));
        } else if (message.member() == QStringLiteral("GetName")) {
            connection.send(message.createReply(QStringLiteral("click")));
        } else {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownMethod"),
                QStringLiteral("Unknown Action method")));
        }
        return true;
    }

    bool text(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.path() != QStringLiteral("/font_family")) {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownObject"),
                QStringLiteral("No text")));
        } else if (message.member()
                   == QStringLiteral("GetCharacterCount")) {
            connection.send(message.createReply(
                static_cast<int>(m_text.size())));
        } else if (message.member() == QStringLiteral("GetText")) {
            const auto start = message.arguments().at(0).toInt();
            const auto end = message.arguments().at(1).toInt();
            const auto boundedStart = std::clamp<qsizetype>(
                start,
                0,
                m_text.size());
            const auto boundedLength = std::clamp<qsizetype>(
                end - start,
                0,
                m_text.size() - boundedStart);
            connection.send(message.createReply(
                m_text.sliced(boundedStart, boundedLength)));
        } else if (message.member() == QStringLiteral("GetCaretOffset")) {
            connection.send(message.createReply(
                static_cast<int>(m_text.size())));
        } else {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownMethod"),
                QStringLiteral("Unknown Text method")));
        }
        return true;
    }

    bool editableText(
        const QDBusMessage& message,
        const QDBusConnection& connection)
    {
        if (message.path() == QStringLiteral("/font_family")
            && message.member() == QStringLiteral("SetTextContents")
            && message.arguments().size() == 1) {
            m_text = message.arguments().first().toString();
            connection.send(message.createReply(true));
        } else {
            connection.send(message.createErrorReply(
                QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownMethod"),
                QStringLiteral("Unknown EditableText method")));
        }
        return true;
    }
};

} // namespace

Q_DECLARE_METATYPE(ObjectReference)
Q_DECLARE_METATYPE(ObjectReferences)
Q_DECLARE_METATYPE(Action)
Q_DECLARE_METATYPE(Actions)
Q_DECLARE_METATYPE(Rect)

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    qDBusRegisterMetaType<ObjectReference>();
    qDBusRegisterMetaType<ObjectReferences>();
    qDBusRegisterMetaType<Action>();
    qDBusRegisterMetaType<Actions>();
    qDBusRegisterMetaType<Rect>();
    qDBusRegisterMetaType<QMap<QString, QString>>();
    qDBusRegisterMetaType<fake_types::IntPair>();
    qDBusRegisterMetaType<fake_types::PortalStream>();
    qDBusRegisterMetaType<fake_types::PortalStreams>();

    if (application.arguments().contains(QStringLiteral("--child"))) {
        static_cast<void>(prctl(PR_SET_PDEATHSIG, SIGTERM));
        auto childBus = QDBusConnection::sessionBus();
        if (!childBus.isConnected()) {
            return 2;
        }
        FakeAtSpi childFake(childBus.baseService());
        if (!childBus.registerVirtualObject(
                QStringLiteral("/"),
                &childFake,
                QDBusConnection::SubPath)) {
            return 3;
        }
        const auto ready =
            QStringLiteral("READY %1\n").arg(childBus.baseService()).toUtf8();
        static_cast<void>(std::fwrite(
            ready.constData(),
            1,
            static_cast<std::size_t>(ready.size()),
            stdout));
        std::fflush(stdout);
        return application.exec();
    }

    QProcess secondApplication;
    secondApplication.setProgram(QCoreApplication::applicationFilePath());
    secondApplication.setArguments({QStringLiteral("--child")});
    secondApplication.start();
    if (!secondApplication.waitForStarted()
        || !secondApplication.waitForReadyRead(3000)) {
        return 4;
    }
    const auto ready = secondApplication.readLine().trimmed().split(' ');
    if (ready.size() != 2 || ready.first() != QByteArrayLiteral("READY")) {
        return 5;
    }
    const auto secondBusName = QString::fromUtf8(ready.at(1));

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()
        || !bus.registerService(QStringLiteral("org.a11y.Bus"))
        || !bus.registerService(
            QStringLiteral("org.a11y.atspi.Registry"))
        || !bus.registerService(
            QStringLiteral("org.freedesktop.portal.Desktop"))) {
        return 2;
    }

    FakeAtSpi fake(bus.baseService(), secondBusName);
    if (!bus.registerVirtualObject(
            QStringLiteral("/"),
            &fake,
            QDBusConnection::SubPath)) {
        return 3;
    }

    std::fputs("READY\n", stdout);
    std::fflush(stdout);
    return application.exec();
}
