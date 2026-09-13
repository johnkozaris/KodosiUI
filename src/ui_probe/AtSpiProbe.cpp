#include "ui_probe/AtSpiProbe.hpp"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QMetaType>
#include <QRegularExpression>
#include <QThread>
#include <QVariantMap>
#include <QXmlStreamReader>

#include <algorithm>
#include <limits>
#include <utility>

namespace kodosi::ui_probe {
namespace {

constexpr auto AccessibleInterface = "org.a11y.atspi.Accessible";
constexpr auto ActionInterface = "org.a11y.atspi.Action";
constexpr auto ComponentInterface = "org.a11y.atspi.Component";
constexpr auto EditableTextInterface = "org.a11y.atspi.EditableText";
constexpr auto TextInterface = "org.a11y.atspi.Text";
constexpr auto ValueInterface = "org.a11y.atspi.Value";
constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto IntrospectableInterface =
    "org.freedesktop.DBus.Introspectable";
constexpr auto DbusService = "org.freedesktop.DBus";
constexpr auto DbusPath = "/org/freedesktop/DBus";
constexpr auto DbusInterface = "org.freedesktop.DBus";
constexpr auto RegistryService = "org.a11y.atspi.Registry";
constexpr auto RegistryRoot = "/org/a11y/atspi/accessible/root";
constexpr auto AccessibilityBusService = "org.a11y.Bus";
constexpr auto AccessibilityBusPath = "/org/a11y/bus";
constexpr auto AccessibilityBusInterface = "org.a11y.Bus";

QString boundedString(const QString& value, const qsizetype maximum = 8192)
{
    if (value.size() <= maximum) {
        return value;
    }
    return value.left(maximum);
}

QString boundedUtf8String(const QString& value, const qsizetype maximumBytes)
{
    if (value.toUtf8().size() <= maximumBytes) {
        return value;
    }
    qsizetype low = 0;
    qsizetype high = value.size();
    while (low < high) {
        const auto middle = low + (high - low + 1) / 2;
        if (value.first(middle).toUtf8().size() <= maximumBytes) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    if (low > 0 && low < value.size()
        && value.at(low - 1).isHighSurrogate()
        && value.at(low).isLowSurrogate()) {
        --low;
    }
    return value.first(low);
}

[[noreturn]] void throwRemote(
    const QString& operation,
    const QDBusMessage& reply)
{
    const auto name = reply.errorName();
    const auto timedOut =
        name == QStringLiteral("org.freedesktop.DBus.Error.NoReply")
        || name == QStringLiteral("org.freedesktop.DBus.Error.Timeout")
        || name.endsWith(QStringLiteral(".TimedOut"));
    const auto invalidated =
        name == QStringLiteral("org.freedesktop.DBus.Error.UnknownObject")
        || name == QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner")
        || name == QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown")
        || name == QStringLiteral("org.a11y.atspi.Error.ObjectNotFound");
    const auto unsupported =
        name == QStringLiteral("org.freedesktop.DBus.Error.UnknownInterface")
        || name == QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod")
        || name == QStringLiteral("org.freedesktop.DBus.Error.UnknownProperty");
    throw ProbeError(
        timedOut
            ? ExitCode::Timeout
            : invalidated
            ? ExitCode::NotFound
            : unsupported ? ExitCode::Unsupported : ExitCode::RemoteError,
        timedOut
            ? QStringLiteral("dbus-timeout")
            : invalidated
            ? QStringLiteral("object-disappeared")
            : unsupported ? QStringLiteral("dbus-member-unsupported")
                          : QStringLiteral("dbus-error"),
        QStringLiteral("%1 failed: %2: %3")
            .arg(operation, name, reply.errorMessage()));
}

QDBusMessage call(
    const QDBusConnection& connection,
    const QString& service,
    const QString& path,
    const QString& interface,
    const QString& method,
    const QVariantList& arguments,
    const QString& expectedSignature,
    const int timeoutMs)
{
    auto message =
        QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    const auto reply = connection.call(message, QDBus::Block, timeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        throwRemote(
            QStringLiteral("%1.%2").arg(interface, method),
            reply);
    }
    if (reply.type() != QDBusMessage::ReplyMessage) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-reply"),
            QStringLiteral("%1.%2 returned a non-reply message")
                .arg(interface, method));
    }
    if (reply.signature() != expectedSignature) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-signature"),
            QStringLiteral("%1.%2 returned signature '%3', expected '%4'")
                .arg(interface, method, reply.signature(), expectedSignature));
    }
    return reply;
}

QVariant unwrapVariant(const QVariant& value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        return qvariant_cast<QDBusVariant>(value).variant();
    }
    return value;
}

QVariant getProperty(
    const QDBusConnection& connection,
    const AtSpiProbe::Reference& reference,
    const QString& interface,
    const QString& property,
    const int timeoutMs)
{
    const auto reply = call(
        connection,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(PropertiesInterface),
        QStringLiteral("Get"),
        {interface, property},
        QStringLiteral("v"),
        timeoutMs);
    const auto argument = reply.arguments().at(0);
    if (argument.metaType() != QMetaType::fromType<QDBusVariant>()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-type"),
            QStringLiteral("Property '%1' was not wrapped as a D-Bus variant")
                .arg(property));
    }
    return qvariant_cast<QDBusVariant>(argument).variant();
}

QString stringProperty(
    const QDBusConnection& connection,
    const AtSpiProbe::Reference& reference,
    const QString& interface,
    const QString& property,
    const int timeoutMs)
{
    const auto value =
        getProperty(connection, reference, interface, property, timeoutMs);
    if (value.metaType() != QMetaType::fromType<QString>()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-type"),
            QStringLiteral("Property '%1' is not a string").arg(property));
    }
    return boundedString(value.toString());
}

int intProperty(
    const QDBusConnection& connection,
    const AtSpiProbe::Reference& reference,
    const QString& interface,
    const QString& property,
    const int timeoutMs)
{
    const auto value =
        getProperty(connection, reference, interface, property, timeoutMs);
    if (value.metaType() != QMetaType::fromType<int>()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-type"),
            QStringLiteral("Property '%1' is not an int32").arg(property));
    }
    return value.toInt();
}

double doubleProperty(
    const QDBusConnection& connection,
    const AtSpiProbe::Reference& reference,
    const QString& interface,
    const QString& property,
    const int timeoutMs)
{
    const auto value =
        getProperty(connection, reference, interface, property, timeoutMs);
    if (value.metaType() != QMetaType::fromType<double>()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-type"),
            QStringLiteral("Property '%1' is not a double").arg(property));
    }
    return value.toDouble();
}

QStringList stringListReply(const QDBusMessage& reply)
{
    const auto value = reply.arguments().at(0);
    if (value.metaType() != QMetaType::fromType<QStringList>()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-type"),
            QStringLiteral("Expected a D-Bus string array"));
    }
    auto list = value.toStringList();
    if (list.size() > 128) {
        throw ProbeError(
            ExitCode::LimitExceeded,
            QStringLiteral("interface-limit"),
            QStringLiteral("AT-SPI object exposed too many interfaces"));
    }
    for (auto& item : list) {
        item = boundedString(item, 256);
    }
    return list;
}

QJsonArray stringArray(const QStringList& strings)
{
    QJsonArray result;
    for (const auto& string : strings) {
        result.append(string);
    }
    return result;
}

QStringList jsonStringList(const QJsonArray& values)
{
    QStringList result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.append(value.toString());
    }
    return result;
}

