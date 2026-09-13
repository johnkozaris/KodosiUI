#include "ui_probe/AtSpiProbe.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace kodosi::ui_probe {
namespace {

class PortalResponseReceiver final : public QObject
{
    Q_OBJECT

public:
    quint32 response = 2;
    QVariantMap results;
    bool received = false;

signals:
    void completed();

public slots:
    void Response(const quint32 code, const QVariantMap& values)
    {
        response = code;
        results = values;
        received = true;
        emit completed();
    }
};

int remainingTime(const QDeadlineTimer& deadline)
{
    const auto remaining = deadline.remainingTime();
    if (remaining <= 0) {
        throw ProbeError(
            ExitCode::Timeout,
            QStringLiteral("portal-timeout"),
            QStringLiteral("The screenshot operation deadline expired"));
    }
    return static_cast<int>(remaining);
}

QDBusMessage portalCall(
    const QString& interface,
    const QString& method,
    const QVariantList& arguments,
    const QString& signature,
    const int timeoutMs)
{
    auto request = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        interface,
        method);
    request.setArguments(arguments);
    const auto reply =
        QDBusConnection::sessionBus().call(request, QDBus::Block, timeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const auto timedOut =
            reply.errorName()
                == QStringLiteral("org.freedesktop.DBus.Error.NoReply")
            || reply.errorName()
                == QStringLiteral("org.freedesktop.DBus.Error.Timeout")
            || reply.errorName().endsWith(QStringLiteral(".TimedOut"));
        const auto unsupported =
            reply.errorName()
                == QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownMethod")
            || reply.errorName()
                == QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownInterface");
        throw ProbeError(
            timedOut
                ? ExitCode::Timeout
                : unsupported ? ExitCode::Unsupported
                              : ExitCode::RemoteError,
            timedOut
                ? QStringLiteral("portal-timeout")
                : unsupported
                ? QStringLiteral("portal-screenshot-unavailable")
                : QStringLiteral("portal-error"),
            QStringLiteral("%1.%2 failed: %3: %4")
                .arg(
                    interface,
                    method,
                    reply.errorName(),
                    reply.errorMessage()));
    }
    if (reply.signature() != signature) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-dbus-signature"),
            QStringLiteral("%1.%2 returned signature '%3', expected '%4'")
                .arg(interface, method, reply.signature(), signature));
    }
    return reply;
}

}

QJsonObject takePortalScreenshot(
    const QString& absoluteOutputPath,
    const bool interactive,
    const int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs, Qt::PreciseTimer);
    const QFileInfo destination(absoluteOutputPath);
    if (!destination.isAbsolute()) {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("relative-output-path"),
            QStringLiteral("--output must be an absolute path"));
    }
    if (!destination.dir().exists()) {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("output-directory-missing"),
            QStringLiteral("The screenshot output directory does not exist"));
    }
    if (timeoutMs < 100 || timeoutMs > AtSpiProbe::MaximumWaitTimeoutMs) {
        throw ProbeError(
            ExitCode::Usage,
            QStringLiteral("invalid-timeout"),
            QStringLiteral("Screenshot timeout must be between 100 and %1 ms")
                .arg(AtSpiProbe::MaximumWaitTimeoutMs));
    }

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || bus.baseService().isEmpty()) {
        throw ProbeError(
            ExitCode::Environment,
            QStringLiteral("session-bus-unavailable"),
            QStringLiteral("The D-Bus session bus is unavailable"));
    }
    const auto token =
        QStringLiteral("kodosi_ui_probe_%1")
            .arg(QCoreApplication::applicationPid());
    auto sender = bus.baseService();
    sender.remove(QLatin1Char(':'));
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const auto predictedRequestPath =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
            .arg(sender, token);

    PortalResponseReceiver receiver;
    if (!bus.connect(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            predictedRequestPath,
            QStringLiteral("org.freedesktop.portal.Request"),
            QStringLiteral("Response"),
            &receiver,
            SLOT(Response(uint,QVariantMap)))) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("portal-response-subscribe-failed"),
            QStringLiteral("Could not subscribe to the portal Response signal"));
    }

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("interactive"), interactive);
    options.insert(QStringLiteral("modal"), true);
    const auto reply = portalCall(
        QStringLiteral("org.freedesktop.portal.Screenshot"),
        QStringLiteral("Screenshot"),
        {QString(), options},
        QStringLiteral("o"),
        std::min(
            AtSpiProbe::DefaultCallTimeoutMs,
            remainingTime(deadline)));
    const auto requestPath =
        qvariant_cast<QDBusObjectPath>(reply.arguments().at(0)).path();
    if (requestPath.isEmpty()) {
        bus.disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            predictedRequestPath,
            QStringLiteral("org.freedesktop.portal.Request"),
            QStringLiteral("Response"),
            &receiver,
            SLOT(Response(uint,QVariantMap)));
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-portal-request"),
            QStringLiteral("Screenshot returned an empty request path"));
    }

    if (requestPath != predictedRequestPath) {
        bus.disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            predictedRequestPath,
            QStringLiteral("org.freedesktop.portal.Request"),
            QStringLiteral("Response"),
            &receiver,
            SLOT(Response(uint,QVariantMap)));
        if (!bus.connect(
                QStringLiteral("org.freedesktop.portal.Desktop"),
                requestPath,
                QStringLiteral("org.freedesktop.portal.Request"),
                QStringLiteral("Response"),
                &receiver,
                SLOT(Response(uint,QVariantMap)))) {
            throw ProbeError(
                ExitCode::RemoteError,
                QStringLiteral("portal-response-subscribe-failed"),
                QStringLiteral(
                    "Could not subscribe to the returned portal request"));
        }
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(
        &receiver,
        &PortalResponseReceiver::completed,
        &loop,
        &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(remainingTime(deadline));
    loop.exec();
    bus.disconnect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        requestPath,
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"),
        &receiver,
        SLOT(Response(uint,QVariantMap)));

    if (!receiver.received) {
        auto close = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            requestPath,
            QStringLiteral("org.freedesktop.portal.Request"),
            QStringLiteral("Close"));
        static_cast<void>(bus.call(close, QDBus::NoBlock));
        throw ProbeError(
            ExitCode::Timeout,
            QStringLiteral("portal-timeout"),
            QStringLiteral("Timed out waiting for portal screenshot consent"));
    }
    if (receiver.response == 1U) {
        throw ProbeError(
            ExitCode::PortalDenied,
            QStringLiteral("portal-cancelled"),
            QStringLiteral("The screenshot request was cancelled"));
    }
    if (receiver.response != 0U) {
        throw ProbeError(
            ExitCode::PortalDenied,
            QStringLiteral("portal-denied"),
            QStringLiteral("The screenshot portal rejected the request"));
    }

    const auto uriValue = receiver.results.value(QStringLiteral("uri"));
    if (uriValue.metaType() != QMetaType::fromType<QString>()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("invalid-portal-result"),
            QStringLiteral("Screenshot response did not contain a string URI"));
    }
    const auto uri = QUrl(uriValue.toString());
    if (!uri.isValid() || !uri.isLocalFile()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("unsupported-screenshot-uri"),
            QStringLiteral("Screenshot portal returned a non-local URI"));
    }

    QFile source(uri.toLocalFile());
    if (!source.open(QIODevice::ReadOnly)) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("screenshot-read-failed"),
            QStringLiteral("Could not read the portal screenshot result"));
    }
    QSaveFile output(destination.absoluteFilePath());
    if (!output.open(QIODevice::WriteOnly)) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("screenshot-output-failed"),
            QStringLiteral("Could not open the requested screenshot output"));
    }
    constexpr qint64 maximumScreenshotBytes = 100 * 1024 * 1024;
    qint64 copied = 0;
    while (!source.atEnd()) {
        static_cast<void>(remainingTime(deadline));
        const auto chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() && source.error() != QFile::NoError) {
            output.cancelWriting();
            throw ProbeError(
                ExitCode::RemoteError,
                QStringLiteral("screenshot-read-failed"),
                QStringLiteral("Failed while reading the portal screenshot"));
        }
        copied += chunk.size();
        if (copied > maximumScreenshotBytes
            || output.write(chunk) != chunk.size()) {
            output.cancelWriting();
            throw ProbeError(
                copied > maximumScreenshotBytes
                    ? ExitCode::LimitExceeded
                    : ExitCode::RemoteError,
                copied > maximumScreenshotBytes
                    ? QStringLiteral("screenshot-size-limit")
                    : QStringLiteral("screenshot-write-failed"),
                QStringLiteral("Could not safely persist the portal screenshot"));
        }
    }
    if (!output.commit()) {
        throw ProbeError(
            ExitCode::RemoteError,
            QStringLiteral("screenshot-commit-failed"),
            QStringLiteral("Could not atomically commit the screenshot output"));
    }

    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("screenshot")},
        {QStringLiteral("interactive"), interactive},
        {QStringLiteral("portalUri"), uri.toString()},
        {QStringLiteral("path"), destination.absoluteFilePath()},
        {QStringLiteral("bytes"), copied},
    };
}

}

#include "PortalScreenshot.moc"