QList<AtSpiProbe::Reference> referenceArray(const QDBusMessage& reply)
{
    QList<AtSpiProbe::Reference> result;
    const auto argument =
        qvariant_cast<QDBusArgument>(reply.arguments().at(0));
    argument.beginArray();
    while (!argument.atEnd()) {
        QString busName;
        QDBusObjectPath objectPath;
        argument.beginStructure();
        argument >> busName >> objectPath;
        argument.endStructure();
        if (!AtSpiProbe::decodeHandle(
                AtSpiProbe::encodeHandle(busName, objectPath.path()),
                nullptr,
                nullptr)) {
            throw ProbeError(
                ExitCode::RemoteError,
                QStringLiteral("invalid-atspi-reference"),
                QStringLiteral("AT-SPI returned an invalid object reference"));
        }
        result.append({busName, objectPath.path()});
        if (result.size() > AtSpiProbe::MaximumNodes) {
            throw ProbeError(
                ExitCode::LimitExceeded,
                QStringLiteral("node-limit"),
                QStringLiteral("AT-SPI child array exceeds the node limit"));
        }
    }
    argument.endArray();
    return result;
}

QJsonObject boundsReply(const QDBusMessage& reply)
{
    const auto argument =
        qvariant_cast<QDBusArgument>(reply.arguments().at(0));
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    argument.beginStructure();
    argument >> x >> y >> width >> height;
    argument.endStructure();
    return {
        {QStringLiteral("x"), x},
        {QStringLiteral("y"), y},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("coordinateType"), QStringLiteral("screen")},
    };
}

QJsonObject attributesReply(const QDBusMessage& reply)
{
    const auto value = reply.arguments().at(0);
    QVariantMap map;
    if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
        map = value.toMap();
    } else if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const auto argument = qvariant_cast<QDBusArgument>(value);
        argument.beginMap();
        while (!argument.atEnd()) {
            QString key;
            QString entry;
            argument.beginMapEntry();
            argument >> key >> entry;
            argument.endMapEntry();
            map.insert(key, entry);
            if (map.size() > 64) {
                throw ProbeError(
                    ExitCode::LimitExceeded,
                    QStringLiteral("attribute-limit"),
                    QStringLiteral("AT-SPI object exposed too many attributes"));
            }
        }
        argument.endMap();
    } else {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-type"),
            QStringLiteral("Accessible.GetAttributes did not return a string map"));
    }
    if (map.size() > 64) {
        throw ProbeError(
            ExitCode::LimitExceeded,
            QStringLiteral("attribute-limit"),
            QStringLiteral("AT-SPI object exposed too many attributes"));
    }
    QJsonObject result;
    for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator) {
        const auto value = unwrapVariant(iterator.value());
        if (value.metaType() != QMetaType::fromType<QString>()) {
            throw ProbeError(
                ExitCode::RemoteError,
                QStringLiteral("invalid-dbus-type"),
                QStringLiteral("Accessible attribute '%1' is not a string")
                    .arg(iterator.key()));
        }
        result.insert(
            boundedString(iterator.key(), 128),
            boundedString(value.toString(), 2048));
    }
    return result;
}

bool interfaceHasProperty(
    const QDBusConnection& connection,
    const AtSpiProbe::Reference& reference,
    const QString& interface,
    const QString& property,
    const int timeoutMs)
{
    const auto reply = call(
        connection,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(IntrospectableInterface),
        QStringLiteral("Introspect"),
        {},
        QStringLiteral("s"),
        timeoutMs);
    const auto xml = reply.arguments().at(0).toString();
    if (xml.toUtf8().size() > AtSpiProbe::MaximumTraversalBytes) {
        throw ProbeError(
            ExitCode::LimitExceeded,
            QStringLiteral("introspection-limit"),
            QStringLiteral("D-Bus introspection XML exceeds the safety limit"));
    }
    QXmlStreamReader reader(xml);
    QString currentInterface;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()
            && reader.name() == QLatin1StringView("interface")) {
            currentInterface =
                reader.attributes().value(QLatin1StringView("name")).toString();
        } else if (
            reader.isEndElement()
            && reader.name() == QLatin1StringView("interface")) {
            currentInterface.clear();
        } else if (
            reader.isStartElement()
            && reader.name() == QLatin1StringView("property")
            && currentInterface == interface
            && reader.attributes().value(QLatin1StringView("name"))
                == property) {
            return true;
        }
    }
    if (reader.hasError()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-introspection-xml"),
            QStringLiteral("D-Bus introspection returned malformed XML"));
    }
    return false;
}

QList<quint32> stateWords(const QDBusMessage& reply)
{
    QList<quint32> result;
    const auto argument =
        qvariant_cast<QDBusArgument>(reply.arguments().at(0));
    argument.beginArray();
    while (!argument.atEnd()) {
        quint32 word = 0;
        argument >> word;
        result.append(word);
        if (result.size() > 2) {
            throw ProbeError(
                ExitCode::RemoteError,
                QStringLiteral("invalid-state-set"),
                QStringLiteral("AT-SPI state set contains too many words"));
        }
    }
    argument.endArray();
    return result;
}

QJsonArray actionsReply(const QDBusMessage& reply)
{
    QJsonArray result;
    const auto argument =
        qvariant_cast<QDBusArgument>(reply.arguments().at(0));
    argument.beginArray();
    while (!argument.atEnd()) {
        QString name;
        QString description;
        QString keyBinding;
        argument.beginStructure();
        argument >> name >> description >> keyBinding;
        argument.endStructure();
        result.append(QJsonObject{
            {QStringLiteral("index"), result.size()},
            {QStringLiteral("name"), boundedString(name, 256)},
            {QStringLiteral("description"), boundedString(description)},
            {QStringLiteral("keyBinding"), boundedString(keyBinding, 512)},
        });
        if (result.size() > 128) {
            throw ProbeError(
                ExitCode::LimitExceeded,
                QStringLiteral("action-limit"),
                QStringLiteral("AT-SPI object exposed too many actions"));
        }
    }
    argument.endArray();
    return result;
}

QString normalized(const QString& value)
{
    return value.trimmed().toCaseFolded();
}

QString standardRoleName(const quint32 role)
{
    static const QStringList names =
        QStringLiteral(
            "invalid|accelerator label|alert|animation|arrow|calendar|canvas|"
            "check box|check menu item|color chooser|column header|combo box|"
            "date editor|desktop icon|desktop frame|dial|dialog|directory pane|"
            "drawing area|file chooser|filler|focus traversable|font chooser|"
            "frame|glass pane|html container|icon|image|internal frame|label|"
            "layered pane|list|list item|menu|menu bar|menu item|option pane|"
            "page tab|page tab list|panel|password text|popup menu|progress bar|"
            "button|radio button|radio menu item|root pane|row header|scroll bar|"
            "scroll pane|separator|slider|spin button|split pane|status bar|table|"
            "table cell|table column header|table row header|tearoff menu item|"
            "terminal|text|toggle button|tool bar|tool tip|tree|tree table|"
            "unknown|viewport|window|extended|header|footer|paragraph|ruler|"
            "application|autocomplete|editbar|embedded|entry|chart|caption|"
            "document frame|heading|page|section|redundant object|form|link|"
            "input method window|table row|tree item|document spreadsheet|"
            "document presentation|document text|document web|document email|"
            "comment|list box|grouping|image map|notification|info bar|level bar|"
            "title bar|block quote|audio|video|definition|article|landmark|log|"
            "marquee|math|rating|timer|static|math fraction|math root|subscript|"
            "superscript|description list|description term|description value|"
            "footnote|content deletion|content insertion|mark|suggestion|"
            "push button menu|switch")
            .split(QLatin1Char('|'));
    return role < static_cast<quint32>(names.size())
        ? names.at(static_cast<qsizetype>(role))
        : QString {};
}

}

ProbeError::ProbeError(
    const ExitCode code,
    QString kind,
    QString message)
    : m_code(code)
    , m_kind(std::move(kind))
    , m_message(std::move(message))
    , m_utf8(m_message.toUtf8())
{
}

ExitCode ProbeError::code() const noexcept
{
    return m_code;
}

const QString& ProbeError::kind() const noexcept
{
    return m_kind;
}

const QString& ProbeError::message() const noexcept
{
    return m_message;
}

const char* ProbeError::what() const noexcept
{
    return m_utf8.constData();
}

bool Selector::empty() const
{
    return id.isEmpty() && name.isEmpty() && role.isEmpty()
        && contains.isEmpty();
}

AtSpiProbe::TimeoutBudget::TimeoutBudget(
    const int defaultTimeoutMs,
    const QDeadlineTimer* deadline)
    : m_defaultTimeoutMs(defaultTimeoutMs)
    , m_deadline(deadline)
{
}

void AtSpiProbe::TimeoutBudget::setDeadline(
    const QDeadlineTimer* deadline)
{
    m_deadline = deadline;
}

AtSpiProbe::TimeoutBudget::operator int() const
{
    if (m_deadline == nullptr || m_deadline->isForever()) {
        return m_defaultTimeoutMs;
    }
    const auto remaining = m_deadline->remainingTime();
    if (remaining <= 0) {
        throw ProbeError(
            ExitCode::Timeout,
            QStringLiteral("operation-timeout"),
            QStringLiteral("The operation deadline expired"));
    }
    return std::min(
        m_defaultTimeoutMs,
        static_cast<int>(remaining));
}

AtSpiProbe::AtSpiProbe(
    const int callTimeoutMs,
    const int operationTimeoutMs)
    : m_sessionBus(QDBusConnection::sessionBus())
    , m_atspiBus(QDBusConnection::sessionBus())
    , m_operationDeadline(
          operationTimeoutMs >= 0
              ? std::optional<QDeadlineTimer>(
                    std::in_place,
                    operationTimeoutMs,
                    Qt::PreciseTimer)
              : std::nullopt)
    , m_callTimeoutMs(
          std::clamp(callTimeoutMs, 1, 30000),
          m_operationDeadline.has_value()
              ? &m_operationDeadline.value()
              : nullptr)
{
    if (!m_sessionBus.isConnected()) {
        throw ProbeError(
            ExitCode::Environment,
            QStringLiteral("session-bus-unavailable"),
            QStringLiteral("The D-Bus session bus is unavailable"));
    }

    const auto addressReply = call(
        m_sessionBus,
        QString::fromLatin1(AccessibilityBusService),
        QString::fromLatin1(AccessibilityBusPath),
        QString::fromLatin1(AccessibilityBusInterface),
        QStringLiteral("GetAddress"),
        {},
        QStringLiteral("s"),
        m_callTimeoutMs);
    const auto addressValue = addressReply.arguments().at(0);
    if (addressValue.metaType() != QMetaType::fromType<QString>()
        || addressValue.toString().isEmpty()
        || addressValue.toString().size() > 4096) {
        throw ProbeError(
            ExitCode::Environment,
            QStringLiteral("invalid-accessibility-address"),
            QStringLiteral("org.a11y.Bus.GetAddress returned an invalid address"));
    }

    m_atspiConnectionName =
        QStringLiteral("kodosi-ui-probe-atspi-%1")
            .arg(QCoreApplication::applicationPid());
    m_atspiBus = QDBusConnection::connectToBus(
        addressValue.toString(),
        m_atspiConnectionName);
    if (!m_atspiBus.isConnected()) {
        throw ProbeError(
            ExitCode::Environment,
            QStringLiteral("accessibility-bus-unavailable"),
            QStringLiteral("Could not connect to the AT-SPI accessibility bus"));
    }

}

AtSpiProbe::~AtSpiProbe()
{
    if (!m_atspiConnectionName.isEmpty()) {
        QDBusConnection::disconnectFromBus(m_atspiConnectionName);
    }
}

bool AtSpiProbe::validBusName(const QString& busName)
{
    static const QRegularExpression uniqueName(
        QStringLiteral("^:[A-Za-z0-9_-]+(?:\\.[A-Za-z0-9_-]+)+$"));
    return busName.size() <= 255 && uniqueName.match(busName).hasMatch();
}

bool AtSpiProbe::validObjectPath(const QString& objectPath)
{
    if (objectPath.size() > 1024 || objectPath.isEmpty()) {
        return false;
    }
    static const QRegularExpression pathPattern(
        QStringLiteral("^(?:/|(?:/[A-Za-z0-9_]+)+)$"));
    return pathPattern.match(objectPath).hasMatch();
}

QString AtSpiProbe::encodeHandle(
    const QString& busName,
    const QString& objectPath)
{
    if (!validBusName(busName) || !validObjectPath(objectPath)) {
        return {};
    }
    auto payload = busName.toUtf8();
    payload.append('\0');
    payload.append(objectPath.toUtf8());
    return QStringLiteral("atspi1_")
        + QString::fromLatin1(
            payload.toBase64(
                QByteArray::Base64UrlEncoding
                | QByteArray::OmitTrailingEquals));
}

bool AtSpiProbe::decodeHandle(
    const QString& handle,
    QString* busName,
    QString* objectPath)
{
    constexpr auto prefix = "atspi1_";
    if (!handle.startsWith(QString::fromLatin1(prefix))
        || handle.size() > 2048) {
        return false;
    }
    const auto encoded = handle.mid(
        static_cast<qsizetype>(std::char_traits<char>::length(prefix)));
    const auto payload = QByteArray::fromBase64(
        encoded.toLatin1(),
        QByteArray::Base64UrlEncoding
            | QByteArray::AbortOnBase64DecodingErrors);
    const auto separator = payload.indexOf('\0');
    if (separator <= 0 || separator != payload.lastIndexOf('\0')) {
        return false;
    }
    const auto decodedBus = QString::fromUtf8(payload.first(separator));
    const auto decodedPath = QString::fromUtf8(payload.sliced(separator + 1));
    if (decodedBus.toUtf8() != payload.first(separator)
        || decodedPath.toUtf8() != payload.sliced(separator + 1)
        || !validBusName(decodedBus)
        || !validObjectPath(decodedPath)
        || encodeHandle(decodedBus, decodedPath) != handle) {
        return false;
    }
    if (busName != nullptr) {
        *busName = decodedBus;
    }
    if (objectPath != nullptr) {
        *objectPath = decodedPath;
    }
    return true;
}

AtSpiProbe::Reference AtSpiProbe::referenceFromHandle(const QString& handle)
{
    Reference reference;
    if (!decodeHandle(handle, &reference.busName, &reference.objectPath)) {
        throw ProbeError(
            ExitCode::InvalidHandle,
            QStringLiteral("invalid-handle"),
            QStringLiteral("The element handle is malformed or non-canonical"));
    }
    return reference;
}

QJsonObject AtSpiProbe::referenceJson(const Reference& reference)
{
    return {
        {QStringLiteral("handle"),
         encodeHandle(reference.busName, reference.objectPath)},
        {QStringLiteral("busName"), reference.busName},
        {QStringLiteral("objectPath"), reference.objectPath},
    };
}

QList<AtSpiProbe::Reference> AtSpiProbe::children(
    const Reference& reference)
{
    return referenceArray(call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("GetChildren"),
        {},
        QStringLiteral("a(so)"),
        m_callTimeoutMs));
}

AtSpiProbe::Snapshot AtSpiProbe::snapshot(
    const Reference& reference,
    const bool includeBounds)
{
    Snapshot result;
    result.reference = reference;
    result.name = stringProperty(
        m_atspiBus,
        reference,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("Name"),
        m_callTimeoutMs);
    result.description = stringProperty(
        m_atspiBus,
        reference,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("Description"),
        m_callTimeoutMs);
    result.accessibleId = stringProperty(
        m_atspiBus,
        reference,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("AccessibleId"),
        m_callTimeoutMs);
    result.helpText = stringProperty(
        m_atspiBus,
        reference,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("HelpText"),
        m_callTimeoutMs);
    result.childCount = intProperty(
        m_atspiBus,
        reference,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("ChildCount"),
        m_callTimeoutMs);
    if (result.childCount < 0 || result.childCount > MaximumNodes) {
        throw ProbeError(
            ExitCode::LimitExceeded,
            QStringLiteral("child-count-limit"),
            QStringLiteral("AT-SPI object reported an invalid child count"));
    }

    const auto roleReply = call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("GetRole"),
        {},
        QStringLiteral("u"),
        m_callTimeoutMs);
    const auto role = roleReply.arguments().at(0).toUInt();
    result.role = standardRoleName(role);
    if (result.role.isEmpty()) {
        try {
            const auto roleNameReply = call(
                m_atspiBus,
                reference.busName,
                reference.objectPath,
                QString::fromLatin1(AccessibleInterface),
                QStringLiteral("GetRoleName"),
                {},
                QStringLiteral("s"),
                m_callTimeoutMs);
            result.role =
                boundedString(roleNameReply.arguments().at(0).toString(), 256);
        } catch (const ProbeError&) {
            result.role = QStringLiteral("role-%1").arg(role);
        }
    }

    result.interfaces = stringListReply(call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("GetInterfaces"),
        {},
        QStringLiteral("as"),
        m_callTimeoutMs));
    result.states = decodeStates(
        stateWords(call(
            m_atspiBus,
            reference.busName,
            reference.objectPath,
            QString::fromLatin1(AccessibleInterface),
            QStringLiteral("GetState"),
            {},
            QStringLiteral("au"),
            m_callTimeoutMs)));
    result.attributes = attributesReply(call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(AccessibleInterface),
        QStringLiteral("GetAttributes"),
        {},
        QStringLiteral("a{ss}"),
        m_callTimeoutMs));

    if (includeBounds
        && result.interfaces.contains(QString::fromLatin1(ComponentInterface))) {
        result.bounds = boundsReply(call(
            m_atspiBus,
            reference.busName,
            reference.objectPath,
            QString::fromLatin1(ComponentInterface),
            QStringLiteral("GetExtents"),
            {QVariant::fromValue<quint32>(0)},
            QStringLiteral("(iiii)"),
            m_callTimeoutMs));
        result.hasBounds = true;
    }
    return result;
}

QJsonObject AtSpiProbe::snapshotJson(const Snapshot& snapshot)
{
    auto result = referenceJson(snapshot.reference);
    result.insert(QStringLiteral("id"), snapshot.accessibleId);
    result.insert(QStringLiteral("name"), snapshot.name);
    result.insert(QStringLiteral("description"), snapshot.description);
    result.insert(QStringLiteral("helpText"), snapshot.helpText);
    result.insert(QStringLiteral("role"), snapshot.role);
    result.insert(
        QStringLiteral("interfaces"),
        stringArray(snapshot.interfaces));
    result.insert(QStringLiteral("states"), stringArray(snapshot.states));
    result.insert(QStringLiteral("attributes"), snapshot.attributes);
    result.insert(QStringLiteral("childCount"), snapshot.childCount);
    if (snapshot.hasBounds) {
        result.insert(QStringLiteral("bounds"), snapshot.bounds);
    }
    return result;
}

QStringList AtSpiProbe::decodeStates(const QList<quint32>& words)
{
    static const QStringList names = {
        QStringLiteral("invalid"),
        QStringLiteral("active"),
        QStringLiteral("armed"),
        QStringLiteral("busy"),
        QStringLiteral("checked"),
        QStringLiteral("collapsed"),
        QStringLiteral("defunct"),
        QStringLiteral("editable"),
        QStringLiteral("enabled"),
        QStringLiteral("expandable"),
        QStringLiteral("expanded"),
        QStringLiteral("focusable"),
        QStringLiteral("focused"),
        QStringLiteral("has-tooltip"),
        QStringLiteral("horizontal"),
        QStringLiteral("iconified"),
        QStringLiteral("modal"),
        QStringLiteral("multi-line"),
        QStringLiteral("multiselectable"),
        QStringLiteral("opaque"),
        QStringLiteral("pressed"),
        QStringLiteral("resizable"),
        QStringLiteral("selectable"),
        QStringLiteral("selected"),
        QStringLiteral("sensitive"),
        QStringLiteral("showing"),
        QStringLiteral("single-line"),
        QStringLiteral("stale"),
        QStringLiteral("transient"),
        QStringLiteral("vertical"),
        QStringLiteral("visible"),
        QStringLiteral("manages-descendants"),
        QStringLiteral("indeterminate"),
        QStringLiteral("required"),
        QStringLiteral("truncated"),
        QStringLiteral("animated"),
        QStringLiteral("invalid-entry"),
        QStringLiteral("supports-autocompletion"),
        QStringLiteral("selectable-text"),
        QStringLiteral("is-default"),
        QStringLiteral("visited"),
        QStringLiteral("checkable"),
        QStringLiteral("has-popup"),
        QStringLiteral("read-only"),
    };
    QStringList result;
    for (qsizetype state = 0; state < names.size(); ++state) {
        const auto word = state / 32;
        const auto bit = state % 32;
        if (word < words.size()
            && (words.at(word) & (quint32{1} << bit)) != 0U) {
            result.append(names.at(state));
        }
    }
    return result;
}

QList<AtSpiProbe::Reference> AtSpiProbe::applications()
{
    return children({
        QString::fromLatin1(RegistryService),
        QString::fromLatin1(RegistryRoot),
    });
}

qint64 AtSpiProbe::applicationProcessId(const Reference& reference)
{
    const auto reply = call(
        m_atspiBus,
        QString::fromLatin1(DbusService),
        QString::fromLatin1(DbusPath),
        QString::fromLatin1(DbusInterface),
        QStringLiteral("GetConnectionUnixProcessID"),
        {reference.busName},
        QStringLiteral("u"),
        m_callTimeoutMs);
    const auto processId = reply.arguments().at(0).toUInt();
    if (processId == 0) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-application-pid"),
            QStringLiteral("AT-SPI application has no valid process ID"));
    }
    return static_cast<qint64>(processId);
}

QJsonObject AtSpiProbe::applicationJson(
    const Reference& reference,
    const bool includeBounds)
{
    auto result = snapshotJson(snapshot(reference, includeBounds));
    result.insert(
        QStringLiteral("processId"),
        applicationProcessId(reference));
    return result;
}

AtSpiProbe::Reference AtSpiProbe::resolveApplication(
    const QString& app,
    const std::optional<qint64> processId)
{
    if (app.isEmpty() && !processId.has_value()) {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("missing-app"),
            QStringLiteral("--app or --pid is required"));
    }

    QString decodedBus;
    QString decodedPath;
    const auto isHandle =
        !app.isEmpty() && decodeHandle(app, &decodedBus, &decodedPath);

    QList<Reference> exact;
    QList<Reference> folded;
    for (const auto& reference : applications()) {
        try {
            if (processId.has_value()
                && applicationProcessId(reference) != *processId) {
                continue;
            }
            if (isHandle) {
                if (reference.busName == decodedBus
                    && reference.objectPath == decodedPath) {
                    exact.append(reference);
                }
                continue;
            }
            if (app.isEmpty()) {
                exact.append(reference);
                continue;
            }
            const auto candidate = snapshot(reference, false);
            if (reference.busName == app
                || candidate.accessibleId == app
                || candidate.name == app) {
                exact.append(reference);
            } else if (normalized(candidate.name) == normalized(app)
                       || normalized(candidate.accessibleId)
                           == normalized(app)) {
                folded.append(reference);
            }
        } catch (const ProbeError& error) {
            if (error.code() != ExitCode::NotFound) {
                throw;
            }
        }
    }
    const auto matches = exact.isEmpty() ? folded : exact;
    if (matches.isEmpty()) {
        throw ProbeError(
            ExitCode::NotFound,
            QStringLiteral("app-not-found"),
            processId.has_value()
                ? QStringLiteral(
                      "No accessible application matched PID %1%2")
                      .arg(*processId)
                      .arg(
                          app.isEmpty()
                              ? QString {}
                              : QStringLiteral(" and '%1'").arg(app))
                : QStringLiteral(
                      "No accessible application matched '%1'").arg(app));
    }
    if (matches.size() != 1) {
        throw ProbeError(
            ExitCode::Ambiguous,
            QStringLiteral("ambiguous-app"),
            QStringLiteral("%1 accessible applications matched%2%3")
                .arg(matches.size())
                .arg(
                    processId.has_value()
                        ? QStringLiteral(" PID %1").arg(*processId)
                        : QString {})
                .arg(
                    app.isEmpty()
                        ? QString {}
                        : QStringLiteral(" '%1'").arg(app)));
    }
    return matches.first();
}

QJsonObject AtSpiProbe::apps()
{
    QJsonArray appResults;
    int stale = 0;
    int nodeCount = 0;
    qsizetype estimatedBytes = 0;
    bool truncated = false;
    for (const auto& reference : applications()) {
        if (nodeCount >= MaximumNodes
            || estimatedBytes >= MaximumTraversalBytes) {
            truncated = true;
            break;
        }
        try {
            auto app = applicationJson(reference, true);
            ++nodeCount;
            QJsonArray windows;
            for (const auto& child : children(reference)) {
                if (nodeCount >= MaximumNodes
                    || estimatedBytes >= MaximumTraversalBytes) {
                    truncated = true;
                    break;
                }
                try {
                    const auto window = snapshot(child, true);
                    ++nodeCount;
                    if (window.role == QStringLiteral("frame")
                        || window.role == QStringLiteral("window")
                        || window.role == QStringLiteral("dialog")
                        || window.role == QStringLiteral("application")) {
                        const auto windowJson = snapshotJson(window);
                        estimatedBytes +=
                            QJsonDocument(windowJson)
                                .toJson(QJsonDocument::Compact)
                                .size();
                        if (estimatedBytes > MaximumTraversalBytes) {
                            truncated = true;
                            break;
                        }
                        windows.append(windowJson);
                    }
                } catch (const ProbeError& error) {
                    if (error.code() != ExitCode::NotFound) {
                        throw;
                    }
                    ++stale;
                }
            }
            app.insert(QStringLiteral("windows"), windows);
            estimatedBytes +=
                QJsonDocument(app).toJson(QJsonDocument::Compact).size();
            if (estimatedBytes > MaximumTraversalBytes) {
                truncated = true;
                break;
            }
            appResults.append(app);
        } catch (const ProbeError& error) {
            if (error.code() != ExitCode::NotFound) {
                throw;
            }
            ++stale;
        }
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("apps")},
        {QStringLiteral("applications"), appResults},
        {QStringLiteral("count"), appResults.size()},
        {QStringLiteral("nodeCount"), nodeCount},
        {QStringLiteral("truncated"), truncated},
        {QStringLiteral("staleObjectsSkipped"), stale},
    };
}

AtSpiProbe::TextSnapshot AtSpiProbe::nodeText(
    const Reference& reference,
    const QStringList& interfaces)
{
    if (!interfaces.contains(QString::fromLatin1(TextInterface))) {
        return {};
    }
    const auto count = intProperty(
        m_atspiBus,
        reference,
        QString::fromLatin1(TextInterface),
        QStringLiteral("CharacterCount"),
        m_callTimeoutMs);
    if (count < 0) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-text-length"),
            QStringLiteral("AT-SPI Text reported a negative character count"));
    }
    const auto boundedCount = std::min(count, MaximumTextBytes);
    const auto textReply = call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(TextInterface),
        QStringLiteral("GetText"),
        {0, boundedCount},
        QStringLiteral("s"),
        m_callTimeoutMs);
    const auto raw = textReply.arguments().at(0).toString();
    const auto content = boundedUtf8String(raw, MaximumTextBytes);
    return {
        content,
        count,
        count > boundedCount || content != raw,
    };
}

QJsonObject AtSpiProbe::walkNode(
    const Reference& reference,
    const int remainingDepth,
    const bool includeText,
    WalkResult* result)
{
    if (result->count >= MaximumNodes) {
        result->truncated = true;
        return {};
    }
    auto nodeSnapshot = snapshot(reference, true);
    auto node = snapshotJson(nodeSnapshot);
    ++result->count;
    if (includeText) {
        const auto text = nodeText(reference, nodeSnapshot.interfaces);
        if (!text.content.isEmpty()) {
            node.insert(QStringLiteral("text"), text.content);
            node.insert(QStringLiteral("textTruncated"), text.truncated);
        }
    }
    const auto nodeBytes =
        QJsonDocument(node).toJson(QJsonDocument::Compact).size();
    if (result->estimatedBytes + nodeBytes > MaximumTraversalBytes) {
        result->truncated = true;
        return {};
    }
    result->estimatedBytes += nodeBytes;
    result->flat.append(node);

    QJsonArray descendants;
    if (remainingDepth > 0 && nodeSnapshot.childCount > 0) {
        for (const auto& child : children(reference)) {
            if (result->count >= MaximumNodes) {
                result->truncated = true;
                break;
            }
            try {
                const auto childNode = walkNode(
                    child,
                    remainingDepth - 1,
                    includeText,
                    result);
                if (!childNode.isEmpty()) {
                    descendants.append(childNode);
                }
            } catch (const ProbeError& error) {
                if (error.code() != ExitCode::NotFound) {
                    throw;
                }
            }
        }
    } else if (remainingDepth == 0 && nodeSnapshot.childCount > 0) {
        result->truncated = true;
    }
    if (!descendants.isEmpty()) {
        node.insert(QStringLiteral("children"), descendants);
    }
    return node;
}

AtSpiProbe::WalkResult AtSpiProbe::walk(
    const Reference& root,
    const int depth,
    const bool includeText)
{
    WalkResult result;
    result.root = walkNode(root, depth, includeText, &result);
    return result;
}

QJsonObject AtSpiProbe::tree(
    const QString& app,
    const std::optional<qint64> processId,
    const int depth)
{
    if (depth < 0 || depth > MaximumDepth) {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("invalid-depth"),
            QStringLiteral("--depth must be between 0 and %1")
                .arg(MaximumDepth));
    }
    const auto root = resolveApplication(app, processId);
    const auto result = walk(root, depth, false);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("tree")},
        {QStringLiteral("app"), applicationJson(root, true)},
        {QStringLiteral("tree"), result.root},
        {QStringLiteral("nodeCount"), result.count},
        {QStringLiteral("truncated"), result.truncated},
        {QStringLiteral("depth"), depth},
    };
}

bool AtSpiProbe::matchesStructural(
    const QJsonObject& node,
    const Selector& selector) const
{
    if (!selector.id.isEmpty()
        && node.value(QStringLiteral("id")).toString() != selector.id) {
        return false;
    }
    if (!selector.name.isEmpty()
        && node.value(QStringLiteral("name")).toString() != selector.name) {
        return false;
    }
    if (!selector.role.isEmpty()
        && normalized(node.value(QStringLiteral("role")).toString())
            != normalized(selector.role)) {
        return false;
    }
    return true;
}

AtSpiProbe::MatchResult AtSpiProbe::matchingNodes(
    const Reference& app,
    const Selector& selector)
{
    const auto result = walk(app, MaximumDepth, false);
    MatchResult matchesResult;
    matchesResult.truncated = result.truncated;
    for (const auto& value : result.flat) {
        auto node = value.toObject();
        if (!matchesStructural(node, selector)) {
            continue;
        }
        if (selector.contains.isEmpty()) {
            matchesResult.matches.append(node);
            continue;
        }
        const auto needle = normalized(selector.contains);
        const auto metadata =
            node.value(QStringLiteral("id")).toString()
            + QLatin1Char('\n')
            + node.value(QStringLiteral("name")).toString()
            + QLatin1Char('\n')
            + node.value(QStringLiteral("description")).toString()
            + QLatin1Char('\n')
            + node.value(QStringLiteral("helpText")).toString();
        if (normalized(metadata).contains(needle)) {
            matchesResult.matches.append(node);
            continue;
        }
        const auto reference = referenceFromHandle(
            node.value(QStringLiteral("handle")).toString());
        const auto text = nodeText(
            reference,
            jsonStringList(
                node.value(QStringLiteral("interfaces")).toArray()));
        if (normalized(text.content).contains(needle)) {
            node.insert(QStringLiteral("text"), text.content);
            node.insert(QStringLiteral("textTruncated"), text.truncated);
            matchesResult.matches.append(node);
        } else if (text.truncated) {
            matchesResult.truncated = true;
        }
    }
    return matchesResult;
}

QJsonObject AtSpiProbe::find(
    const QString& app,
    const std::optional<qint64> processId,
    const Selector& selector)
{
    if (selector.empty()) {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("missing-selector"),
            QStringLiteral("find requires at least one selector"));
    }
    const auto appReference = resolveApplication(app, processId);
    const auto matchResult = matchingNodes(appReference, selector);
    if (matchResult.truncated) {
        throw ProbeError(
            ExitCode::LimitExceeded,
            QStringLiteral("incomplete-search"),
            QStringLiteral(
                "The accessible tree exceeded safety limits before "
                "the search could be completed"));
    }
    if (matchResult.matches.isEmpty()) {
        throw ProbeError(
            ExitCode::NotFound,
            QStringLiteral("element-not-found"),
            QStringLiteral("No accessible element matched the selector"));
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("find")},
        {QStringLiteral("app"),
         applicationJson(appReference, true)},
        {QStringLiteral("matches"), matchResult.matches},
        {QStringLiteral("count"), matchResult.matches.size()},
    };
}

QJsonObject AtSpiProbe::fullInspection(const Reference& reference)
{
    const auto base = snapshot(reference, true);
    auto result = snapshotJson(base);

    if (base.interfaces.contains(QString::fromLatin1(ActionInterface))) {
        auto actions = actionsReply(call(
            m_atspiBus,
            reference.busName,
            reference.objectPath,
            QString::fromLatin1(ActionInterface),
            QStringLiteral("GetActions"),
            {},
            QStringLiteral("a(sss)"),
            m_callTimeoutMs));
        for (qsizetype index = 0; index < actions.size(); ++index) {
            auto action = actions.at(index).toObject();
            const auto nameReply = call(
                m_atspiBus,
                reference.busName,
                reference.objectPath,
                QString::fromLatin1(ActionInterface),
                QStringLiteral("GetName"),
                {static_cast<int>(index)},
                QStringLiteral("s"),
                m_callTimeoutMs);
            action.insert(
                QStringLiteral("machineName"),
                boundedString(nameReply.arguments().at(0).toString(), 256));
            actions.replace(index, action);
        }
        result.insert(
            QStringLiteral("actions"),
            actions);
    }

    if (base.interfaces.contains(QString::fromLatin1(TextInterface))) {
        QJsonObject text;
        const auto content = nodeText(reference, base.interfaces);
        text.insert(
            QStringLiteral("characterCount"),
            content.characterCount);
        text.insert(QStringLiteral("content"), content.content);
        text.insert(QStringLiteral("truncated"), content.truncated);
        text.insert(
            QStringLiteral("caretOffset"),
            intProperty(
            m_atspiBus,
            reference,
            QString::fromLatin1(TextInterface),
                QStringLiteral("CaretOffset"),
                m_callTimeoutMs));
        result.insert(QStringLiteral("text"), text);
    }

    if (base.interfaces.contains(QString::fromLatin1(ValueInterface))) {
        QJsonObject value{
            {QStringLiteral("current"),
             doubleProperty(
                 m_atspiBus,
                 reference,
                 QString::fromLatin1(ValueInterface),
                 QStringLiteral("CurrentValue"),
                 m_callTimeoutMs)},
            {QStringLiteral("minimum"),
             doubleProperty(
                 m_atspiBus,
                 reference,
                 QString::fromLatin1(ValueInterface),
                 QStringLiteral("MinimumValue"),
                 m_callTimeoutMs)},
            {QStringLiteral("maximum"),
             doubleProperty(
                 m_atspiBus,
                 reference,
                 QString::fromLatin1(ValueInterface),
                 QStringLiteral("MaximumValue"),
                 m_callTimeoutMs)},
            {QStringLiteral("increment"),
             doubleProperty(
                 m_atspiBus,
                 reference,
                 QString::fromLatin1(ValueInterface),
                 QStringLiteral("MinimumIncrement"),
                 m_callTimeoutMs)},
            {QStringLiteral("textAvailable"), false},
        };
        if (interfaceHasProperty(
                m_atspiBus,
                reference,
                QString::fromLatin1(ValueInterface),
                QStringLiteral("Text"),
                m_callTimeoutMs)) {
            value.insert(
                QStringLiteral("text"),
                stringProperty(
                    m_atspiBus,
                    reference,
                    QString::fromLatin1(ValueInterface),
                    QStringLiteral("Text"),
                    m_callTimeoutMs));
            value.insert(QStringLiteral("textAvailable"), true);
        }
        result.insert(
            QStringLiteral("value"),
            value);
    }

    result.insert(
        QStringLiteral("editableText"),
        base.interfaces.contains(QString::fromLatin1(EditableTextInterface)));
    result.insert(
        QStringLiteral("component"),
        base.interfaces.contains(QString::fromLatin1(ComponentInterface)));
    return result;
}

QJsonObject AtSpiProbe::inspectHandle(const QString& handle)
{
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("inspect")},
        {QStringLiteral("element"),
         fullInspection(referenceFromHandle(handle))},
    };
}

QJsonObject AtSpiProbe::click(const QString& handle)
{
    const auto reference = referenceFromHandle(handle);
    const auto before = fullInspection(reference);
    const auto interfaces =
        stringListReply(call(
            m_atspiBus,
            reference.busName,
            reference.objectPath,
            QString::fromLatin1(AccessibleInterface),
            QStringLiteral("GetInterfaces"),
            {},
            QStringLiteral("as"),
            m_callTimeoutMs));
    if (!interfaces.contains(QString::fromLatin1(ActionInterface))) {
        throw ProbeError(
            ExitCode::Unsupported,
            QStringLiteral("action-unsupported"),
            QStringLiteral("The element does not expose AT-SPI Action"));
    }
    const auto actions = actionsReply(call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(ActionInterface),
        QStringLiteral("GetActions"),
        {},
        QStringLiteral("a(sss)"),
        m_callTimeoutMs));
    int selected = actions.size() == 1 ? 0 : -1;
    const QStringList preferred = {
        QStringLiteral("click"),
        QStringLiteral("press"),
        QStringLiteral("activate"),
        QStringLiteral("default"),
    };
    if (selected < 0) {
        for (const auto& preferredName : preferred) {
            for (qsizetype index = 0; index < actions.size(); ++index) {
                const auto nameReply = call(
                    m_atspiBus,
                    reference.busName,
                    reference.objectPath,
                    QString::fromLatin1(ActionInterface),
                    QStringLiteral("GetName"),
                    {static_cast<int>(index)},
                    QStringLiteral("s"),
                    m_callTimeoutMs);
                if (normalized(nameReply.arguments().at(0).toString())
                    == preferredName) {
                    selected = static_cast<int>(index);
                    break;
                }
            }
            if (selected >= 0) {
                break;
            }
        }
    }
    if (selected < 0) {
        throw ProbeError(
            ExitCode::Unsupported,
            QStringLiteral("click-action-unavailable"),
            QStringLiteral(
                "The element exposes Action but no unambiguous click/default action"));
    }
    const auto reply = call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(ActionInterface),
        QStringLiteral("DoAction"),
        {selected},
        QStringLiteral("b"),
        m_callTimeoutMs);
    if (!reply.arguments().at(0).toBool()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("action-rejected"),
            QStringLiteral("AT-SPI Action.DoAction returned false"));
    }

    QJsonObject result{
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("click")},
        {QStringLiteral("actionIndex"), selected},
        {QStringLiteral("before"), before},
    };
    try {
        result.insert(QStringLiteral("after"), fullInspection(reference));
        result.insert(QStringLiteral("invalidated"), false);
    } catch (const ProbeError& error) {
        result.insert(
            QStringLiteral("invalidated"),
            error.code() == ExitCode::NotFound);
        result.insert(
            QStringLiteral("afterError"),
            QJsonObject{
                {QStringLiteral("kind"), error.kind()},
                {QStringLiteral("message"), error.message()},
                {QStringLiteral("exitCode"),
                 static_cast<int>(error.code())},
            });
    }
    return result;
}

QJsonObject AtSpiProbe::focus(const QString& handle)
{
    const auto reference = referenceFromHandle(handle);
    const auto base = snapshot(reference, false);
    if (!base.interfaces.contains(QString::fromLatin1(ComponentInterface))) {
        throw ProbeError(
            ExitCode::Unsupported,
            QStringLiteral("focus-unsupported"),
            QStringLiteral("The element does not expose AT-SPI Component"));
    }
    const auto reply = call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(ComponentInterface),
        QStringLiteral("GrabFocus"),
        {},
        QStringLiteral("b"),
        m_callTimeoutMs);
    if (!reply.arguments().at(0).toBool()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("focus-rejected"),
            QStringLiteral("AT-SPI Component.GrabFocus returned false"));
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("focus")},
        {QStringLiteral("element"), fullInspection(reference)},
    };
}

QPoint AtSpiProbe::elementPoint(
    const QString& handle,
    const QString& position)
{
    const auto element = snapshot(referenceFromHandle(handle), true);
    if (!element.hasBounds) {
        throw ProbeError(
            ExitCode::Unsupported,
            QStringLiteral("bounds-unsupported"),
            QStringLiteral("The element does not expose screen bounds"));
    }
    const auto x = static_cast<qint64>(
        element.bounds.value(QStringLiteral("x")).toInt());
    const auto y = static_cast<qint64>(
        element.bounds.value(QStringLiteral("y")).toInt());
    const auto width = static_cast<qint64>(
        element.bounds.value(QStringLiteral("width")).toInt());
    const auto height = static_cast<qint64>(
        element.bounds.value(QStringLiteral("height")).toInt());
    if (width <= 0 || height <= 0) {
        throw ProbeError(
            ExitCode::NotFound,
            QStringLiteral("empty-bounds"),
            QStringLiteral("The element has empty or invalid screen bounds"));
    }
    qint64 pointX = 0;
    qint64 pointY = 0;
    if (position == QStringLiteral("center")) {
        pointX = x + width / 2;
        pointY = y + height / 2;
    } else if (position == QStringLiteral("top-left")) {
        pointX = x;
        pointY = y;
    } else if (position == QStringLiteral("top-right")) {
        pointX = x + width - 1;
        pointY = y;
    } else if (position == QStringLiteral("bottom-left")) {
        pointX = x;
        pointY = y + height - 1;
    } else if (position == QStringLiteral("bottom-right")) {
        pointX = x + width - 1;
        pointY = y + height - 1;
    } else {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("invalid-position"),
            QStringLiteral(
                "--position must be center, top-left, top-right, "
                "bottom-left, or bottom-right"));
    }
    if (pointX < std::numeric_limits<int>::min()
        || pointX > std::numeric_limits<int>::max()
        || pointY < std::numeric_limits<int>::min()
        || pointY > std::numeric_limits<int>::max()) {
        throw ProbeError(
            ExitCode::LimitExceeded,
            QStringLiteral("bounds-coordinate-overflow"),
            QStringLiteral(
                "The element bounds exceed supported screen coordinates"));
    }
    return {
        static_cast<int>(pointX),
        static_cast<int>(pointY),
    };
}

QJsonObject AtSpiProbe::setText(
    const QString& handle,
    const QString& text)
{
    if (text.toUtf8().size() > MaximumTextBytes) {
        throw ProbeError(
            ExitCode::LimitExceeded,
            QStringLiteral("text-limit"),
            QStringLiteral("Text exceeds the %1-byte limit")
                .arg(MaximumTextBytes));
    }
    const auto reference = referenceFromHandle(handle);
    const auto base = snapshot(reference, false);
    if (!base.interfaces.contains(
            QString::fromLatin1(EditableTextInterface))) {
        throw ProbeError(
            ExitCode::Unsupported,
            QStringLiteral("editable-text-unsupported"),
            QStringLiteral("The element does not expose AT-SPI EditableText"));
    }
    const auto reply = call(
        m_atspiBus,
        reference.busName,
        reference.objectPath,
        QString::fromLatin1(EditableTextInterface),
        QStringLiteral("SetTextContents"),
        {text},
        QStringLiteral("b"),
        m_callTimeoutMs);
    if (!reply.arguments().at(0).toBool()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("set-text-rejected"),
            QStringLiteral("AT-SPI EditableText.SetTextContents returned false"));
    }
    const auto after = fullInspection(reference);
    const auto readback =
        after.value(QStringLiteral("text"))
            .toObject()
            .value(QStringLiteral("content"))
            .toString();
    if (readback != text) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("set-text-readback-mismatch"),
            QStringLiteral("EditableText readback did not match the requested text"));
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("set-text")},
        {QStringLiteral("readback"), readback},
        {QStringLiteral("element"), after},
    };
}

QJsonObject AtSpiProbe::wait(
    const QString& app,
    const std::optional<qint64> processId,
    const Selector& selector,
    const QString& state,
    const QString& text,
    const int timeoutMs)
{
    if (selector.empty()) {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("missing-selector"),
            QStringLiteral("wait requires an element selector"));
    }
    if (timeoutMs < 0 || timeoutMs > MaximumWaitTimeoutMs) {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("invalid-timeout"),
            QStringLiteral("--timeout-ms must be between 0 and %1")
                .arg(MaximumWaitTimeoutMs));
    }
    if (!m_operationDeadline.has_value()) {
        m_operationDeadline.emplace(timeoutMs, Qt::PreciseTimer);
        m_callTimeoutMs.setDeadline(&m_operationDeadline.value());
    }
    const auto appReference = resolveApplication(app, processId);
    QElapsedTimer timer;
    timer.start();
    int polls = 0;
    while (true) {
        ++polls;
        const auto matchResult = matchingNodes(appReference, selector);
        if (matchResult.truncated) {
            throw ProbeError(
                ExitCode::LimitExceeded,
                QStringLiteral("incomplete-search"),
                QStringLiteral(
                    "The accessible tree exceeded safety limits before "
                    "wait could establish uniqueness"));
        }
        QJsonArray qualified;
        bool indeterminateText = false;
        for (const auto& value : matchResult.matches) {
            auto node = value.toObject();
            const auto states = node.value(QStringLiteral("states")).toArray();
            const auto stateMatches =
                state.isEmpty()
                || std::any_of(
                    states.cbegin(),
                    states.cend(),
                    [&state](const QJsonValue& candidate) {
                        return normalized(candidate.toString())
                            == normalized(state);
                    });
            bool textMatches = text.isEmpty();
            if (stateMatches && !textMatches) {
                const auto needle = normalized(text);
                const auto metadata =
                    node.value(QStringLiteral("name")).toString()
                    + QLatin1Char('\n')
                    + node.value(QStringLiteral("description")).toString();
                textMatches = normalized(metadata).contains(needle);
                if (!textMatches) {
                    const auto reference = referenceFromHandle(
                        node.value(QStringLiteral("handle")).toString());
                    const auto content = nodeText(
                        reference,
                        jsonStringList(
                            node.value(QStringLiteral("interfaces"))
                                .toArray()));
                    textMatches =
                        normalized(content.content).contains(needle);
                    if (textMatches) {
                        node.insert(QStringLiteral("text"), content.content);
                        node.insert(
                            QStringLiteral("textTruncated"),
                            content.truncated);
                    } else if (content.truncated) {
                        indeterminateText = true;
                    }
                }
            }
            if (stateMatches && textMatches) {
                qualified.append(node);
            }
        }
        if (indeterminateText) {
            throw ProbeError(
                ExitCode::LimitExceeded,
                QStringLiteral("incomplete-search"),
                QStringLiteral(
                    "Candidate text exceeded safety limits before wait "
                    "could establish a result"));
        }
        if (qualified.size() > 1) {
            throw ProbeError(
                ExitCode::Ambiguous,
                QStringLiteral("ambiguous-element"),
                QStringLiteral("wait matched more than one accessible element"));
        }
        if (qualified.size() == 1) {
            const auto handle =
                qualified.first()
                    .toObject()
                    .value(QStringLiteral("handle"))
                    .toString();
            return {
                {QStringLiteral("ok"), true},
                {QStringLiteral("command"), QStringLiteral("wait")},
                {QStringLiteral("elapsedMs"),
                 static_cast<qint64>(timer.elapsed())},
                {QStringLiteral("polls"), polls},
                {QStringLiteral("element"),
                 fullInspection(referenceFromHandle(handle))},
            };
        }
        if (m_operationDeadline->hasExpired()) {
            throw ProbeError(
                ExitCode::Timeout,
                QStringLiteral("wait-timeout"),
                QStringLiteral("Timed out after %1 ms waiting for the selector")
                    .arg(timeoutMs));
        }
        QThread::msleep(static_cast<unsigned long>(
            std::max<qint64>(
                1,
                std::min<qint64>(
                    200,
                    m_operationDeadline->remainingTime()))));
    }
}

QJsonObject AtSpiProbe::doctor()
{
    QJsonArray gaps;
    const auto appResult = apps();
    const auto appArray =
        appResult.value(QStringLiteral("applications")).toArray();
    int qtApplications = 0;
    for (const auto& value : appArray) {
        const auto app = value.toObject();
        const auto attributes =
            app.value(QStringLiteral("attributes")).toObject();
        const auto toolkit =
            attributes.value(QStringLiteral("toolkit")).toString();
        if (toolkit.contains(QStringLiteral("Qt"), Qt::CaseInsensitive)
            || app.value(QStringLiteral("id")).toString()
                == QStringLiteral("QGuiApplication")
            || app.value(QStringLiteral("objectPath"))
                   .toString()
                   .contains(QStringLiteral("/qt/"))) {
            ++qtApplications;
        }
    }
    if (appArray.isEmpty()) {
        gaps.append(QStringLiteral(
            "No accessible applications are visible. Start the app with "
            "QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 in this desktop session."));
    } else if (qtApplications == 0) {
        gaps.append(QStringLiteral(
            "No Qt application is visible through AT-SPI. Start the Qt app "
            "with QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1."));
    }

    const auto portal = QDBusConnection::sessionBus().interface();
    const auto portalRegistered =
        portal != nullptr
        && portal->isServiceRegistered(
                     QStringLiteral("org.freedesktop.portal.Desktop"))
               .value();
    bool screenshotAvailable = false;
    if (portalRegistered) {
        try {
            const auto versionReply = call(
                m_sessionBus,
                QStringLiteral("org.freedesktop.portal.Desktop"),
                QStringLiteral("/org/freedesktop/portal/desktop"),
                QString::fromLatin1(PropertiesInterface),
                QStringLiteral("Get"),
                {QStringLiteral("org.freedesktop.portal.Screenshot"),
                 QStringLiteral("version")},
                QStringLiteral("v"),
                m_callTimeoutMs);
            const auto variant =
                qvariant_cast<QDBusVariant>(versionReply.arguments().at(0))
                    .variant();
            screenshotAvailable =
                variant.metaType() == QMetaType::fromType<quint32>();
        } catch (const ProbeError&) {
            screenshotAvailable = false;
        }
    }
    if (!portalRegistered) {
        gaps.append(QStringLiteral(
            "xdg-desktop-portal is not registered on the session bus."));
    } else if (!screenshotAvailable) {
        gaps.append(QStringLiteral(
            "The active xdg-desktop-portal backend does not expose Screenshot."));
    }

    return {
        {QStringLiteral("ok"), gaps.isEmpty()},
        {QStringLiteral("command"), QStringLiteral("doctor")},
        {QStringLiteral("sessionBus"),
         QJsonObject{
             {QStringLiteral("connected"), m_sessionBus.isConnected()},
             {QStringLiteral("baseService"), m_sessionBus.baseService()},
         }},
        {QStringLiteral("accessibility"),
         QJsonObject{
             {QStringLiteral("busConnected"), m_atspiBus.isConnected()},
             {QStringLiteral("registryVisible"), true},
             {QStringLiteral("applicationCount"), appArray.size()},
             {QStringLiteral("qtApplicationCount"), qtApplications},
         }},
        {QStringLiteral("portal"),
         QJsonObject{
             {QStringLiteral("registered"), portalRegistered},
             {QStringLiteral("screenshotAvailable"), screenshotAvailable},
         }},
        {QStringLiteral("desktop"),
         QJsonObject{
             {QStringLiteral("sessionType"),
              qEnvironmentVariable("XDG_SESSION_TYPE")},
             {QStringLiteral("currentDesktop"),
              qEnvironmentVariable("XDG_CURRENT_DESKTOP")},
             {QStringLiteral("display"), qEnvironmentVariable("DISPLAY")},
             {QStringLiteral("waylandDisplay"),
              qEnvironmentVariable("WAYLAND_DISPLAY")},
         }},
        {QStringLiteral("gaps"), gaps},
    };
}

}
