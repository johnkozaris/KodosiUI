#include "ui_probe/InputAdapter.hpp"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <limits>
#include <memory>
#include <poll.h>
#include <ranges>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace kodosi::ui_probe {
namespace {

constexpr auto PortalService = "org.freedesktop.portal.Desktop";
constexpr auto PortalPath = "/org/freedesktop/portal/desktop";
constexpr auto RemoteDesktopInterface =
    "org.freedesktop.portal.RemoteDesktop";
constexpr auto ScreenCastInterface =
    "org.freedesktop.portal.ScreenCast";
constexpr auto RequestInterface = "org.freedesktop.portal.Request";
constexpr auto SessionInterface = "org.freedesktop.portal.Session";
constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto IntrospectableInterface =
    "org.freedesktop.DBus.Introspectable";
constexpr auto RegistryService = "org.a11y.atspi.Registry";
constexpr auto DeviceControllerPath =
    "/org/a11y/atspi/registry/deviceeventcontroller";
constexpr auto DeviceControllerInterface =
    "org.a11y.atspi.DeviceEventController";
constexpr auto AccessibilityBusService = "org.a11y.Bus";
constexpr auto AccessibilityBusPath = "/org/a11y/bus";
constexpr auto AccessibilityBusInterface = "org.a11y.Bus";

constexpr int KeyboardDevice = 1;
constexpr int PointerDevice = 2;
constexpr int MonitorSource = 1;
constexpr int WindowSource = 2;
constexpr int PortalButtonLeft = 0x110;
constexpr int PortalButtonRight = 0x111;
constexpr int PortalButtonMiddle = 0x112;

[[noreturn]] void fail(
    const ExitCode code,
    const QString& kind,
    const QString& message)
{
    throw ProbeError(code, kind, message);
}

qint64 monotonicMilliseconds()
{
    timespec value {};
    if (::clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        fail(
            ExitCode::Environment,
            QStringLiteral("monotonic-clock-unavailable"),
            QStringLiteral("Could not read the monotonic clock"));
    }
    return static_cast<qint64>(value.tv_sec) * 1000
        + value.tv_nsec / 1000000;
}

int remainingMilliseconds(
    const qint64 absoluteDeadlineMs,
    const int maximumMs)
{
    const auto remaining = absoluteDeadlineMs - monotonicMilliseconds();
    if (remaining <= 0) {
        fail(
            ExitCode::Timeout,
            QStringLiteral("sidecar-start-timeout"),
            QStringLiteral(
                "Timed out waiting for RemoteDesktop consent and "
                "sidecar startup"));
    }
    return static_cast<int>(
        std::min<qint64>(remaining, std::max(maximumMs, 1)));
}

QString processStartTime(const qint64 processId)
{
    QFile statFile(
        QStringLiteral("/proc/%1/stat").arg(processId));
    if (!statFile.open(QIODevice::ReadOnly) || statFile.size() > 8192) {
        return {};
    }
    const auto stat = statFile.readAll();
    const auto commandEnd = stat.lastIndexOf(')');
    if (commandEnd < 0) {
        return {};
    }
    const auto fields =
        stat.sliced(commandEnd + 1).simplified().split(' ');
    return fields.size() > 19 ? QString::fromLatin1(fields.at(19))
                              : QString();
}

bool sameProcessInstance(
    const qint64 processId,
    const QString& expectedStartTime)
{
    return processId > 0 && !expectedStartTime.isEmpty()
        && processStartTime(processId) == expectedStartTime;
}

int exactProcessDescriptor(
    const qint64 processId,
    const QString& expectedStartTime,
    bool* selectedProcessExited)
{
    *selectedProcessExited = false;
    const auto descriptor =
        static_cast<int>(
            ::syscall(
                SYS_pidfd_open,
                static_cast<pid_t>(processId),
                0U));
    if (descriptor < 0) {
        *selectedProcessExited = errno == ESRCH;
        return -1;
    }
    if (processStartTime(processId) != expectedStartTime) {
        static_cast<void>(::close(descriptor));
        *selectedProcessExited = true;
        return -1;
    }
    return descriptor;
}

class StartupLifecycle final : public QObject
{
    Q_OBJECT

public:
    StartupLifecycle(
        const int descriptor,
        const qint64 launcherPid,
        QString startupToken,
        const qint64 absoluteDeadlineMs,
        QObject* parent = nullptr)
        : QObject(parent)
        , m_descriptor(descriptor)
        , m_launcherPid(launcherPid)
        , m_startupToken(std::move(startupToken))
        , m_absoluteDeadlineMs(absoluteDeadlineMs)
    {
        sigemptyset(&m_signals);
        sigaddset(&m_signals, SIGTERM);
        sigaddset(&m_signals, SIGINT);
        sigaddset(&m_signals, SIGHUP);
        if (::sigprocmask(SIG_BLOCK, &m_signals, &m_oldMask) != 0) {
            fail(
                ExitCode::Environment,
                QStringLiteral("sidecar-signal-mask-failed"),
                QStringLiteral(
                    "Could not install startup signal cancellation"));
        }
        m_signalMaskInstalled = true;
        m_signalDescriptor =
            ::signalfd(-1, &m_signals, SFD_CLOEXEC | SFD_NONBLOCK);
        if (m_signalDescriptor < 0) {
            fail(
                ExitCode::Environment,
                QStringLiteral("sidecar-signal-fd-failed"),
                QStringLiteral(
                    "Could not monitor startup cancellation signals"));
        }
        const auto flags = ::fcntl(m_descriptor, F_GETFL);
        const auto descriptorFlags = ::fcntl(m_descriptor, F_GETFD);
        if (flags < 0 || descriptorFlags < 0
            || ::fcntl(m_descriptor, F_SETFL, flags | O_NONBLOCK) < 0
            || ::fcntl(
                   m_descriptor,
                   F_SETFD,
                   descriptorFlags | FD_CLOEXEC)
                < 0) {
            fail(
                ExitCode::Environment,
                QStringLiteral("sidecar-lifecycle-fd-invalid"),
                QStringLiteral(
                    "Could not secure the startup lifecycle channel"));
        }
        m_lifecycleNotifier =
            std::make_unique<QSocketNotifier>(
                m_descriptor,
                QSocketNotifier::Read,
                this);
        m_signalNotifier =
            std::make_unique<QSocketNotifier>(
                m_signalDescriptor,
                QSocketNotifier::Read,
                this);
        connect(
            m_lifecycleNotifier.get(),
            &QSocketNotifier::activated,
            this,
            [this] { readLifecycle(); });
        connect(
            m_signalNotifier.get(),
            &QSocketNotifier::activated,
            this,
            [this] { readSignals(); });
        int parentSignal = 0;
        if (::prctl(PR_GET_PDEATHSIG, &parentSignal) != 0
            || parentSignal != SIGTERM) {
            cancel(QStringLiteral(
                "The input sidecar parent-death binding was not inherited"));
        } else if (::getppid() != m_launcherPid) {
            cancel(QStringLiteral("The input-start launcher exited"));
        }
    }

    ~StartupLifecycle() override
    {
        restoreSignalMask();
        closeDescriptors(m_committed);
    }

    [[nodiscard]] bool cancelled() const
    {
        return m_cancelled;
    }

    void processSignals()
    {
        readSignals();
    }

    [[nodiscard]] bool terminationRequestedState() const
    {
        return m_terminationRequested;
    }

    void throwIfCancelled() const
    {
        if (m_cancelled) {
            fail(
                ExitCode::Environment,
                QStringLiteral("sidecar-start-cancelled"),
                m_cancelReason.isEmpty()
                    ? QStringLiteral(
                          "The input-start launcher cancelled startup")
                    : m_cancelReason);
        }
    }

    void readyAndAwaitCommit(
        const QJsonObject& identity,
        const std::function<void()>& commitState)
    {
        throwIfCancelled();
        auto ready = identity;
        ready.insert(QStringLiteral("type"), QStringLiteral("ready"));
        ready.insert(QStringLiteral("startupToken"), m_startupToken);
        writeFrame(ready);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(
            this,
            &StartupLifecycle::cancelledSignal,
            &loop,
            &QEventLoop::quit);
        connect(
            this,
            &StartupLifecycle::commitReceived,
            &loop,
            &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(
            remainingMilliseconds(
                m_absoluteDeadlineMs,
                AtSpiProbe::MaximumWaitTimeoutMs));
        if (m_commit.isEmpty() && !m_cancelled) {
            loop.exec();
        }
        throwIfCancelled();
        if (m_commit.isEmpty()) {
            fail(
                ExitCode::Timeout,
                QStringLiteral("sidecar-commit-timeout"),
                QStringLiteral(
                    "Timed out waiting for the launcher to commit startup"));
        }
        for (auto iterator = identity.constBegin();
             iterator != identity.constEnd();
             ++iterator) {
            if (m_commit.value(iterator.key()) != iterator.value()) {
                fail(
                    ExitCode::RemoteError,
                    QStringLiteral("sidecar-commit-mismatch"),
                    QStringLiteral(
                        "The launcher committed a different sidecar "
                        "identity"));
            }
        }
        if (m_commit.value(QStringLiteral("type")).toString()
                != QStringLiteral("commit")
            || m_commit.value(QStringLiteral("startupToken")).toString()
                != m_startupToken) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("sidecar-commit-mismatch"),
                QStringLiteral(
                    "The launcher startup commit was invalid"));
        }

        readLifecycle();
        readSignals();
        throwIfCancelled();
        commitState();
        readLifecycle();
        readSignals();
        throwIfCancelled();
        if (::prctl(PR_SET_PDEATHSIG, 0) != 0) {
            fail(
                ExitCode::Environment,
                QStringLiteral("sidecar-parent-binding-failed"),
                QStringLiteral(
                    "Could not disarm the launcher parent-death binding"));
        }
        auto acknowledged = identity;
        acknowledged.insert(
            QStringLiteral("type"),
            QStringLiteral("committed"));
        acknowledged.insert(
            QStringLiteral("startupToken"),
            m_startupToken);
        writeFrame(acknowledged);
        m_committed = true;
        closeLifecycleDescriptor();
    }

signals:
    void cancelledSignal();
    void commitReceived();
    void terminationRequested();

private:
    void cancel(const QString& reason)
    {
        if (m_cancelled || m_committed) {
            return;
        }
        m_cancelled = true;
        m_cancelReason = reason;
        emit cancelledSignal();
    }

    void readLifecycle()
    {
        char bytes[4096];
        while (true) {
            const auto count =
                ::read(m_descriptor, bytes, sizeof(bytes));
            if (count > 0) {
                m_buffer.append(bytes, count);
                try {
                    while (auto message =
                               InputProtocol::takeFrame(&m_buffer)) {
                        if (message->value(QStringLiteral("type")).toString()
                            == QStringLiteral("cancel")) {
                            cancel(QStringLiteral(
                                "The input-start launcher cancelled startup"));
                            return;
                        }
                        if (!m_commit.isEmpty()) {
                            cancel(QStringLiteral(
                                "The input-start launcher sent multiple "
                                "startup commits"));
                            return;
                        }
                        m_commit = *message;
                        emit commitReceived();
                    }
                } catch (const ProbeError&) {
                    cancel(QStringLiteral(
                        "The input-start launcher sent an invalid lifecycle "
                        "message"));
                }
                continue;
            }
            if (count == 0) {
                cancel(QStringLiteral(
                    "The input-start launcher exited before startup "
                    "committed"));
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                cancel(QStringLiteral(
                    "The input-start lifecycle channel failed"));
            }
            return;
        }
    }

    void readSignals()
    {
        signalfd_siginfo signal {};
        while (true) {
            const auto count =
                ::read(m_signalDescriptor, &signal, sizeof(signal));
            if (count == sizeof(signal)) {
                if (m_committed) {
                    if (!m_terminationRequested) {
                        m_terminationRequested = true;
                        emit terminationRequested();
                    }
                } else {
                    cancel(QStringLiteral(
                        "The input sidecar startup was interrupted"));
                }
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            return;
        }
    }

    void writeFrame(const QJsonObject& message)
    {
        const auto frame = InputProtocol::frame(message);
        qsizetype offset = 0;
        while (offset < frame.size()) {
            const auto count =
                ::send(
                    m_descriptor,
                    frame.constData() + offset,
                    static_cast<size_t>(frame.size() - offset),
                    MSG_NOSIGNAL);
            if (count > 0) {
                offset += count;
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0
                && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                pollfd descriptor{
                    .fd = m_descriptor,
                    .events = POLLOUT,
                    .revents = 0,
                };
                const auto timeout =
                    remainingMilliseconds(m_absoluteDeadlineMs, 1000);
                if (::poll(&descriptor, 1, timeout) > 0) {
                    continue;
                }
            }
            cancel(QStringLiteral(
                "The input-start lifecycle channel closed"));
            throwIfCancelled();
        }
    }

    void closeDescriptors(const bool closeLifecycle)
    {
        if (m_lifecycleNotifier) {
            m_lifecycleNotifier->setEnabled(false);
            m_lifecycleNotifier.reset();
        }
        if (m_signalNotifier) {
            m_signalNotifier->setEnabled(false);
            m_signalNotifier.reset();
        }
        if (closeLifecycle) {
            closeLifecycleDescriptor();
        }
        if (m_signalDescriptor >= 0) {
            static_cast<void>(::close(m_signalDescriptor));
            m_signalDescriptor = -1;
        }
    }

    void closeLifecycleDescriptor()
    {
        if (m_lifecycleNotifier) {
            m_lifecycleNotifier->setEnabled(false);
            m_lifecycleNotifier.reset();
        }
        if (m_descriptor >= 0) {
            static_cast<void>(::close(m_descriptor));
            m_descriptor = -1;
        }
    }

    void restoreSignalMask()
    {
        if (!m_signalMaskInstalled) {
            return;
        }
        signalfd_siginfo signal {};
        while (m_signalDescriptor >= 0
               && ::read(
                      m_signalDescriptor,
                      &signal,
                      sizeof(signal))
                   == sizeof(signal)) {
        }
        static_cast<void>(
            ::sigprocmask(SIG_SETMASK, &m_oldMask, nullptr));
        m_signalMaskInstalled = false;
    }

    int m_descriptor = -1;
    int m_signalDescriptor = -1;
    qint64 m_launcherPid = -1;
    QString m_startupToken;
    qint64 m_absoluteDeadlineMs = 0;
    sigset_t m_signals {};
    sigset_t m_oldMask {};
    bool m_signalMaskInstalled = false;
    bool m_cancelled = false;
    bool m_committed = false;
    bool m_terminationRequested = false;
    QString m_cancelReason;
    QByteArray m_buffer;
    QJsonObject m_commit;
    std::unique_ptr<QSocketNotifier> m_lifecycleNotifier;
    std::unique_ptr<QSocketNotifier> m_signalNotifier;
};

StartupLifecycle* startupLifecycle = nullptr;

QDBusMessage checkedCall(
    const QDBusConnection& bus,
    const QString& service,
    const QString& path,
    const QString& interface,
    const QString& method,
    const QVariantList& arguments,
    const QString& signature,
    const int timeoutMs = AtSpiProbe::DefaultCallTimeoutMs,
    const bool cancellableStartup = false)
{
    auto message =
        QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    QDBusMessage reply;
    if (cancellableStartup && startupLifecycle != nullptr) {
        startupLifecycle->throwIfCancelled();
        QDBusPendingCallWatcher watcher(
            bus.asyncCall(message, timeoutMs));
        QEventLoop loop;
        QObject::connect(
            &watcher,
            &QDBusPendingCallWatcher::finished,
            &loop,
            &QEventLoop::quit);
        QObject::connect(
            startupLifecycle,
            &StartupLifecycle::cancelledSignal,
            &loop,
            &QEventLoop::quit);
        if (!watcher.isFinished()
            && !startupLifecycle->cancelled()) {
            loop.exec();
        }
        startupLifecycle->throwIfCancelled();
        reply = watcher.reply();
    } else {
        reply = bus.call(message, QDBus::Block, timeoutMs);
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const auto unsupported =
            reply.errorName()
                == QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod")
            || reply.errorName()
                == QStringLiteral(
                    "org.freedesktop.DBus.Error.UnknownInterface")
            || (interface == QString::fromLatin1(PropertiesInterface)
                && reply.errorName()
                    == QStringLiteral(
                        "org.freedesktop.DBus.Error.InvalidArgs"));
        const auto timeout =
            reply.errorName()
                == QStringLiteral("org.freedesktop.DBus.Error.NoReply")
            || reply.errorName()
                == QStringLiteral("org.freedesktop.DBus.Error.Timeout");
        fail(
            timeout
                ? ExitCode::Timeout
                : unsupported ? ExitCode::Unsupported
                              : ExitCode::RemoteError,
            timeout
                ? QStringLiteral("input-timeout")
                : unsupported ? QStringLiteral("input-unsupported")
                              : QStringLiteral("input-remote-error"),
            QStringLiteral("%1.%2 failed: %3: %4")
                .arg(
                    interface,
                    method,
                    reply.errorName(),
                    reply.errorMessage()));
    }
    if (reply.type() != QDBusMessage::ReplyMessage
        || reply.signature() != signature) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("invalid-input-reply"),
            QStringLiteral("%1.%2 returned signature '%3', expected '%4'")
                .arg(interface, method, reply.signature(), signature));
    }
    return reply;
}

QString randomToken()
{
    QByteArray bytes(24, '\0');
    auto* generator = QRandomGenerator::system();
    for (qsizetype offset = 0; offset < bytes.size();
         offset += static_cast<qsizetype>(sizeof(quint32))) {
        const auto value = generator->generate();
        const auto count = std::min<qsizetype>(
            static_cast<qsizetype>(sizeof(value)),
            bytes.size() - offset);
        std::memcpy(bytes.data() + offset, &value, count);
    }
    return QString::fromLatin1(
        bytes.toBase64(
            QByteArray::Base64UrlEncoding
            | QByteArray::OmitTrailingEquals));
}

QString objectToken(const QString& prefix)
{
    auto token = prefix + randomToken().left(16);
    token.replace(QLatin1Char('-'), QLatin1Char('_'));
    return token;
}

QString runtimeRoot()
{
    const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const QFileInfo info(runtime);
    if (runtime.isEmpty() || !info.isAbsolute() || !info.isDir()
        || info.isSymLink()
        || info.ownerId() != static_cast<uint>(::getuid())
        || (info.permissions()
            & (QFileDevice::WriteGroup | QFileDevice::WriteOther))) {
        fail(
            ExitCode::Environment,
            QStringLiteral("unsafe-runtime-directory"),
            QStringLiteral(
                "XDG_RUNTIME_DIR must be an absolute directory owned "
                "by the current user"));
    }
    const auto root = QDir(runtime).filePath(
        QStringLiteral("kodosi-ui-probe"));
    QDir directory;
    if (!directory.mkpath(root)
        || !QFile::setPermissions(
            root,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner
                | QFileDevice::ExeOwner)) {
        fail(
            ExitCode::Environment,
            QStringLiteral("runtime-directory-failed"),
            QStringLiteral("Could not secure the input sidecar directory"));
    }
    const QFileInfo rootInfo(root);
    if (rootInfo.isSymLink()
        || rootInfo.ownerId() != static_cast<uint>(::getuid())
        || (rootInfo.permissions()
            & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
               | QFileDevice::ExeGroup | QFileDevice::ReadOther
               | QFileDevice::WriteOther | QFileDevice::ExeOther))) {
        fail(
            ExitCode::Environment,
            QStringLiteral("unsafe-sidecar-directory"),
            QStringLiteral("The input sidecar directory is not private"));
    }
    return root;
}

QString statePath()
{
    return QDir(runtimeRoot()).filePath(QStringLiteral("input-state.json"));
}

QString stateLockPath()
{
    return QDir(runtimeRoot()).filePath(
        QStringLiteral("input-state.lock"));
}

QString restoreTokenPath()
{
    return QDir(runtimeRoot()).filePath(
        QStringLiteral("remote-desktop-restore-token"));
}

QString startupFailurePath(const QString& startupToken)
{
    return QDir(runtimeRoot()).filePath(
        QStringLiteral("input-start-failure-%1.json")
            .arg(startupToken));
}

QString pendingStatePath()
{
    return QDir(runtimeRoot()).filePath(
        QStringLiteral("input-pending.json"));
}

QString pendingStateLockPath()
{
    return QDir(runtimeRoot()).filePath(
        QStringLiteral("input-pending.lock"));
}

QString launcherLockPath()
{
    return QDir(runtimeRoot()).filePath(
        QStringLiteral("input-launcher.lock"));
}

bool privateOwnerFile(const QFileInfo& info)
{
    return info.ownerId() == static_cast<uint>(::getuid())
        && !(info.permissions()
             & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
                | QFileDevice::ExeGroup | QFileDevice::ReadOther
                | QFileDevice::WriteOther | QFileDevice::ExeOther));
}

void writePrivateJson(const QString& path, const QJsonObject& object)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-state-write-failed"),
            QStringLiteral("Could not write sidecar state"));
    }
    if (!file.setPermissions(
            QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(QJsonDocument(object).toJson(QJsonDocument::Compact))
            < 0
        || !file.commit()) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-state-write-failed"),
            QStringLiteral("Could not atomically commit sidecar state"));
    }
}

void withStateLock(const std::function<void()>& action)
{
    QLockFile lock(stateLockPath());
    lock.setStaleLockTime(0);
    if (!lock.tryLock(5000)) {
        fail(
            ExitCode::Timeout,
            QStringLiteral("sidecar-state-lock-timeout"),
            QStringLiteral(
                "Timed out serializing input sidecar state"));
    }
    action();
}

void writeSidecarState(const QJsonObject& state)
{
    withStateLock(
        [&] { writePrivateJson(statePath(), state); });
}

QJsonObject readState(const bool required)
{
    const auto path = statePath();
    const QFileInfo info(path);
    if (!info.exists()) {
        if (required) {
            fail(
                ExitCode::Environment,
                QStringLiteral("sidecar-not-running"),
                QStringLiteral(
                    "The Wayland input sidecar is not running; "
                    "run input-start"));
        }
        return {};
    }
    if (!info.isFile() || !privateOwnerFile(info)
        || info.size() <= 0 || info.size() > 16384) {
        fail(
            ExitCode::Environment,
            QStringLiteral("unsafe-sidecar-state"),
            QStringLiteral("The input sidecar state file is unsafe"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-state-read-failed"),
            QStringLiteral("Could not read sidecar state"));
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("invalid-sidecar-state"),
            QStringLiteral("The input sidecar state file is invalid"));
    }
    const auto state = document.object();
    const auto socket = QFileInfo(
        state.value(QStringLiteral("socket")).toString());
    if (!socket.isAbsolute()
        || socket.absolutePath() != QFileInfo(runtimeRoot()).absoluteFilePath()
        || !socket.fileName().startsWith(QStringLiteral("input-"))
        || !socket.fileName().endsWith(QStringLiteral(".sock"))
        || state.value(QStringLiteral("nonce")).toString().size() != 32
        || state.value(QStringLiteral("startupToken")).toString().size()
            != 32
        || !state.value(QStringLiteral("committed")).isBool()
        || state.value(QStringLiteral("pid")).toInteger() <= 0
        || state.value(QStringLiteral("processStartTime"))
               .toString()
               .isEmpty()) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("invalid-sidecar-state"),
            QStringLiteral("The input sidecar state contains unsafe values"));
    }
    return state;
}

bool sameSidecarIdentity(
    const QJsonObject& first,
    const QJsonObject& second)
{
    return !first.isEmpty() && !second.isEmpty()
        && first.value(QStringLiteral("pid"))
            == second.value(QStringLiteral("pid"))
        && first.value(QStringLiteral("processStartTime"))
            == second.value(QStringLiteral("processStartTime"))
        && first.value(QStringLiteral("socket"))
            == second.value(QStringLiteral("socket"))
        && first.value(QStringLiteral("nonce"))
            == second.value(QStringLiteral("nonce"))
        && first.value(QStringLiteral("startupToken"))
            == second.value(QStringLiteral("startupToken"));
}

bool exactStateExists(const QJsonObject& expected)
{
    try {
        return sameSidecarIdentity(
            readState(false),
            expected);
    } catch (const ProbeError&) {
        return false;
    }
}

void removeExactStateArtifacts(const QJsonObject& expected)
{
    try {
        withStateLock([&] {
            const auto current = readState(false);
            if (!sameSidecarIdentity(current, expected)) {
                return;
            }
            const auto socket =
                current.value(QStringLiteral("socket")).toString();
            if (QFileInfo(socket).absolutePath()
                == QFileInfo(runtimeRoot()).absoluteFilePath()) {
                QLocalServer::removeServer(socket);
            }
            QFile::remove(statePath());
        });
    } catch (const ProbeError&) {
    }
}

QJsonObject readPendingState()
{
    const auto path = pendingStatePath();
    const QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }
    if (!info.isFile() || !privateOwnerFile(info)
        || info.size() <= 0 || info.size() > 16384) {
        fail(
            ExitCode::Environment,
            QStringLiteral("unsafe-sidecar-pending-state"),
            QStringLiteral(
                "The pending input sidecar state file is unsafe"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-pending-state-read-failed"),
            QStringLiteral(
                "Could not read pending input sidecar state"));
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError
        || !document.isObject()) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("invalid-sidecar-pending-state"),
            QStringLiteral(
                "The pending input sidecar state is invalid"));
    }
    const auto pending = document.object();
    const auto phase =
        pending.value(QStringLiteral("phase")).toString();
    if (pending.value(QStringLiteral("pid")).toInteger() <= 0
        || pending.value(QStringLiteral("processStartTime"))
               .toString()
               .isEmpty()
        || pending.value(QStringLiteral("startupToken"))
               .toString()
               .size()
            != 32
        || (phase != QStringLiteral("launcher")
            && phase != QStringLiteral("sidecar"))) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("invalid-sidecar-pending-state"),
            QStringLiteral(
                "The pending input sidecar state contains unsafe values"));
    }
    return pending;
}

void writePendingState(const QJsonObject& pending)
{
    QLockFile lock(pendingStateLockPath());
    lock.setStaleLockTime(0);
    if (!lock.tryLock(5000)) {
        fail(
            ExitCode::Timeout,
            QStringLiteral("sidecar-pending-lock-timeout"),
            QStringLiteral(
                "Timed out serializing pending input sidecar state"));
    }
    writePrivateJson(pendingStatePath(), pending);
}

void removePendingState(const QString& startupToken)
{
    try {
        QLockFile lock(pendingStateLockPath());
        lock.setStaleLockTime(0);
        if (!lock.tryLock(5000)) {
            return;
        }
        const auto current = readPendingState();
        if (!current.isEmpty()
            && current.value(QStringLiteral("startupToken")).toString()
                == startupToken) {
            QFile::remove(pendingStatePath());
        }
    } catch (const ProbeError&) {
    }
}

QJsonObject errorJson(const ProbeError& error)
{
    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"),
         QJsonObject{
             {QStringLiteral("kind"), error.kind()},
             {QStringLiteral("message"), error.message()},
             {QStringLiteral("exitCode"), static_cast<int>(error.code())},
         }},
    };
}

class PortalResponse final : public QObject
{
    Q_OBJECT

public:
    quint32 code = 2;
    QVariantMap results;
    bool received = false;

signals:
    void done();

public slots:
    void Response(const quint32 response, const QVariantMap& values)
    {
        code = response;
        results = values;
        received = true;
        emit done();
    }
};

class PortalRequestCloseGuard final
{
public:
    PortalRequestCloseGuard(
        QDBusConnection bus,
        QString path)
        : m_bus(std::move(bus))
        , m_path(std::move(path))
    {
    }

    ~PortalRequestCloseGuard()
    {
        if (!m_armed || m_path.isEmpty()) {
            return;
        }
        auto close = QDBusMessage::createMethodCall(
            QString::fromLatin1(PortalService),
            m_path,
            QString::fromLatin1(RequestInterface),
            QStringLiteral("Close"));
        static_cast<void>(m_bus.call(close, QDBus::NoBlock));
    }

    void setPath(QString path)
    {
        m_path = std::move(path);
    }

    void disarm()
    {
        m_armed = false;
    }

private:
    QDBusConnection m_bus;
    QString m_path;
    bool m_armed = true;
};

QVariantMap portalRequest(
    const QString& interface,
    const QString& method,
    QVariantList arguments,
    const qint64 absoluteDeadlineMs)
{
    auto bus = QDBusConnection::sessionBus();
    const auto token =
        objectToken(
            QStringLiteral("kodosi_input_%1_")
                .arg(QCoreApplication::applicationPid()));
    auto sender = bus.baseService();
    sender.remove(QLatin1Char(':'));
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const auto predicted =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
            .arg(sender, token);

    auto options = arguments.takeLast().toMap();
    options.insert(QStringLiteral("handle_token"), token);
    arguments.append(options);

    PortalResponse receiver;
    if (!bus.connect(
            QString::fromLatin1(PortalService),
            predicted,
            QString::fromLatin1(RequestInterface),
            QStringLiteral("Response"),
            &receiver,
            SLOT(Response(uint,QVariantMap)))) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("portal-response-subscribe-failed"),
            QStringLiteral("Could not subscribe to portal response"));
    }
    PortalRequestCloseGuard closeGuard(bus, predicted);
    const auto reply = checkedCall(
        bus,
        QString::fromLatin1(PortalService),
        QString::fromLatin1(PortalPath),
        interface,
        method,
        arguments,
        QStringLiteral("o"),
        remainingMilliseconds(
            absoluteDeadlineMs,
            AtSpiProbe::DefaultCallTimeoutMs),
        true);
    const auto actual =
        qvariant_cast<QDBusObjectPath>(reply.arguments().first()).path();
    closeGuard.setPath(actual);
    if (actual != predicted) {
        bus.disconnect(
            QString::fromLatin1(PortalService),
            predicted,
            QString::fromLatin1(RequestInterface),
            QStringLiteral("Response"),
            &receiver,
            SLOT(Response(uint,QVariantMap)));
        if (!bus.connect(
                QString::fromLatin1(PortalService),
                actual,
                QString::fromLatin1(RequestInterface),
                QStringLiteral("Response"),
                &receiver,
                SLOT(Response(uint,QVariantMap)))) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("portal-response-subscribe-failed"),
                QStringLiteral("Could not subscribe to returned portal request"));
        }
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&receiver, &PortalResponse::done, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (startupLifecycle != nullptr) {
        QObject::connect(
            startupLifecycle,
            &StartupLifecycle::cancelledSignal,
            &loop,
            &QEventLoop::quit);
    }
    timer.start(
        remainingMilliseconds(
            absoluteDeadlineMs,
            AtSpiProbe::MaximumWaitTimeoutMs));
    if (!receiver.received
        && (startupLifecycle == nullptr
            || !startupLifecycle->cancelled())) {
        loop.exec();
    }
    bus.disconnect(
        QString::fromLatin1(PortalService),
        actual,
        QString::fromLatin1(RequestInterface),
        QStringLiteral("Response"),
        &receiver,
        SLOT(Response(uint,QVariantMap)));
    if (startupLifecycle != nullptr) {
        startupLifecycle->throwIfCancelled();
    }
    if (!receiver.received) {
        fail(
            ExitCode::Timeout,
            QStringLiteral("portal-timeout"),
            QStringLiteral("Timed out waiting for RemoteDesktop consent"));
    }
    closeGuard.disarm();
    if (receiver.code != 0U) {
        fail(
            ExitCode::PortalDenied,
            receiver.code == 1U
                ? QStringLiteral("portal-cancelled")
                : QStringLiteral("portal-denied"),
            QStringLiteral("RemoteDesktop consent was not granted"));
    }
    return receiver.results;
}

quint32 portalProperty(
    const QString& interface,
    const QString& property,
    const int timeoutMs = AtSpiProbe::DefaultCallTimeoutMs,
    const bool cancellableStartup = false)
{
    const auto reply = checkedCall(
        QDBusConnection::sessionBus(),
        QString::fromLatin1(PortalService),
        QString::fromLatin1(PortalPath),
        QString::fromLatin1(PropertiesInterface),
        QStringLiteral("Get"),
        {interface, property},
        QStringLiteral("v"),
        timeoutMs,
        cancellableStartup);
    const auto value =
        qvariant_cast<QDBusVariant>(reply.arguments().first()).variant();
    if (value.metaType() != QMetaType::fromType<quint32>()) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("invalid-portal-property"),
            QStringLiteral("%1.%2 is not uint32")
                .arg(interface, property));
    }
    return value.toUInt();
}

struct PortalStream {
    quint32 id = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool contains(const double px, const double py) const
    {
        return width > 0 && height > 0 && px >= x && py >= y
            && px < x + width && py < y + height;
    }
};

std::optional<QPair<int, int>> intPair(QVariant value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        value = qvariant_cast<QDBusVariant>(value).variant();
    }
    if (value.metaType() != QMetaType::fromType<QDBusArgument>()) {
        return std::nullopt;
    }
    const auto argument = qvariant_cast<QDBusArgument>(value);
    argument.beginStructure();
    int first = 0;
    int second = 0;
    argument >> first >> second;
    argument.endStructure();
    return QPair<int, int>(first, second);
}

QList<PortalStream> portalStreams(QVariant value)
{
    QList<PortalStream> result;
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        value = qvariant_cast<QDBusVariant>(value).variant();
    }
    if (value.metaType() != QMetaType::fromType<QDBusArgument>()) {
        return result;
    }
    const auto argument = qvariant_cast<QDBusArgument>(value);
    argument.beginArray();
    while (!argument.atEnd() && result.size() < 32) {
        PortalStream stream;
        QVariantMap properties;
        argument.beginStructure();
        argument >> stream.id >> properties;
        argument.endStructure();
        const auto position =
            intPair(properties.value(QStringLiteral("position")));
        auto size =
            intPair(properties.value(QStringLiteral("logical_size")));
        if (!size.has_value()) {
            size = intPair(properties.value(QStringLiteral("size")));
        }
        if (position.has_value() && size.has_value()) {
            stream.x = position->first;
            stream.y = position->second;
            stream.width = size->first;
            stream.height = size->second;
        }
        result.append(stream);
    }
    argument.endArray();
    return result;
}

class AtSpiInputAdapter final : public InputAdapter
{
public:
    AtSpiInputAdapter()
        : m_sessionBus(QDBusConnection::sessionBus())
        , m_bus(QDBusConnection::sessionBus())
    {
        const auto reply = checkedCall(
            m_sessionBus,
            QString::fromLatin1(AccessibilityBusService),
            QString::fromLatin1(AccessibilityBusPath),
            QString::fromLatin1(AccessibilityBusInterface),
            QStringLiteral("GetAddress"),
            {},
            QStringLiteral("s"));
        m_connectionName =
            QStringLiteral("kodosi-ui-input-atspi-%1")
                .arg(QCoreApplication::applicationPid());
        m_bus = QDBusConnection::connectToBus(
            reply.arguments().first().toString(),
            m_connectionName);
        if (!m_bus.isConnected()) {
            fail(
                ExitCode::Environment,
                QStringLiteral("accessibility-bus-unavailable"),
                QStringLiteral("Could not connect to the AT-SPI bus"));
        }
        const auto interface = m_bus.interface();
        const auto owner =
            interface == nullptr
            ? QDBusReply<QString>()
            : interface->serviceOwner(
                  QString::fromLatin1(RegistryService));
        if (!owner.isValid() || owner.value().isEmpty()) {
            fail(
                ExitCode::Environment,
                QStringLiteral("atspi-registry-unowned"),
                QStringLiteral(
                    "The AT-SPI Registry has no owner on the "
                    "accessibility bus"));
        }
        m_registryOwner = owner.value();
        const auto introspection = checkedCall(
            m_bus,
            QString::fromLatin1(RegistryService),
            QString::fromLatin1(DeviceControllerPath),
            QString::fromLatin1(IntrospectableInterface),
            QStringLiteral("Introspect"),
            {},
            QStringLiteral("s"));
        const auto xml = introspection.arguments().first().toString();
        if (!xml.contains(
                QStringLiteral(
                    "interface name=\"org.a11y.atspi."
                    "DeviceEventController\""))
            || !xml.contains(
                QStringLiteral(
                    "method name=\"GenerateKeyboardEvent\""))
            || !xml.contains(
                QStringLiteral(
                    "method name=\"GenerateMouseEvent\""))) {
            fail(
                ExitCode::Unsupported,
                QStringLiteral(
                    "atspi-controller-methods-unavailable"),
                QStringLiteral(
                    "The AT-SPI DeviceEventController does not expose "
                    "GenerateKeyboardEvent and GenerateMouseEvent"));
        }
    }

    ~AtSpiInputAdapter() override
    {
        QDBusConnection::disconnectFromBus(m_connectionName);
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("atspi-device-event-controller");
    }

    void send(const InputEvent& event) override
    {
        if (event.kind == InputEventKind::Key) {
            quint32 synthType = event.pressed ? 3U : 1U;
            auto value = event.code;
            if (event.code == 0xffe1) {
                value = 1;
                synthType = event.pressed ? 5U : 6U;
            } else if (event.code == 0xffe3) {
                value = 4;
                synthType = event.pressed ? 5U : 6U;
            } else if (event.code == 0xffe9) {
                value = 8;
                synthType = event.pressed ? 5U : 6U;
            } else if (event.code == 0xffeb) {
                value = 64;
                synthType = event.pressed ? 5U : 6U;
            } else if (!event.pressed) {
                return;
            }
            checkedCall(
                m_bus,
                QString::fromLatin1(RegistryService),
                QString::fromLatin1(DeviceControllerPath),
                QString::fromLatin1(DeviceControllerInterface),
                QStringLiteral("GenerateKeyboardEvent"),
                {value,
                 QString(),
                 QVariant::fromValue<quint32>(synthType)},
                QString());
            return;
        }
        QString name;
        int x = static_cast<int>(std::lround(event.x));
        int y = static_cast<int>(std::lround(event.y));
        if (event.kind == InputEventKind::PointerAbsolute) {
            name = QStringLiteral("abs");
        } else if (event.kind == InputEventKind::PointerRelative) {
            name = QStringLiteral("rel");
        } else {
            const auto button =
                event.code == PortalButtonLeft
                ? 1
                : event.code == PortalButtonMiddle ? 2 : 3;
            name = QStringLiteral("b%1%2")
                       .arg(button)
                       .arg(event.pressed ? QLatin1Char('p') : QLatin1Char('r'));
            x = -1;
            y = -1;
        }
        checkedCall(
            m_bus,
            QString::fromLatin1(RegistryService),
            QString::fromLatin1(DeviceControllerPath),
            QString::fromLatin1(DeviceControllerInterface),
            QStringLiteral("GenerateMouseEvent"),
            {x, y, name},
            QString());
    }

    [[nodiscard]] QJsonObject status() const override
    {
        return {
            {QStringLiteral("adapter"), name()},
            {QStringLiteral("available"), m_bus.isConnected()},
            {QStringLiteral("registryOwner"), m_registryOwner},
            {QStringLiteral("interface"),
             QString::fromLatin1(DeviceControllerInterface)},
            {QStringLiteral("keyboardMethodAvailable"), true},
            {QStringLiteral("mouseMethodAvailable"), true},
            {QStringLiteral("heldNonModifierKeys"), false},
        };
    }

private:
    QDBusConnection m_sessionBus;
    QDBusConnection m_bus;
    QString m_connectionName;
    QString m_registryOwner;
};

class PortalInputAdapter final : public InputAdapter
{
public:
    PortalInputAdapter(QString session, QList<PortalStream> streams)
        : m_session(std::move(session))
        , m_streams(std::move(streams))
    {
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("xdg-desktop-portal-remote-desktop");
    }

    void send(const InputEvent& event) override
    {
        QString method;
        QVariantList arguments{
            QVariant::fromValue(QDBusObjectPath(m_session)),
            QVariantMap {},
        };
        if (event.kind == InputEventKind::Key) {
            method = QStringLiteral("NotifyKeyboardKeysym");
            arguments.append(event.code);
            arguments.append(
                QVariant::fromValue<quint32>(event.pressed ? 1U : 0U));
        } else if (event.kind == InputEventKind::PointerRelative) {
            method = QStringLiteral("NotifyPointerMotion");
            arguments.append(event.x);
            arguments.append(event.y);
        } else if (event.kind == InputEventKind::PointerAbsolute) {
            const auto stream = std::ranges::find_if(
                m_streams,
                [&](const PortalStream& candidate) {
                    return candidate.contains(event.x, event.y);
                });
            if (stream == m_streams.cend()) {
                fail(
                    ExitCode::Unsupported,
                    QStringLiteral("portal-absolute-pointer-unavailable"),
                    QStringLiteral(
                        "No portal stream geometry contains the requested "
                        "screen coordinate; use relative pointer motion"));
            }
            method = QStringLiteral("NotifyPointerMotionAbsolute");
            arguments.append(QVariant::fromValue<quint32>(stream->id));
            arguments.append(event.x - stream->x);
            arguments.append(event.y - stream->y);
        } else {
            method = QStringLiteral("NotifyPointerButton");
            arguments.append(event.code);
            arguments.append(
                QVariant::fromValue<quint32>(event.pressed ? 1U : 0U));
        }
        checkedCall(
            QDBusConnection::sessionBus(),
            QString::fromLatin1(PortalService),
            QString::fromLatin1(PortalPath),
            QString::fromLatin1(RemoteDesktopInterface),
            method,
            arguments,
            QString());
    }

    [[nodiscard]] QJsonObject status() const override
    {
        const auto absolutePointer =
            std::ranges::any_of(
                m_streams,
                [](const PortalStream& stream) {
                    return stream.width > 0 && stream.height > 0;
                });
        QJsonObject result{
            {QStringLiteral("adapter"), name()},
            {QStringLiteral("available"), true},
            {QStringLiteral("session"), QStringLiteral("active")},
            {QStringLiteral("absolutePointer"),
             absolutePointer},
            {QStringLiteral("absolutePointerCapability"),
             absolutePointer
                 ? QStringLiteral("available")
                 : m_streams.isEmpty()
                     ? QStringLiteral("unavailable-no-stream")
                     : QStringLiteral(
                           "unavailable-no-stream-geometry")},
            {QStringLiteral("streamCount"), m_streams.size()},
        };
        return result;
    }

private:
    QString m_session;
    QList<PortalStream> m_streams;
};

void executeEvents(
    InputAdapter* adapter,
    const QList<InputEvent>& events,
    const QList<InputEvent>& cleanup,
    const std::function<bool()>& interrupted = {})
{
    try {
        for (qsizetype index = 0; index < events.size(); ++index) {
            const auto& event = events.at(index);
            if (interrupted && interrupted()) {
                fail(
                    ExitCode::Environment,
                    QStringLiteral("sidecar-interrupted"),
                    QStringLiteral(
                        "Input execution was interrupted by sidecar "
                        "shutdown"));
            }
            adapter->send(event);
            const auto delay =
                std::max(
                    event.delayMs,
                    1000 / InputProtocol::MaximumEventsPerSecond);
            if (!interrupted || index + 1 == events.size()) {
                QThread::msleep(
                    static_cast<unsigned long>(delay));
                continue;
            }
            QDeadlineTimer delayDeadline(
                delay,
                Qt::PreciseTimer);
            while (!delayDeadline.hasExpired()) {
                QCoreApplication::processEvents(
                    QEventLoop::AllEvents,
                    static_cast<int>(
                        std::min<qint64>(
                            std::max<qint64>(
                                delayDeadline.remainingTime(),
                                1),
                            10)));
                if (interrupted()) {
                    fail(
                        ExitCode::Environment,
                        QStringLiteral("sidecar-interrupted"),
                        QStringLiteral(
                            "Input execution was interrupted by sidecar "
                            "shutdown"));
                }
                const auto remaining =
                    delayDeadline.remainingTime();
                if (remaining > 0) {
                    QThread::msleep(
                        static_cast<unsigned long>(
                            std::min<qint64>(remaining, 5)));
                }
            }
        }
    } catch (...) {
        for (const auto& event : cleanup) {
            try {
                adapter->send(event);
            } catch (...) {
            }
        }
        throw;
    }
}

QJsonObject sidecarRequestWithState(
    const QJsonObject& request,
    const QJsonObject& state,
    const int timeoutMs)
{
    const auto socketPath = state.value(QStringLiteral("socket")).toString();
    const QFileInfo socketInfo(socketPath);
    if (!socketInfo.exists() || !privateOwnerFile(socketInfo)) {
        fail(
            ExitCode::Environment,
            QStringLiteral("sidecar-socket-unavailable"),
            QStringLiteral("The input sidecar socket is missing or unsafe"));
    }
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(timeoutMs)) {
        fail(
            ExitCode::Environment,
            QStringLiteral("sidecar-connect-failed"),
            QStringLiteral("Could not connect to the input sidecar"));
    }
    auto authenticated = request;
    authenticated.insert(
        QStringLiteral("nonce"),
        state.value(QStringLiteral("nonce")).toString());
    authenticated.insert(
        QStringLiteral("expectedPid"),
        state.value(QStringLiteral("pid")));
    authenticated.insert(
        QStringLiteral("expectedProcessStartTime"),
        state.value(QStringLiteral("processStartTime")));
    authenticated.insert(
        QStringLiteral("expectedStartupToken"),
        state.value(QStringLiteral("startupToken")));
    const auto bytes = InputProtocol::frame(authenticated);
    if (socket.write(bytes) != bytes.size()
        || !socket.waitForBytesWritten(timeoutMs)) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-write-failed"),
            QStringLiteral("Could not send the input request"));
    }
    QByteArray buffer;
    QDeadlineTimer deadline(timeoutMs, Qt::PreciseTimer);
    while (true) {
        const auto chunk = socket.readAll();
        if (buffer.size() + chunk.size()
            > InputProtocol::MaximumMessageBytes + 4) {
            fail(
                ExitCode::LimitExceeded,
                QStringLiteral("sidecar-response-limit"),
                QStringLiteral("Input sidecar response exceeds 64 KiB"));
        }
        buffer.append(chunk);
        if (auto response = InputProtocol::takeFrame(&buffer);
            response.has_value()) {
            const auto error = response->value(QStringLiteral("error")).toObject();
            if (!response->value(QStringLiteral("ok")).toBool()) {
                fail(
                    static_cast<ExitCode>(
                        error.value(QStringLiteral("exitCode")).toInt(
                            static_cast<int>(ExitCode::RemoteError))),
                    error.value(QStringLiteral("kind")).toString(),
                    error.value(QStringLiteral("message")).toString());
            }
            return *response;
        }
        const auto remaining = deadline.remainingTime();
        if (remaining <= 0 || !socket.waitForReadyRead(
                                  static_cast<int>(remaining))) {
            fail(
                ExitCode::Timeout,
                QStringLiteral("sidecar-timeout"),
                QStringLiteral("Timed out waiting for the input sidecar"));
        }
    }
}

QJsonObject sidecarRequest(
    const QJsonObject& request,
    const int timeoutMs)
{
    return sidecarRequestWithState(
        request,
        readState(true),
        timeoutMs);
}

QJsonArray eventsJson(const QList<InputEvent>& events)
{
    QJsonArray result;
    for (const auto& event : events) {
        result.append(InputProtocol::eventJson(event));
    }
    return result;
}

QList<InputEvent> eventsFromJson(const QJsonArray& values)
{
    if (values.size() > InputProtocol::MaximumEventsPerRequest) {
        fail(
            ExitCode::LimitExceeded,
            QStringLiteral("input-event-limit"),
            QStringLiteral("The input request contains too many events"));
    }
    QList<InputEvent> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        if (!value.isObject()) {
            fail(
                ExitCode::Usage,
                QStringLiteral("invalid-input-event"),
                QStringLiteral("Every input event must be an object"));
        }
        result.append(InputProtocol::eventFromJson(value.toObject()));
    }
    return result;
}

bool sameUidPeer(const QLocalSocket* socket)
{
    struct ucred credential {};
    socklen_t length = sizeof(credential);
    const auto descriptor = socket->socketDescriptor();
    return descriptor >= 0
        && ::getsockopt(
               static_cast<int>(descriptor),
               SOL_SOCKET,
               SO_PEERCRED,
               &credential,
               &length)
            == 0
        && length == sizeof(credential)
        && InputProtocol::peerUidAllowed(
            static_cast<quint32>(::getuid()),
            static_cast<quint32>(credential.uid));
}

class PortalSession final : public QObject
{
    Q_OBJECT

public:
    explicit PortalSession(
        const qint64 absoluteDeadlineMs,
        QObject* parent = nullptr)
        : QObject(parent)
        , m_absoluteDeadlineMs(absoluteDeadlineMs)
    {
    }

    ~PortalSession() override
    {
        close();
    }

    [[nodiscard]] std::unique_ptr<PortalInputAdapter> open()
    {
        auto bus = QDBusConnection::sessionBus();
        const auto interface = bus.interface();
        if (interface == nullptr
            || !interface
                    ->isServiceRegistered(
                        QString::fromLatin1(PortalService))
                    .value()) {
            fail(
                ExitCode::Environment,
                QStringLiteral("portal-unavailable"),
                QStringLiteral("xdg-desktop-portal is not registered"));
        }
        m_version = portalProperty(
            QString::fromLatin1(RemoteDesktopInterface),
            QStringLiteral("version"),
            remainingMilliseconds(
                m_absoluteDeadlineMs,
                AtSpiProbe::DefaultCallTimeoutMs),
            true);
        m_deviceTypes = portalProperty(
            QString::fromLatin1(RemoteDesktopInterface),
            QStringLiteral("AvailableDeviceTypes"),
            remainingMilliseconds(
                m_absoluteDeadlineMs,
                AtSpiProbe::DefaultCallTimeoutMs),
            true);
        if ((m_deviceTypes & (KeyboardDevice | PointerDevice))
            != (KeyboardDevice | PointerDevice)) {
            fail(
                ExitCode::Unsupported,
                QStringLiteral("portal-input-devices-unavailable"),
                QStringLiteral(
                    "RemoteDesktop does not expose both keyboard and pointer"));
        }
        m_screenCastVersion = portalProperty(
            QString::fromLatin1(ScreenCastInterface),
            QStringLiteral("version"),
            remainingMilliseconds(
                m_absoluteDeadlineMs,
                AtSpiProbe::DefaultCallTimeoutMs),
            true);
        m_sourceTypes = portalProperty(
            QString::fromLatin1(ScreenCastInterface),
            QStringLiteral("AvailableSourceTypes"),
            remainingMilliseconds(
                m_absoluteDeadlineMs,
                AtSpiProbe::DefaultCallTimeoutMs),
            true);
        const auto requestedSources =
            m_sourceTypes & (MonitorSource | WindowSource);
        if (requestedSources == 0U) {
            fail(
                ExitCode::Unsupported,
                QStringLiteral("portal-screen-cast-sources-unavailable"),
                QStringLiteral(
                    "ScreenCast does not expose a monitor or window source"));
        }

        QVariantMap createOptions;
        createOptions.insert(
            QStringLiteral("session_handle_token"),
            objectToken(QStringLiteral("kodosi_input_session_")));
        const auto created = portalRequest(
            QString::fromLatin1(RemoteDesktopInterface),
            QStringLiteral("CreateSession"),
            {createOptions},
            m_absoluteDeadlineMs);
        auto sessionHandle =
            created.value(QStringLiteral("session_handle"));
        if (sessionHandle.metaType()
            == QMetaType::fromType<QDBusVariant>()) {
            sessionHandle =
                qvariant_cast<QDBusVariant>(sessionHandle).variant();
        }
        if (sessionHandle.metaType()
            == QMetaType::fromType<QDBusObjectPath>()) {
            m_session =
                qvariant_cast<QDBusObjectPath>(sessionHandle).path();
        }
        if (!m_session.startsWith(
                QStringLiteral("/org/freedesktop/portal/desktop/session/"))) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("invalid-portal-session"),
                QStringLiteral("RemoteDesktop returned an invalid session"));
        }
        if (!bus.connect(
                QString::fromLatin1(PortalService),
                m_session,
                QString::fromLatin1(SessionInterface),
                QStringLiteral("Closed"),
                this,
                SLOT(Closed()))) {
            close();
            fail(
                ExitCode::RemoteError,
                QStringLiteral("portal-session-subscribe-failed"),
                QStringLiteral("Could not monitor portal session closure"));
        }
        ensureOpen();

        QString restoreToken;
        QFile tokenFile(restoreTokenPath());
        if (tokenFile.open(QIODevice::ReadOnly)
            && tokenFile.size() > 0 && tokenFile.size() <= 4096
            && privateOwnerFile(QFileInfo(tokenFile))) {
            restoreToken = QString::fromUtf8(tokenFile.readAll());
            tokenFile.close();
            QFile::remove(restoreTokenPath());
        }

        QVariantMap sourceOptions;
        sourceOptions.insert(
            QStringLiteral("types"),
            QVariant::fromValue<quint32>(requestedSources));
        sourceOptions.insert(QStringLiteral("multiple"), false);
        if (m_screenCastVersion >= 4U) {
            sourceOptions.insert(
                QStringLiteral("persist_mode"),
                QVariant::fromValue<quint32>(2U));
            if (!restoreToken.isEmpty()) {
                sourceOptions.insert(
                    QStringLiteral("restore_token"),
                    restoreToken);
            }
        }
        static_cast<void>(portalRequest(
            QString::fromLatin1(ScreenCastInterface),
            QStringLiteral("SelectSources"),
            {QVariant::fromValue(QDBusObjectPath(m_session)), sourceOptions},
            m_absoluteDeadlineMs));
        ensureOpen();

        QVariantMap selectOptions;
        selectOptions.insert(
            QStringLiteral("types"),
            QVariant::fromValue<quint32>(KeyboardDevice | PointerDevice));
        if (m_version >= 2U) {
            selectOptions.insert(
                QStringLiteral("persist_mode"),
                QVariant::fromValue<quint32>(2U));
            if (!restoreToken.isEmpty()) {
                selectOptions.insert(
                    QStringLiteral("restore_token"),
                    restoreToken);
            }
        }
        static_cast<void>(portalRequest(
            QString::fromLatin1(RemoteDesktopInterface),
            QStringLiteral("SelectDevices"),
            {QVariant::fromValue(QDBusObjectPath(m_session)), selectOptions},
            m_absoluteDeadlineMs));
        ensureOpen();
        const auto started = portalRequest(
            QString::fromLatin1(RemoteDesktopInterface),
            QStringLiteral("Start"),
            {QVariant::fromValue(QDBusObjectPath(m_session)),
             QString(),
             QVariantMap {}},
            m_absoluteDeadlineMs);
        ensureOpen();
        const auto devices =
            started.value(QStringLiteral("devices")).toUInt();
        if ((devices & (KeyboardDevice | PointerDevice))
            != (KeyboardDevice | PointerDevice)) {
            fail(
                ExitCode::PortalDenied,
                QStringLiteral("portal-devices-not-granted"),
                QStringLiteral(
                    "RemoteDesktop did not grant keyboard and pointer"));
        }
        const auto restore =
            started.value(QStringLiteral("restore_token")).toString();
        if (!restore.isEmpty() && restore.size() <= 4096) {
            QSaveFile tokenFile(restoreTokenPath());
            if (tokenFile.open(QIODevice::WriteOnly)
                && tokenFile.setPermissions(
                    QFileDevice::ReadOwner | QFileDevice::WriteOwner)
                && tokenFile.write(restore.toUtf8())
                    == restore.toUtf8().size()) {
                static_cast<void>(tokenFile.commit());
            }
        }
        m_streams =
            portalStreams(started.value(QStringLiteral("streams")));
        return std::make_unique<PortalInputAdapter>(m_session, m_streams);
    }

    [[nodiscard]] quint32 version() const
    {
        return m_version;
    }

    [[nodiscard]] quint32 deviceTypes() const
    {
        return m_deviceTypes;
    }

    [[nodiscard]] quint32 sourceTypes() const
    {
        return m_sourceTypes;
    }

    [[nodiscard]] bool closed() const
    {
        return m_closed;
    }

    void close()
    {
        if (m_session.isEmpty() || m_closed) {
            return;
        }
        auto message = QDBusMessage::createMethodCall(
            QString::fromLatin1(PortalService),
            m_session,
            QString::fromLatin1(SessionInterface),
            QStringLiteral("Close"));
        static_cast<void>(
            QDBusConnection::sessionBus().call(message, QDBus::NoBlock));
        m_closed = true;
    }

signals:
    void sessionClosed();

public slots:
    void Closed()
    {
        m_closed = true;
        emit sessionClosed();
    }

private:
    void ensureOpen() const
    {
        if (m_closed) {
            fail(
                ExitCode::PortalDenied,
                QStringLiteral("portal-session-closed"),
                QStringLiteral(
                    "The portal closed the RemoteDesktop session "
                    "during setup"));
        }
    }

    QString m_session;
    QList<PortalStream> m_streams;
    qint64 m_absoluteDeadlineMs;
    quint32 m_version = 0;
    quint32 m_deviceTypes = 0;
    quint32 m_screenCastVersion = 0;
    quint32 m_sourceTypes = 0;
    bool m_closed = false;
};

}

int InputProtocol::keySym(const QString& name)
{
    const auto canonical = canonicalKeyName(name);
    bool numeric = false;
    const auto parsed =
        canonical.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
        ? canonical.sliced(2).toUInt(&numeric, 16)
        : canonical.toUInt(&numeric, 10);
    if (numeric && parsed > 0U && parsed <= 0x0110ffffU) {
        return static_cast<int>(parsed);
    }
    static const QHash<QString, int> keys{
        {QStringLiteral("Backspace"), 0xff08},
        {QStringLiteral("Tab"), 0xff09},
        {QStringLiteral("Return"), 0xff0d},
        {QStringLiteral("Escape"), 0xff1b},
        {QStringLiteral("Home"), 0xff50},
        {QStringLiteral("Left"), 0xff51},
        {QStringLiteral("Up"), 0xff52},
        {QStringLiteral("Right"), 0xff53},
        {QStringLiteral("Down"), 0xff54},
        {QStringLiteral("PageUp"), 0xff55},
        {QStringLiteral("PageDown"), 0xff56},
        {QStringLiteral("End"), 0xff57},
        {QStringLiteral("Insert"), 0xff63},
        {QStringLiteral("Delete"), 0xffff},
        {QStringLiteral("Shift"), 0xffe1},
        {QStringLiteral("Ctrl"), 0xffe3},
        {QStringLiteral("Alt"), 0xffe9},
        {QStringLiteral("Meta"), 0xffeb},
        {QStringLiteral("Space"), 0x20},
        {QStringLiteral("F1"), 0xffbe},
        {QStringLiteral("F2"), 0xffbf},
        {QStringLiteral("F3"), 0xffc0},
        {QStringLiteral("F4"), 0xffc1},
        {QStringLiteral("F5"), 0xffc2},
        {QStringLiteral("F6"), 0xffc3},
        {QStringLiteral("F7"), 0xffc4},
        {QStringLiteral("F8"), 0xffc5},
        {QStringLiteral("F9"), 0xffc6},
        {QStringLiteral("F10"), 0xffc7},
        {QStringLiteral("F11"), 0xffc8},
        {QStringLiteral("F12"), 0xffc9},
    };
    if (keys.contains(canonical)) {
        return keys.value(canonical);
    }
    const auto scalars = canonical.toUcs4();
    if (scalars.size() == 1) {
        const auto scalar = scalars.first();
        return scalar <= 0xff ? scalar : 0x01000000 | scalar;
    }
    fail(
        ExitCode::Usage,
        QStringLiteral("invalid-key"),
        QStringLiteral("Unknown keysym or key name '%1'").arg(name));
}

QString InputProtocol::canonicalKeyName(const QString& name)
{
    if (name.size() > 64 || name.isEmpty()) {
        fail(
            ExitCode::Usage,
            QStringLiteral("invalid-key"),
            QStringLiteral("--key must contain one bounded key name"));
    }
    const auto folded = name.trimmed().toCaseFolded();
    static const QHash<QString, QString> aliases{
        {QStringLiteral("control"), QStringLiteral("Ctrl")},
        {QStringLiteral("ctrl"), QStringLiteral("Ctrl")},
        {QStringLiteral("shift"), QStringLiteral("Shift")},
        {QStringLiteral("alt"), QStringLiteral("Alt")},
        {QStringLiteral("meta"), QStringLiteral("Meta")},
        {QStringLiteral("super"), QStringLiteral("Meta")},
        {QStringLiteral("enter"), QStringLiteral("Return")},
        {QStringLiteral("return"), QStringLiteral("Return")},
        {QStringLiteral("esc"), QStringLiteral("Escape")},
        {QStringLiteral("escape"), QStringLiteral("Escape")},
        {QStringLiteral("backspace"), QStringLiteral("Backspace")},
        {QStringLiteral("tab"), QStringLiteral("Tab")},
        {QStringLiteral("space"), QStringLiteral("Space")},
        {QStringLiteral("left"), QStringLiteral("Left")},
        {QStringLiteral("right"), QStringLiteral("Right")},
        {QStringLiteral("up"), QStringLiteral("Up")},
        {QStringLiteral("down"), QStringLiteral("Down")},
        {QStringLiteral("home"), QStringLiteral("Home")},
        {QStringLiteral("end"), QStringLiteral("End")},
        {QStringLiteral("insert"), QStringLiteral("Insert")},
        {QStringLiteral("delete"), QStringLiteral("Delete")},
        {QStringLiteral("pageup"), QStringLiteral("PageUp")},
        {QStringLiteral("pagedown"), QStringLiteral("PageDown")},
    };
    if (aliases.contains(folded)) {
        return aliases.value(folded);
    }
    if (folded.size() >= 2 && folded.at(0) == QLatin1Char('f')) {
        bool valid = false;
        const auto number = folded.sliced(1).toInt(&valid);
        if (valid && number >= 1 && number <= 12) {
            return QStringLiteral("F%1").arg(number);
        }
    }
    if (name.toUcs4().size() == 1) {
        return name;
    }
    return name.trimmed();
}

int InputProtocol::buttonCode(const QString& name)
{
    const auto folded = name.trimmed().toCaseFolded();
    if (folded == QStringLiteral("left")) {
        return PortalButtonLeft;
    }
    if (folded == QStringLiteral("middle")) {
        return PortalButtonMiddle;
    }
    if (folded == QStringLiteral("right")) {
        return PortalButtonRight;
    }
    fail(
        ExitCode::Usage,
        QStringLiteral("invalid-button"),
        QStringLiteral("--button must be left, middle, or right"));
}

QJsonObject InputProtocol::eventJson(const InputEvent& event)
{
    QJsonObject result;
    switch (event.kind) {
    case InputEventKind::Key:
        result = {
            {QStringLiteral("type"), QStringLiteral("key")},
            {QStringLiteral("keysym"), event.code},
            {QStringLiteral("state"),
             event.pressed ? QStringLiteral("down")
                           : QStringLiteral("up")},
        };
        break;
    case InputEventKind::PointerAbsolute:
        result = {
            {QStringLiteral("type"), QStringLiteral("pointer-absolute")},
            {QStringLiteral("x"), event.x},
            {QStringLiteral("y"), event.y},
        };
        break;
    case InputEventKind::PointerRelative:
        result = {
            {QStringLiteral("type"), QStringLiteral("pointer-relative")},
            {QStringLiteral("x"), event.x},
            {QStringLiteral("y"), event.y},
        };
        break;
    case InputEventKind::Button:
        result = {
            {QStringLiteral("type"), QStringLiteral("button")},
            {QStringLiteral("button"), event.code},
            {QStringLiteral("state"),
             event.pressed ? QStringLiteral("down")
                           : QStringLiteral("up")},
        };
        break;
    }
    if (event.delayMs > 0) {
        result.insert(QStringLiteral("delayMs"), event.delayMs);
    }
    return result;
}

InputEvent InputProtocol::eventFromJson(const QJsonObject& object)
{
    const auto type = object.value(QStringLiteral("type")).toString();
    InputEvent event;
    if (type == QStringLiteral("key")) {
        event.kind = InputEventKind::Key;
        event.code = object.value(QStringLiteral("keysym")).toInt();
        if (event.code <= 0 || event.code > 0x0110ffff) {
            fail(
                ExitCode::Usage,
                QStringLiteral("invalid-keysym"),
                QStringLiteral("Keysym is outside the supported range"));
        }
    } else if (type == QStringLiteral("button")) {
        event.kind = InputEventKind::Button;
        event.code = object.value(QStringLiteral("button")).toInt();
        if (event.code != PortalButtonLeft
            && event.code != PortalButtonMiddle
            && event.code != PortalButtonRight) {
            fail(
                ExitCode::Usage,
                QStringLiteral("invalid-button"),
                QStringLiteral("Button code is unsupported"));
        }
    } else if (type == QStringLiteral("pointer-absolute")
               || type == QStringLiteral("pointer-relative")) {
        event.kind =
            type == QStringLiteral("pointer-absolute")
            ? InputEventKind::PointerAbsolute
            : InputEventKind::PointerRelative;
        event.x = object.value(QStringLiteral("x")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        event.y = object.value(QStringLiteral("y")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(event.x) || !std::isfinite(event.y)
            || std::abs(event.x) > MaximumCoordinate
            || std::abs(event.y) > MaximumCoordinate) {
            fail(
                ExitCode::Usage,
                QStringLiteral("invalid-coordinate"),
                QStringLiteral("Pointer coordinates are outside safe bounds"));
        }
        event.delayMs =
            object.value(QStringLiteral("delayMs")).toInt();
        if (event.delayMs < 0
            || event.delayMs > MaximumEventDelayMs) {
            fail(
                ExitCode::Usage,
                QStringLiteral("invalid-event-delay"),
                QStringLiteral("Input event delay is outside safe bounds"));
        }
        return event;
    } else {
        fail(
            ExitCode::Usage,
            QStringLiteral("invalid-input-event"),
            QStringLiteral("Unknown input event type"));
    }
    const auto state = object.value(QStringLiteral("state")).toString();
    if (state != QStringLiteral("down") && state != QStringLiteral("up")) {
        fail(
            ExitCode::Usage,
            QStringLiteral("invalid-input-state"),
            QStringLiteral("Input state must be down or up"));
    }
    event.pressed = state == QStringLiteral("down");
    event.delayMs =
        object.value(QStringLiteral("delayMs")).toInt();
    if (event.delayMs < 0 || event.delayMs > MaximumEventDelayMs) {
        fail(
            ExitCode::Usage,
            QStringLiteral("invalid-event-delay"),
            QStringLiteral("Input event delay is outside safe bounds"));
    }
    return event;
}

QByteArray InputProtocol::frame(const QJsonObject& object)
{
    const auto payload =
        QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > MaximumMessageBytes) {
        fail(
            ExitCode::LimitExceeded,
            QStringLiteral("input-message-limit"),
            QStringLiteral("Input sidecar message exceeds 64 KiB"));
    }
    QByteArray result(4, '\0');
    const auto size = static_cast<quint32>(payload.size());
    result[0] = static_cast<char>((size >> 24U) & 0xffU);
    result[1] = static_cast<char>((size >> 16U) & 0xffU);
    result[2] = static_cast<char>((size >> 8U) & 0xffU);
    result[3] = static_cast<char>(size & 0xffU);
    result.append(payload);
    return result;
}

std::optional<QJsonObject> InputProtocol::takeFrame(QByteArray* data)
{
    if (data->size() < 4) {
        return std::nullopt;
    }
    const auto size =
        (static_cast<quint32>(
             static_cast<unsigned char>(data->at(0)))
         << 24U)
        | (static_cast<quint32>(
               static_cast<unsigned char>(data->at(1)))
           << 16U)
        | (static_cast<quint32>(
               static_cast<unsigned char>(data->at(2)))
           << 8U)
        | static_cast<quint32>(
            static_cast<unsigned char>(data->at(3)));
    if (size == 0U
        || size > static_cast<quint32>(MaximumMessageBytes)) {
        fail(
            ExitCode::LimitExceeded,
            QStringLiteral("input-frame-limit"),
            QStringLiteral("Input sidecar frame length is invalid"));
    }
    if (data->size() < 4 + static_cast<qsizetype>(size)) {
        return std::nullopt;
    }
    const auto payload = data->sliced(4, size);
    data->remove(0, 4 + size);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(
            ExitCode::Usage,
            QStringLiteral("invalid-input-json"),
            QStringLiteral("Input sidecar frame is not a JSON object"));
    }
    return document.object();
}

bool InputProtocol::peerUidAllowed(
    const quint32 owner,
    const quint32 peer)
{
    return owner == peer;
}

InputPlan keyPlan(const QString& key, const bool down, const bool up)
{
    const auto canonical = InputProtocol::canonicalKeyName(key);
    const auto code = InputProtocol::keySym(canonical);
    InputPlan plan;
    plan.command = QStringLiteral("key");
    plan.details = {
        {QStringLiteral("key"), canonical},
        {QStringLiteral("keysym"), code},
        {QStringLiteral("state"),
         down && up
             ? QStringLiteral("press")
             : down ? QStringLiteral("down") : QStringLiteral("up")},
    };
    if (down) {
        plan.events.append({InputEventKind::Key, code, true});
    }
    if (up) {
        plan.events.append({InputEventKind::Key, code, false});
    }
    if (down && up) {
        plan.cleanup.append({InputEventKind::Key, code, false});
    }
    return plan;
}

InputPlan shortcutPlan(const QString& keys)
{
    const auto parts = keys.split(QLatin1Char('+'), Qt::KeepEmptyParts);
    if (parts.size() < 2 || parts.size() > 8
        || std::ranges::any_of(parts, [](const QString& part) {
               return part.trimmed().isEmpty();
           })) {
        fail(
            ExitCode::Usage,
            QStringLiteral("invalid-shortcut"),
            QStringLiteral(
                "--keys must be a bounded '+'-separated shortcut"));
    }
    QList<QPair<QString, int>> resolved;
    QSet<int> seen;
    for (const auto& part : parts) {
        const auto name = InputProtocol::canonicalKeyName(part);
        const auto code = InputProtocol::keySym(name);
        if (seen.contains(code)) {
            fail(
                ExitCode::Usage,
                QStringLiteral("duplicate-shortcut-key"),
                QStringLiteral("Shortcut keys must be unique"));
        }
        seen.insert(code);
        resolved.append({name, code});
    }
    const QSet<QString> modifiers{
        QStringLiteral("Ctrl"),
        QStringLiteral("Shift"),
        QStringLiteral("Alt"),
        QStringLiteral("Meta"),
    };
    for (qsizetype index = 0; index + 1 < resolved.size(); ++index) {
        if (!modifiers.contains(resolved.at(index).first)) {
            fail(
                ExitCode::Usage,
                QStringLiteral("invalid-shortcut-order"),
                QStringLiteral(
                    "Only modifiers may precede the final shortcut key"));
        }
    }
    if (modifiers.contains(resolved.last().first)) {
        fail(
            ExitCode::Usage,
            QStringLiteral("missing-shortcut-key"),
            QStringLiteral("Shortcut must end with a non-modifier key"));
    }
    if (resolved.last().second >= 'A'
        && resolved.last().second <= 'Z') {
        resolved.last().second += 'a' - 'A';
    }
    InputPlan plan;
    plan.command = QStringLiteral("shortcut");
    plan.details = {{QStringLiteral("keys"), keys}};
    for (const auto& [name, code] : resolved) {
        Q_UNUSED(name)
        plan.events.append({InputEventKind::Key, code, true});
    }
    plan.events.append(
        {InputEventKind::Key, resolved.last().second, false});
    plan.cleanup.append(
        {InputEventKind::Key, resolved.last().second, false});
    for (auto iterator = resolved.crbegin() + 1;
         iterator != resolved.crend();
         ++iterator) {
        plan.events.append(
            {InputEventKind::Key, iterator->second, false});
        plan.cleanup.append(
            {InputEventKind::Key, iterator->second, false});
    }
    return plan;
}

InputPlan pointerPlan(const double x, const double y, const bool relative)
{
    InputEvent event{
        relative ? InputEventKind::PointerRelative
                 : InputEventKind::PointerAbsolute,
        0,
        false,
        x,
        y,
    };
    static_cast<void>(InputProtocol::eventFromJson(
        InputProtocol::eventJson(event)));
    return {
        QStringLiteral("pointer"),
        {event},
        {},
        {
            {QStringLiteral("relative"), relative},
            {QStringLiteral("x"), x},
            {QStringLiteral("y"), y},
        },
    };
}

InputPlan buttonPlan(
    const QString& button,
    const bool down,
    const bool up)
{
    const auto code = InputProtocol::buttonCode(button);
    InputPlan plan;
    plan.command = QStringLiteral("button");
    plan.details = {{QStringLiteral("button"), button.toCaseFolded()}};
    if (down) {
        plan.events.append({InputEventKind::Button, code, true});
    }
    if (up) {
        plan.events.append({InputEventKind::Button, code, false});
    }
    if (down && up) {
        plan.cleanup.append({InputEventKind::Button, code, false});
    }
    return plan;
}

InputPlan dragPlan(
    const int fromX,
    const int fromY,
    const int toX,
    const int toY,
    const int durationMs)
{
    if (durationMs < 0
        || durationMs > InputProtocol::MaximumDragDurationMs) {
        fail(
            ExitCode::Usage,
            QStringLiteral("invalid-duration"),
            QStringLiteral("--duration-ms must be between 0 and %1")
                .arg(InputProtocol::MaximumDragDurationMs));
    }
    static_cast<void>(pointerPlan(fromX, fromY, false));
    static_cast<void>(pointerPlan(toX, toY, false));
    const auto steps = std::clamp(durationMs / 16, 1, 240);
    const auto delay = durationMs == 0 ? 0 : durationMs / steps;
    InputPlan plan;
    plan.command = QStringLiteral("drag");
    plan.details = {
        {QStringLiteral("fromX"), fromX},
        {QStringLiteral("fromY"), fromY},
        {QStringLiteral("toX"), toX},
        {QStringLiteral("toY"), toY},
        {QStringLiteral("durationMs"), durationMs},
    };
    plan.events.append(
        {InputEventKind::PointerAbsolute, 0, false,
         static_cast<double>(fromX), static_cast<double>(fromY)});
    plan.events.append(
        {InputEventKind::Button, PortalButtonLeft, true});
    for (int step = 1; step <= steps; ++step) {
        const auto fraction =
            static_cast<double>(step) / static_cast<double>(steps);
        plan.events.append(
            {InputEventKind::PointerAbsolute,
             0,
             false,
             fromX + (toX - fromX) * fraction,
             fromY + (toY - fromY) * fraction,
             delay});
    }
    plan.events.append(
        {InputEventKind::Button, PortalButtonLeft, false});
    plan.cleanup.append(
        {InputEventKind::Button, PortalButtonLeft, false});
    return plan;
}

QJsonObject executeInput(
    const InputPlan& plan,
    const QString& adapterPreference)
{
    if (plan.events.isEmpty()
        || plan.events.size() > InputProtocol::MaximumEventsPerRequest) {
        fail(
            ExitCode::LimitExceeded,
            QStringLiteral("input-event-limit"),
            QStringLiteral("Input request has an invalid event count"));
    }
    const auto wayland =
        qEnvironmentVariable("XDG_SESSION_TYPE")
                .compare(QStringLiteral("wayland"), Qt::CaseInsensitive)
            == 0
        || !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty();
    const auto usePortal =
        adapterPreference == QStringLiteral("portal")
        || (adapterPreference == QStringLiteral("auto") && wayland);
    if (wayland && adapterPreference == QStringLiteral("atspi")) {
        fail(
            ExitCode::Unsupported,
            QStringLiteral("wayland-atspi-input-forbidden"),
            QStringLiteral(
                "Wayland raw input must use the consent-authoritative "
                "portal sidecar"));
    }
    QJsonObject result;
    if (usePortal) {
        const auto timeout =
            std::max(
                30000,
                plan.details.value(QStringLiteral("durationMs")).toInt()
                    + 5000);
        result = sidecarRequest(
            {
                {QStringLiteral("command"), plan.command},
                {QStringLiteral("events"), eventsJson(plan.events)},
                {QStringLiteral("cleanup"), eventsJson(plan.cleanup)},
            },
            timeout);
    } else if (adapterPreference == QStringLiteral("auto")
               || adapterPreference == QStringLiteral("atspi")) {
        if (plan.command == QStringLiteral("key")
            && plan.details.value(QStringLiteral("state")).toString()
                != QStringLiteral("press")) {
            const auto code =
                plan.details.value(QStringLiteral("keysym")).toInt();
            if (code != 0xffe1 && code != 0xffe3
                && code != 0xffe9 && code != 0xffeb) {
                fail(
                    ExitCode::Unsupported,
                    QStringLiteral("atspi-key-state-unsupported"),
                    QStringLiteral(
                        "AT-SPI supports named key press/release synthesis "
                        "but not a held non-modifier keysym; use --press"));
            }
        }
        AtSpiInputAdapter adapter;
        executeEvents(&adapter, plan.events, plan.cleanup);
        result = {
            {QStringLiteral("ok"), true},
            {QStringLiteral("adapter"), adapter.name()},
            {QStringLiteral("eventCount"), plan.events.size()},
        };
    } else {
        fail(
            ExitCode::Usage,
            QStringLiteral("invalid-adapter"),
            QStringLiteral("--adapter must be auto, atspi, or portal"));
    }
    result.insert(QStringLiteral("command"), plan.command);
    result.insert(QStringLiteral("details"), plan.details);
    result.insert(QStringLiteral("events"), eventsJson(plan.events));
    return result;
}

QJsonObject inputStatus()
{
    QJsonObject atspi{
        {QStringLiteral("available"), false},
        {QStringLiteral("interface"),
         QString::fromLatin1(DeviceControllerInterface)},
    };
    try {
        AtSpiInputAdapter adapter;
        atspi = adapter.status();
    } catch (const ProbeError& error) {
        atspi.insert(QStringLiteral("errorKind"), error.kind());
        atspi.insert(QStringLiteral("error"), error.message());
    }

    QJsonObject portal{
        {QStringLiteral("registered"), false},
        {QStringLiteral("available"), false},
    };
    const auto bus = QDBusConnection::sessionBus();
    const auto busInterface = bus.interface();
    const auto registered =
        busInterface != nullptr
        && busInterface
               ->isServiceRegistered(QString::fromLatin1(PortalService))
               .value();
    portal.insert(QStringLiteral("registered"), registered);
    if (registered) {
        try {
            const auto version = portalProperty(
                QString::fromLatin1(RemoteDesktopInterface),
                QStringLiteral("version"));
            const auto devices = portalProperty(
                QString::fromLatin1(RemoteDesktopInterface),
                QStringLiteral("AvailableDeviceTypes"));
            const auto screenCastVersion = portalProperty(
                QString::fromLatin1(ScreenCastInterface),
                QStringLiteral("version"));
            const auto sources = portalProperty(
                QString::fromLatin1(ScreenCastInterface),
                QStringLiteral("AvailableSourceTypes"));
            portal.insert(
                QStringLiteral("version"),
                static_cast<qint64>(version));
            portal.insert(
                QStringLiteral("availableDeviceTypes"),
                static_cast<qint64>(devices));
            portal.insert(
                QStringLiteral("screenCastVersion"),
                static_cast<qint64>(screenCastVersion));
            portal.insert(
                QStringLiteral("availableSourceTypes"),
                static_cast<qint64>(sources));
            portal.insert(
                QStringLiteral("keyboardAvailable"),
                (devices & KeyboardDevice) != 0U);
            portal.insert(
                QStringLiteral("pointerAvailable"),
                (devices & PointerDevice) != 0U);
            portal.insert(
                QStringLiteral("available"),
                (devices & (KeyboardDevice | PointerDevice))
                        == (KeyboardDevice | PointerDevice)
                    && (sources & (MonitorSource | WindowSource)) != 0U);
            portal.insert(
                QStringLiteral("absolutePointerCapability"),
                (sources & (MonitorSource | WindowSource)) != 0U
                    ? QStringLiteral("requires-consented-stream")
                    : QStringLiteral("unavailable-no-source-types"));
        } catch (const ProbeError& error) {
            portal.insert(QStringLiteral("errorKind"), error.kind());
            portal.insert(QStringLiteral("error"), error.message());
        }
    }

    QJsonObject sidecar{
        {QStringLiteral("running"), false},
        {QStringLiteral("pending"), false},
        {QStringLiteral("sessionClosed"), false},
    };
    try {
        const auto state = readState(false);
        if (!state.isEmpty()) {
            const auto committed =
                state.value(QStringLiteral("committed")).toBool();
            sidecar.insert(
                QStringLiteral("pid"),
                state.value(QStringLiteral("pid")));
            sidecar.insert(
                QStringLiteral("socket"),
                state.value(QStringLiteral("socket")));
            sidecar.insert(
                QStringLiteral("sessionClosed"),
                state.value(QStringLiteral("sessionClosed")).toBool());
            sidecar.insert(QStringLiteral("pending"), !committed);
            try {
                const auto response = sidecarRequestWithState(
                    {{QStringLiteral("command"), QStringLiteral("status")}},
                    state,
                    1000);
                sidecar.insert(
                    QStringLiteral("running"),
                    committed);
                sidecar.insert(
                    QStringLiteral("session"),
                    response.value(QStringLiteral("session")));
            } catch (const ProbeError& error) {
                sidecar.insert(QStringLiteral("errorKind"), error.kind());
                sidecar.insert(QStringLiteral("error"), error.message());
            }
        } else {
            const auto pending = readPendingState();
            if (!pending.isEmpty()
                && sameProcessInstance(
                    pending.value(QStringLiteral("pid")).toInteger(),
                    pending
                        .value(QStringLiteral("processStartTime"))
                        .toString())) {
                sidecar.insert(QStringLiteral("pending"), true);
                sidecar.insert(
                    QStringLiteral("pid"),
                    pending.value(QStringLiteral("pid")));
                sidecar.insert(
                    QStringLiteral("processStartTime"),
                    pending.value(QStringLiteral("processStartTime")));
            } else if (!pending.isEmpty()) {
                removePendingState(
                    pending
                        .value(QStringLiteral("startupToken"))
                        .toString());
            }
        }
    } catch (const ProbeError& error) {
        sidecar.insert(QStringLiteral("errorKind"), error.kind());
        sidecar.insert(QStringLiteral("error"), error.message());
    }

    const auto wayland =
        qEnvironmentVariable("XDG_SESSION_TYPE")
                .compare(QStringLiteral("wayland"), Qt::CaseInsensitive)
            == 0
        || !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty();
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("input-status")},
        {QStringLiteral("selectedAdapter"),
         wayland ? QStringLiteral("portal-sidecar")
                 : QStringLiteral("atspi-device-event-controller")},
        {QStringLiteral("atspi"), atspi},
        {QStringLiteral("portal"), portal},
        {QStringLiteral("sidecar"), sidecar},
        {QStringLiteral("portalSecurityAuthority"), true},
        {QStringLiteral("limits"),
         QJsonObject{
             {QStringLiteral("maximumMessageBytes"),
              InputProtocol::MaximumMessageBytes},
             {QStringLiteral("maximumEventsPerRequest"),
              InputProtocol::MaximumEventsPerRequest},
             {QStringLiteral("maximumEventsPerSecond"),
              InputProtocol::MaximumEventsPerSecond},
             {QStringLiteral("maximumDragDurationMs"),
              InputProtocol::MaximumDragDurationMs},
         }},
    };
}

QJsonObject servePortalInput(
    const qint64 absoluteDeadlineMs,
    const int lifecycleDescriptor,
    const qint64 launcherPid,
    const QString& startupToken)
{
    const auto processId = QCoreApplication::applicationPid();
    const auto processStart = processStartTime(processId);
    if (processStart.isEmpty()) {
        fail(
            ExitCode::Environment,
            QStringLiteral("sidecar-process-identity-unavailable"),
            QStringLiteral(
                "Could not verify the input sidecar process identity"));
    }
    StartupLifecycle lifecycle(
        lifecycleDescriptor,
        launcherPid,
        startupToken,
        absoluteDeadlineMs);
    struct LifecycleRegistration final {
        explicit LifecycleRegistration(StartupLifecycle* lifecycle)
        {
            startupLifecycle = lifecycle;
        }
        ~LifecycleRegistration()
        {
            startupLifecycle = nullptr;
        }
    } lifecycleRegistration(&lifecycle);
    struct PendingStateCleanup final {
        QString token;
        ~PendingStateCleanup()
        {
            removePendingState(token);
        }
    } pendingCleanup{startupToken};
    lifecycle.throwIfCancelled();
    if (absoluteDeadlineMs <= monotonicMilliseconds()) {
        fail(
            ExitCode::Timeout,
            QStringLiteral("sidecar-start-timeout"),
            QStringLiteral(
                "The input sidecar startup deadline has expired"));
    }
    const auto root = runtimeRoot();
    QLockFile sidecarLock(
        QDir(root).filePath(QStringLiteral("input-sidecar.lock")));
    sidecarLock.setStaleLockTime(0);
    if (!sidecarLock.tryLock(0)) {
        fail(
            ExitCode::Ambiguous,
            QStringLiteral("sidecar-already-starting"),
            QStringLiteral(
                "Another input sidecar already owns the per-user lock"));
    }
    if (!readState(false).isEmpty()) {
        fail(
            ExitCode::Ambiguous,
            QStringLiteral("sidecar-already-running"),
            QStringLiteral(
                "Input sidecar state already exists; use input-stop or "
                "remove only a verified stale state"));
    }

    PortalSession session(absoluteDeadlineMs);
    auto adapter = session.open();
    const auto nonce = randomToken();
    const auto socketPath =
        QDir(root).filePath(
            QStringLiteral("input-%1.sock").arg(randomToken().left(16)));
    QLocalServer server;
    if (!server.listen(socketPath)
        || !QFile::setPermissions(
            socketPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        session.close();
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-listen-failed"),
            QStringLiteral("Could not create the private input socket"));
    }
    QJsonObject state{
        {QStringLiteral("pid"),
         static_cast<qint64>(processId)},
        {QStringLiteral("processStartTime"),
         processStart},
        {QStringLiteral("socket"), socketPath},
        {QStringLiteral("nonce"), nonce},
        {QStringLiteral("startupToken"), startupToken},
        {QStringLiteral("committed"), false},
        {QStringLiteral("portalVersion"),
         static_cast<qint64>(session.version())},
        {QStringLiteral("availableDeviceTypes"),
         static_cast<qint64>(session.deviceTypes())},
        {QStringLiteral("availableSourceTypes"),
         static_cast<qint64>(session.sourceTypes())},
        {QStringLiteral("sessionClosed"), false},
    };
    try {
        writeSidecarState(state);
    } catch (...) {
        server.close();
        QLocalServer::removeServer(socketPath);
        throw;
    }
    bool shuttingDown = false;
    bool stopRequested = false;
    bool closedByPortal = false;
    bool startupCommitted = false;
    bool executingInput = false;
    const auto handleSessionClosed = [&] {
        if (closedByPortal) {
            return;
        }
        closedByPortal = true;
        state.insert(QStringLiteral("sessionClosed"), true);
        try {
            writeSidecarState(state);
        } catch (const ProbeError&) {
        }
        shuttingDown = true;
        server.close();
        QCoreApplication::quit();
    };
    QObject::connect(
        &session,
        &PortalSession::sessionClosed,
        &server,
        handleSessionClosed);
    QObject::connect(
        &lifecycle,
        &StartupLifecycle::terminationRequested,
        &server,
        [&] {
            shuttingDown = true;
            stopRequested = true;
            server.close();
            QCoreApplication::quit();
        });
    if (session.closed()) {
        handleSessionClosed();
    }
    QObject::connect(
        &server,
        &QLocalServer::newConnection,
        &server,
        [&] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                socket->setParent(&server);
                if (!sameUidPeer(socket)) {
                    socket->abort();
                    socket->deleteLater();
                    continue;
                }
                auto* buffer = new QByteArray;
                QObject::connect(
                    socket,
                    &QLocalSocket::readyRead,
                    socket,
                    [&, socket, buffer] {
                        try {
                            const auto chunk = socket->readAll();
                            if (buffer->size() + chunk.size()
                                > InputProtocol::MaximumMessageBytes + 4) {
                                fail(
                                    ExitCode::LimitExceeded,
                                    QStringLiteral("input-frame-limit"),
                                    QStringLiteral(
                                        "Input sidecar request exceeds "
                                        "64 KiB"));
                            }
                            buffer->append(chunk);
                            auto request = InputProtocol::takeFrame(buffer);
                            if (!request.has_value()) {
                                return;
                            }
                            if (!buffer->isEmpty()) {
                                fail(
                                    ExitCode::LimitExceeded,
                                    QStringLiteral("multiple-input-frames"),
                                    QStringLiteral(
                                        "One sidecar connection accepts "
                                        "exactly one request"));
                            }
                            if (request->value(QStringLiteral("nonce")).toString()
                                != nonce) {
                                fail(
                                    ExitCode::Environment,
                                    QStringLiteral("sidecar-auth-failed"),
                                    QStringLiteral(
                                        "Input sidecar nonce did not match"));
                            }
                            if (request
                                        ->value(QStringLiteral("expectedPid"))
                                        .toInteger()
                                    != processId
                                || request
                                           ->value(QStringLiteral(
                                               "expectedProcessStartTime"))
                                           .toString()
                                    != processStart
                                || request
                                           ->value(QStringLiteral(
                                               "expectedStartupToken"))
                                           .toString()
                                    != startupToken) {
                                fail(
                                    ExitCode::Environment,
                                    QStringLiteral(
                                        "sidecar-identity-mismatch"),
                                    QStringLiteral(
                                        "Input sidecar identity did not "
                                        "match the selected state"));
                            }
                            const auto command =
                                request->value(QStringLiteral("command"))
                                    .toString();
                            if (!startupCommitted
                                && command
                                    != QStringLiteral("status")) {
                                fail(
                                    ExitCode::Environment,
                                    QStringLiteral(
                                        "sidecar-start-pending"),
                                    QStringLiteral(
                                        "The input sidecar is not committed"));
                            }
                            QJsonObject response{
                                {QStringLiteral("ok"), true},
                                {QStringLiteral("command"), command},
                                {QStringLiteral("adapter"), adapter->name()},
                            };
                            if (command == QStringLiteral("shutdown")) {
                                shuttingDown = true;
                                stopRequested = true;
                                response.insert(
                                    QStringLiteral("stopping"),
                                    true);
                            } else if (command == QStringLiteral("status")) {
                                response.insert(
                                    QStringLiteral("pid"),
                                    static_cast<qint64>(processId));
                                response.insert(
                                    QStringLiteral("processStartTime"),
                                    processStart);
                                response.insert(
                                    QStringLiteral("socket"),
                                    socketPath);
                                response.insert(
                                    QStringLiteral("nonce"),
                                    nonce);
                                response.insert(
                                    QStringLiteral("startupToken"),
                                    startupToken);
                                response.insert(
                                    QStringLiteral("committed"),
                                    startupCommitted);
                                response.insert(
                                    QStringLiteral("session"),
                                    adapter->status());
                            } else {
                                static const QSet<QString> inputCommands{
                                    QStringLiteral("key"),
                                    QStringLiteral("shortcut"),
                                    QStringLiteral("pointer"),
                                    QStringLiteral("button"),
                                    QStringLiteral("drag"),
                                };
                                if (!inputCommands.contains(command)) {
                                    fail(
                                        ExitCode::Usage,
                                        QStringLiteral(
                                            "invalid-sidecar-command"),
                                        QStringLiteral(
                                            "Unsupported input sidecar "
                                            "command"));
                                }
                                if (executingInput) {
                                    fail(
                                            ExitCode::Ambiguous,
                                            QStringLiteral("sidecar-busy"),
                                            QStringLiteral(
                                                "The input sidecar is already "
                                                "executing an input request"));
                                }
                                const auto events = eventsFromJson(
                                    request->value(QStringLiteral("events"))
                                        .toArray());
                                const auto cleanup = eventsFromJson(
                                    request->value(QStringLiteral("cleanup"))
                                        .toArray());
                                if (events.isEmpty()) {
                                    fail(
                                        ExitCode::Usage,
                                        QStringLiteral("empty-input-request"),
                                        QStringLiteral(
                                            "Input request contains no events"));
                                }
                                struct ExecutionGuard final {
                                    explicit ExecutionGuard(bool* executing)
                                            : value(executing)
                                    {
                                            *value = true;
                                    }
                                    ~ExecutionGuard()
                                    {
                                            *value = false;
                                    }
                                    bool* value;
                                } executionGuard(&executingInput);
                                executeEvents(
                                    adapter.get(),
                                    events,
                                    cleanup,
                                    [&] {
                                            lifecycle.processSignals();
                                            return stopRequested
                                                || closedByPortal
                                                || lifecycle
                                                       .terminationRequestedState();
                                    });
                                response.insert(
                                    QStringLiteral("eventCount"),
                                    events.size());
                            }
                            const auto framed = InputProtocol::frame(response);
                            socket->write(framed);
                            socket->flush();
                            socket->disconnectFromServer();
                            if (shuttingDown) {
                                QTimer::singleShot(
                                    0,
                                    QCoreApplication::instance(),
                                    &QCoreApplication::quit);
                            }
                        } catch (const ProbeError& error) {
                            socket->write(
                                InputProtocol::frame(errorJson(error)));
                            socket->flush();
                            socket->disconnectFromServer();
                        }
                    });
                QObject::connect(
                    socket,
                    &QLocalSocket::disconnected,
                    socket,
                    [socket, buffer] {
                        delete buffer;
                        socket->deleteLater();
                    });
            }
        });

    const QJsonObject identity{
        {QStringLiteral("pid"), static_cast<qint64>(processId)},
        {QStringLiteral("processStartTime"), processStart},
        {QStringLiteral("socket"), socketPath},
        {QStringLiteral("nonce"), nonce},
    };
    try {
        lifecycle.readyAndAwaitCommit(
            identity,
            [&] {
                state.insert(QStringLiteral("committed"), true);
                writeSidecarState(state);
                startupCommitted = true;
            });
    } catch (...) {
        session.close();
        server.close();
        QLocalServer::removeServer(socketPath);
        removeExactStateArtifacts(state);
        throw;
    }

    const auto exitCode =
        shuttingDown ? 0 : QCoreApplication::exec();
    session.close();
    server.close();
    if (!closedByPortal || stopRequested) {
        removeExactStateArtifacts(state);
    } else if (QFileInfo(socketPath).absolutePath()
               == QFileInfo(root).absoluteFilePath()) {
        QLocalServer::removeServer(socketPath);
    }
    if (exitCode != 0) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-event-loop-failed"),
            QStringLiteral("Input sidecar event loop failed"));
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("input-serve")},
        {QStringLiteral("stopped"), true},
        {QStringLiteral("portalSessionClosed"), session.closed()},
    };
}

namespace {

struct StartedSidecar {
    pid_t pid = -1;
    QString startTime;
    int lifecycleDescriptor = -1;
    QString startupToken;
    QByteArray lifecycleBuffer;
};

StartedSidecar spawnSidecar(
    QFile* log,
    const qint64 absoluteDeadlineMs,
    const QString& startupToken)
{
    const auto program =
        QFile::encodeName(QCoreApplication::applicationFilePath());
    const auto deadline =
        QByteArray::number(absoluteDeadlineMs);
    const auto token = startupToken.toUtf8();
    const auto descriptor = log->handle();
    const auto launcherPid = ::getpid();
    const auto launcherPidBytes = QByteArray::number(launcherPid);
    int lifecycle[2] {-1, -1};
    if (::socketpair(
            AF_UNIX,
            SOCK_STREAM | SOCK_CLOEXEC,
            0,
            lifecycle)
        != 0) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-lifecycle-failed"),
            QStringLiteral(
                "Could not create the input sidecar lifecycle channel"));
    }
    const auto processId = ::fork();
    if (processId == 0) {
        const auto lifecycleDescriptorBytes =
            QByteArray::number(lifecycle[1]);
        static_cast<void>(::close(lifecycle[0]));
        if (::prctl(PR_SET_PDEATHSIG, SIGTERM) != 0
            || ::getppid() != launcherPid) {
            ::_exit(125);
        }
        const auto lifecycleFlags =
            ::fcntl(lifecycle[1], F_GETFD);
        if (lifecycleFlags < 0
            || ::fcntl(
                   lifecycle[1],
                   F_SETFD,
                   lifecycleFlags & ~FD_CLOEXEC)
                < 0) {
            ::_exit(126);
        }
        static_cast<void>(::setsid());
        if (::dup2(descriptor, STDOUT_FILENO) < 0
            || ::dup2(descriptor, STDERR_FILENO) < 0) {
            ::_exit(126);
        }
        if (descriptor > STDERR_FILENO) {
            static_cast<void>(::close(descriptor));
        }
        ::execl(
            program.constData(),
            program.constData(),
            "input-serve",
            "--deadline-monotonic-ms",
            deadline.constData(),
            "--startup-token",
            token.constData(),
            "--lifecycle-fd",
            lifecycleDescriptorBytes.constData(),
            "--launcher-pid",
            launcherPidBytes.constData(),
            static_cast<char*>(nullptr));
        ::_exit(127);
    }
    static_cast<void>(::close(lifecycle[1]));
    if (processId < 0) {
        static_cast<void>(::close(lifecycle[0]));
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-start-failed"),
            QStringLiteral("Could not fork the input sidecar"));
    }
    QString startTime;
    for (int attempt = 0; attempt < 20 && startTime.isEmpty(); ++attempt) {
        startTime = processStartTime(processId);
        if (startTime.isEmpty()) {
            QThread::msleep(5);
        }
    }
    if (startTime.isEmpty()) {
        static_cast<void>(::close(lifecycle[0]));
        static_cast<void>(::kill(processId, SIGKILL));
        int status = 0;
        while (::waitpid(processId, &status, 0) < 0
               && errno == EINTR) {
        }
        fail(
            ExitCode::Environment,
            QStringLiteral("sidecar-process-identity-unavailable"),
            QStringLiteral(
                "Could not verify the started input sidecar identity"));
    }
    return {
        processId,
        startTime,
        lifecycle[0],
        startupToken,
        {},
    };
}

void stopAndReapStartedSidecar(const StartedSidecar& child)
{
    if (child.pid <= 0) {
        return;
    }
    int status = 0;
    const auto graceDeadline = monotonicMilliseconds() + 1000;
    while (monotonicMilliseconds() < graceDeadline) {
        const auto result = ::waitpid(child.pid, &status, WNOHANG);
        if (result == child.pid || (result < 0 && errno == ECHILD)) {
            return;
        }
        if (result < 0 && errno != EINTR) {
            return;
        }
        QThread::msleep(10);
    }
    if (::kill(child.pid, SIGTERM) == 0 || errno == EPERM) {
        const auto termDeadline = monotonicMilliseconds() + 1000;
        while (monotonicMilliseconds() < termDeadline) {
            const auto result = ::waitpid(child.pid, &status, WNOHANG);
            if (result == child.pid || (result < 0 && errno == ECHILD)) {
                return;
            }
            if (result < 0 && errno != EINTR) {
                return;
            }
            QThread::msleep(10);
        }
    }
    static_cast<void>(::kill(child.pid, SIGKILL));
    while (::waitpid(child.pid, &status, 0) < 0 && errno == EINTR) {
    }
}

void removeStartedSidecarArtifacts(const StartedSidecar& child)
{
    removePendingState(child.startupToken);
    try {
        const auto state = readState(false);
        if (state.isEmpty()
            || state.value(QStringLiteral("pid")).toInteger() != child.pid
            || state.value(QStringLiteral("startupToken")).toString()
                != child.startupToken
            || (!child.startTime.isEmpty()
                && state.value(QStringLiteral("processStartTime")).toString()
                    != child.startTime)) {
            return;
        }
        removeExactStateArtifacts(state);
    } catch (const ProbeError&) {
    }
}

void closeLifecycle(StartedSidecar* child)
{
    if (child->lifecycleDescriptor >= 0) {
        static_cast<void>(::close(child->lifecycleDescriptor));
        child->lifecycleDescriptor = -1;
    }
}

void writeLifecycleFrame(
    const StartedSidecar& child,
    const QJsonObject& message,
    const qint64 absoluteDeadlineMs)
{
    const auto frame = InputProtocol::frame(message);
    qsizetype offset = 0;
    while (offset < frame.size()) {
        const auto count =
            ::send(
                child.lifecycleDescriptor,
                frame.constData() + offset,
                static_cast<size_t>(frame.size() - offset),
                MSG_NOSIGNAL);
        if (count > 0) {
            offset += count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        pollfd descriptor{
            .fd = child.lifecycleDescriptor,
            .events = POLLOUT,
            .revents = 0,
        };
        if (count < 0
            && (errno == EAGAIN || errno == EWOULDBLOCK)
            && ::poll(
                   &descriptor,
                   1,
                   remainingMilliseconds(
                       absoluteDeadlineMs,
                       1000))
                > 0) {
            continue;
        }
        fail(
            ExitCode::Environment,
            QStringLiteral("sidecar-lifecycle-failed"),
            QStringLiteral(
                "The input sidecar lifecycle channel closed"));
    }
}

std::optional<QJsonObject> readLifecycleFrame(
    StartedSidecar* child,
    const qint64 absoluteDeadlineMs)
{
    if (auto message =
            InputProtocol::takeFrame(&child->lifecycleBuffer);
        message.has_value()) {
        return message;
    }
    pollfd descriptor{
        .fd = child->lifecycleDescriptor,
        .events = POLLIN,
        .revents = 0,
    };
    const auto result =
        ::poll(
            &descriptor,
            1,
            remainingMilliseconds(absoluteDeadlineMs, 100));
    if (result < 0 && errno == EINTR) {
        return std::nullopt;
    }
    if (result < 0) {
        fail(
            ExitCode::Environment,
            QStringLiteral("sidecar-lifecycle-failed"),
            QStringLiteral(
                "Could not monitor the input sidecar lifecycle channel"));
    }
    if (result == 0) {
        return std::nullopt;
    }
    char bytes[4096];
    const auto count =
        ::read(child->lifecycleDescriptor, bytes, sizeof(bytes));
    if (count > 0) {
        child->lifecycleBuffer.append(bytes, count);
        return InputProtocol::takeFrame(&child->lifecycleBuffer);
    }
    if (count < 0
        && (errno == EINTR || errno == EAGAIN
            || errno == EWOULDBLOCK)) {
        return std::nullopt;
    }
    if (count == 0) {
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("exited")},
        };
    }
    fail(
        ExitCode::Environment,
        QStringLiteral("sidecar-lifecycle-failed"),
        QStringLiteral(
            "The input sidecar exited before startup committed"));
}

}

QJsonObject startPortalInput(const int timeoutMs)
{
    QLockFile launcherLock(
        launcherLockPath());
    launcherLock.setStaleLockTime(0);
    if (!launcherLock.tryLock(0)) {
        fail(
            ExitCode::Ambiguous,
            QStringLiteral("sidecar-already-starting"),
            QStringLiteral(
                "Another input-start launcher owns the startup lock"));
    }
    std::optional<QJsonObject> existingStatus;
    try {
        existingStatus = sidecarRequest(
            {{QStringLiteral("command"), QStringLiteral("status")}},
            500);
    } catch (const ProbeError&) {
    }
    if (existingStatus.has_value()) {
        if (!existingStatus->value(
                               QStringLiteral("committed"))
                 .toBool()) {
            fail(
                ExitCode::Ambiguous,
                QStringLiteral("sidecar-already-starting"),
                QStringLiteral(
                    "An input sidecar is still awaiting launcher commit"));
        }
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("command"), QStringLiteral("input-start")},
            {QStringLiteral("alreadyRunning"), true},
            {QStringLiteral("sidecar"),
             QJsonObject{
                 {QStringLiteral("ok"), true},
                 {QStringLiteral("command"), QStringLiteral("status")},
                 {QStringLiteral("adapter"),
                  existingStatus->value(QStringLiteral("adapter"))},
                 {QStringLiteral("session"),
                  existingStatus->value(QStringLiteral("session"))},
             }},
        };
    }

    const auto state = readState(false);
    if (!state.isEmpty()) {
        const auto recordedPid =
            state.value(QStringLiteral("pid")).toInteger();
        if (sameProcessInstance(
                recordedPid,
                state.value(QStringLiteral("processStartTime")).toString())) {
            fail(
                ExitCode::Environment,
                QStringLiteral("sidecar-unresponsive"),
                QStringLiteral(
                    "The recorded input sidecar is still running but "
                    "did not accept an authenticated request"));
        }
        removeExactStateArtifacts(state);
    }
    const auto pending = readPendingState();
    if (!pending.isEmpty()) {
        if (sameProcessInstance(
                pending.value(QStringLiteral("pid")).toInteger(),
                pending
                    .value(QStringLiteral("processStartTime"))
                    .toString())) {
            fail(
                ExitCode::Ambiguous,
                QStringLiteral("sidecar-already-starting"),
                QStringLiteral(
                    "Another input sidecar is awaiting portal consent"));
        }
        removePendingState(
            pending
                .value(QStringLiteral("startupToken"))
                .toString());
    }

    const auto logPath =
        QDir(runtimeRoot()).filePath(QStringLiteral("input-sidecar.log"));
    QFile log(logPath);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append)
        || !log.setPermissions(
            QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        fail(
            ExitCode::RemoteError,
            QStringLiteral("sidecar-log-failed"),
            QStringLiteral("Could not secure the input sidecar log"));
    }
    const auto absoluteDeadlineMs =
        monotonicMilliseconds() + timeoutMs;
    const auto startupToken = randomToken();
    QFile::remove(startupFailurePath(startupToken));
    const auto launcherPid = QCoreApplication::applicationPid();
    const auto launcherStartTime =
        processStartTime(launcherPid);
    if (launcherStartTime.isEmpty()) {
        fail(
            ExitCode::Environment,
            QStringLiteral("launcher-process-identity-unavailable"),
            QStringLiteral(
                "Could not verify the input-start launcher identity"));
    }
    writePendingState(
        {
            {QStringLiteral("pid"),
             static_cast<qint64>(launcherPid)},
            {QStringLiteral("processStartTime"), launcherStartTime},
            {QStringLiteral("startupToken"), startupToken},
            {QStringLiteral("phase"), QStringLiteral("launcher")},
        });
    StartedSidecar child;
    try {
        child =
            spawnSidecar(&log, absoluteDeadlineMs, startupToken);
    } catch (...) {
        removePendingState(startupToken);
        throw;
    }
    log.close();
    auto mutableChild = child;
    try {
        writePendingState(
            {
                {QStringLiteral("pid"),
                 static_cast<qint64>(child.pid)},
                {QStringLiteral("processStartTime"), child.startTime},
                {QStringLiteral("startupToken"), startupToken},
                {QStringLiteral("phase"), QStringLiteral("sidecar")},
            });
        const auto throwStartupFailure = [&]() {
            const QFileInfo failureInfo(
                startupFailurePath(startupToken));
            if (!failureInfo.exists() || !failureInfo.isFile()
                || !privateOwnerFile(failureInfo)
                || failureInfo.size() <= 0
                || failureInfo.size() > 16384) {
                return false;
            }
            QFile failureFile(startupFailurePath(startupToken));
            if (!failureFile.open(QIODevice::ReadOnly)) {
                return false;
            }
            const auto failure =
                QJsonDocument::fromJson(failureFile.readAll())
                    .object()
                    .value(QStringLiteral("error"))
                    .toObject();
            QFile::remove(startupFailurePath(startupToken));
            fail(
                static_cast<ExitCode>(
                    failure.value(QStringLiteral("exitCode"))
                        .toInt(
                            static_cast<int>(
                                ExitCode::RemoteError))),
                failure.value(QStringLiteral("kind")).toString(),
                failure.value(QStringLiteral("message")).toString());
        };
        QJsonObject ready;
        while (monotonicMilliseconds() < absoluteDeadlineMs
               && ready.isEmpty()) {
            static_cast<void>(throwStartupFailure());
            if (auto message =
                    readLifecycleFrame(
                        &mutableChild,
                        absoluteDeadlineMs);
                message.has_value()) {
                if (message->value(QStringLiteral("type")).toString()
                    == QStringLiteral("exited")) {
                    static_cast<void>(throwStartupFailure());
                    fail(
                        ExitCode::Environment,
                        QStringLiteral("sidecar-start-failed"),
                        QStringLiteral(
                            "The input sidecar exited before startup "
                            "committed"));
                }
                ready = *message;
            }
        }
        if (ready.isEmpty()) {
            fail(
                ExitCode::Timeout,
                QStringLiteral("sidecar-start-timeout"),
                QStringLiteral(
                    "Timed out waiting for RemoteDesktop consent and "
                    "sidecar startup"));
        }
        if (ready.value(QStringLiteral("type")).toString()
                != QStringLiteral("ready")
            || ready.value(QStringLiteral("startupToken")).toString()
                != startupToken
            || ready.value(QStringLiteral("pid")).toInteger()
                != child.pid
            || ready
                   .value(QStringLiteral("processStartTime"))
                   .toString()
                != child.startTime) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("sidecar-identity-mismatch"),
                QStringLiteral(
                    "The sidecar READY message did not match the exact "
                    "child process"));
        }
        const auto socketPath =
            ready.value(QStringLiteral("socket")).toString();
        const auto nonce =
            ready.value(QStringLiteral("nonce")).toString();
        const QFileInfo socketInfo(socketPath);
        if (!socketInfo.isAbsolute()
            || socketInfo.absolutePath()
                != QFileInfo(runtimeRoot()).absoluteFilePath()
            || !socketInfo.fileName().startsWith(
                QStringLiteral("input-"))
            || !socketInfo.fileName().endsWith(
                QStringLiteral(".sock"))
            || nonce.size() != 32) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("sidecar-identity-mismatch"),
                QStringLiteral(
                    "The sidecar READY message contained unsafe "
                    "identity values"));
        }
        const auto runningState = readState(true);
        if (runningState.value(QStringLiteral("pid")).toInteger()
                != child.pid
            || runningState
                   .value(QStringLiteral("processStartTime"))
                   .toString()
                != child.startTime
            || runningState.value(QStringLiteral("socket")).toString()
                != socketPath
            || runningState.value(QStringLiteral("nonce")).toString()
                != nonce
            || runningState
                   .value(QStringLiteral("startupToken"))
                   .toString()
                != startupToken
            || runningState.value(QStringLiteral("committed")).toBool()) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("sidecar-identity-mismatch"),
                QStringLiteral(
                    "The provisional sidecar state did not match READY"));
        }
        const auto sidecarStatus = sidecarRequest(
            {{QStringLiteral("command"), QStringLiteral("status")}},
            std::min(
                1000,
                remainingMilliseconds(absoluteDeadlineMs, 1000)));
        if (sidecarStatus.value(QStringLiteral("pid")).toInteger()
                != child.pid
            || sidecarStatus
                   .value(QStringLiteral("processStartTime"))
                   .toString()
                != child.startTime
            || sidecarStatus.value(QStringLiteral("socket")).toString()
                != socketPath
            || sidecarStatus.value(QStringLiteral("nonce")).toString()
                != nonce
            || sidecarStatus
                   .value(QStringLiteral("startupToken"))
                   .toString()
                != startupToken
            || sidecarStatus.value(QStringLiteral("committed")).toBool()) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("sidecar-identity-mismatch"),
                QStringLiteral(
                    "The authenticated sidecar status did not match READY"));
        }

        auto commit = ready;
        commit.insert(QStringLiteral("type"), QStringLiteral("commit"));
        writeLifecycleFrame(
            mutableChild,
            commit,
            absoluteDeadlineMs);
        QJsonObject acknowledged;
        while (monotonicMilliseconds() < absoluteDeadlineMs
               && acknowledged.isEmpty()) {
            static_cast<void>(throwStartupFailure());
            if (auto message =
                    readLifecycleFrame(
                        &mutableChild,
                        absoluteDeadlineMs);
                message.has_value()) {
                if (message->value(QStringLiteral("type")).toString()
                    == QStringLiteral("exited")) {
                    static_cast<void>(throwStartupFailure());
                    fail(
                        ExitCode::Environment,
                        QStringLiteral("sidecar-start-failed"),
                        QStringLiteral(
                            "The input sidecar exited before acknowledging "
                            "startup"));
                }
                acknowledged = *message;
            }
        }
        if (acknowledged.value(QStringLiteral("type")).toString()
                != QStringLiteral("committed")
            || acknowledged.value(QStringLiteral("startupToken")).toString()
                != startupToken
            || acknowledged.value(QStringLiteral("pid")).toInteger()
                != child.pid
            || acknowledged
                   .value(QStringLiteral("processStartTime"))
                   .toString()
                != child.startTime
            || acknowledged.value(QStringLiteral("socket")).toString()
                != socketPath
            || acknowledged.value(QStringLiteral("nonce")).toString()
                != nonce) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("sidecar-commit-mismatch"),
                QStringLiteral(
                    "The sidecar did not acknowledge the exact committed "
                    "identity"));
        }
        const auto committedState = readState(true);
        if (!committedState.value(QStringLiteral("committed")).toBool()
            || committedState.value(QStringLiteral("pid")).toInteger()
                != child.pid
            || committedState
                   .value(QStringLiteral("processStartTime"))
                   .toString()
                != child.startTime
            || committedState.value(QStringLiteral("socket")).toString()
                != socketPath
            || committedState.value(QStringLiteral("nonce")).toString()
                != nonce) {
            fail(
                ExitCode::RemoteError,
                QStringLiteral("sidecar-commit-mismatch"),
                QStringLiteral(
                    "The committed sidecar state did not match the "
                    "acknowledgment"));
        }
        closeLifecycle(&mutableChild);
        removePendingState(startupToken);
        QFile::remove(startupFailurePath(startupToken));
        QJsonObject publicStatus{
            {QStringLiteral("ok"), true},
            {QStringLiteral("command"), QStringLiteral("status")},
            {QStringLiteral("adapter"),
             sidecarStatus.value(QStringLiteral("adapter"))},
            {QStringLiteral("session"),
             sidecarStatus.value(QStringLiteral("session"))},
        };
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("command"), QStringLiteral("input-start")},
            {QStringLiteral("alreadyRunning"), false},
            {QStringLiteral("pid"), static_cast<qint64>(child.pid)},
            {QStringLiteral("sidecar"), publicStatus},
        };
    } catch (...) {
        closeLifecycle(&mutableChild);
        stopAndReapStartedSidecar(child);
        removeStartedSidecarArtifacts(child);
        QFile::remove(startupFailurePath(startupToken));
        throw;
    }
}

QJsonObject stopPortalInput(const int timeoutMs)
{
    auto state = readState(false);
    auto pending = readPendingState();
    std::unique_ptr<QLockFile> stopLauncherLock;
    if (state.isEmpty() && pending.isEmpty()) {
        QDeadlineTimer registrationDeadline(
            timeoutMs,
            Qt::PreciseTimer);
        while (state.isEmpty() && pending.isEmpty()
               && !registrationDeadline.hasExpired()) {
            auto launcherLock =
                std::make_unique<QLockFile>(launcherLockPath());
            launcherLock->setStaleLockTime(0);
            if (launcherLock->tryLock(0)) {
                state = readState(false);
                pending = readPendingState();
                stopLauncherLock = std::move(launcherLock);
                break;
            }
            QThread::msleep(1);
            state = readState(false);
            pending = readPendingState();
        }
    }
    if (state.isEmpty() && pending.isEmpty()) {
        fail(
            ExitCode::Environment,
            QStringLiteral("sidecar-not-running"),
            QStringLiteral(
                "The Wayland input sidecar is not running; run input-start"));
    }
    if (state.isEmpty()
        || !state.value(QStringLiteral("committed")).toBool()) {
        const auto startupToken =
            (state.isEmpty() ? pending : state)
                .value(QStringLiteral("startupToken"))
                .toString();
        const auto cancellingLauncher =
            state.isEmpty()
            && pending.value(QStringLiteral("phase")).toString()
                == QStringLiteral("launcher");
        const auto identity =
            state.isEmpty() ? pending : state;
        const auto processId =
            identity.value(QStringLiteral("pid")).toInteger();
        QDeadlineTimer deadline(timeoutMs, Qt::PreciseTimer);
        const auto cancelExact =
            [&](const QJsonObject& target) {
                const auto targetPid =
                    target.value(QStringLiteral("pid")).toInteger();
                const auto targetStart =
                    target
                        .value(QStringLiteral("processStartTime"))
                        .toString();
                bool selectedProcessExited = false;
                const auto processDescriptor =
                    exactProcessDescriptor(
                        targetPid,
                        targetStart,
                        &selectedProcessExited);
                if (processDescriptor < 0) {
                    if (selectedProcessExited) {
                        return;
                    }
                    fail(
                        ExitCode::Environment,
                        QStringLiteral("sidecar-pidfd-unavailable"),
                        QStringLiteral(
                            "Could not pin the exact pending input "
                            "sidecar process"));
                }
                if (::syscall(
                        SYS_pidfd_send_signal,
                        processDescriptor,
                        SIGTERM,
                        nullptr,
                        0U)
                        != 0
                    && errno != ESRCH) {
                    static_cast<void>(::close(processDescriptor));
                    fail(
                        ExitCode::RemoteError,
                        QStringLiteral("sidecar-cancel-failed"),
                        QStringLiteral(
                            "Could not cancel the exact pending input "
                            "sidecar"));
                }
                bool exited = false;
                while (!deadline.hasExpired()) {
                    pollfd descriptor{
                        .fd = processDescriptor,
                        .events = POLLIN,
                        .revents = 0,
                    };
                    const auto result =
                        ::poll(
                            &descriptor,
                            1,
                            static_cast<int>(
                                std::min<qint64>(
                                    std::max<qint64>(
                                        deadline.remainingTime(),
                                        1),
                                    100)));
                    if (result > 0
                        && (descriptor.revents & POLLIN) != 0) {
                        exited = true;
                        break;
                    }
                    if (result < 0 && errno != EINTR) {
                        static_cast<void>(::close(processDescriptor));
                        fail(
                            ExitCode::RemoteError,
                            QStringLiteral("sidecar-cancel-failed"),
                            QStringLiteral(
                                "Could not monitor the exact pending "
                                "input sidecar"));
                    }
                }
                static_cast<void>(::close(processDescriptor));
                if (!exited) {
                    fail(
                        ExitCode::Timeout,
                        QStringLiteral("sidecar-cancel-timeout"),
                        QStringLiteral(
                            "Timed out waiting for the pending input "
                            "sidecar to exit"));
                }
            };
        const auto removeProvisionalState =
            [&](const QJsonObject& candidate) {
                if (candidate.isEmpty()
                    || candidate
                           .value(QStringLiteral("startupToken"))
                           .toString()
                        != startupToken) {
                    return;
                }
                removeExactStateArtifacts(candidate);
            };

        cancelExact(identity);
        removeProvisionalState(state);
        if (!cancellingLauncher) {
            removePendingState(startupToken);
        }

        if (cancellingLauncher) {
            auto currentState = readState(false);
            auto currentPending = readPendingState();
            if (currentState
                    .value(QStringLiteral("startupToken"))
                    .toString()
                != startupToken) {
                currentState = {};
            }
            if (currentPending
                    .value(QStringLiteral("startupToken"))
                    .toString()
                != startupToken) {
                currentPending = {};
            }
            if (!currentState.isEmpty()
                && currentState
                       .value(QStringLiteral("committed"))
                       .toBool()) {
                state = currentState;
                removePendingState(startupToken);
            } else {
                if (!currentState.isEmpty()) {
                    cancelExact(currentState);
                    removeProvisionalState(currentState);
                } else if (!currentPending.isEmpty()
                           && currentPending
                                  .value(QStringLiteral("phase"))
                                  .toString()
                               == QStringLiteral("sidecar")) {
                    cancelExact(currentPending);
                    removeProvisionalState(readState(false));
                }
                removePendingState(startupToken);
            }
        }
        if (state.isEmpty()
            || !state.value(QStringLiteral("committed")).toBool()) {
            return {
                {QStringLiteral("ok"), true},
                {QStringLiteral("command"), QStringLiteral("input-stop")},
                {QStringLiteral("pid"), processId},
                {QStringLiteral("stopped"), true},
                {QStringLiteral("pendingCancelled"), true},
            };
        }
    }
    const auto processId = state.value(QStringLiteral("pid")).toInteger();
    const auto startTime =
        state.value(QStringLiteral("processStartTime")).toString();
    QDeadlineTimer deadline(timeoutMs, Qt::PreciseTimer);
    QJsonObject response;
    try {
        response = sidecarRequestWithState(
            {{QStringLiteral("command"), QStringLiteral("shutdown")}},
            state,
            timeoutMs);
    } catch (const ProbeError&) {
        if (!state.value(QStringLiteral("sessionClosed")).toBool()) {
            throw;
        }
        while (sameProcessInstance(processId, startTime)
               && !deadline.hasExpired()) {
            QThread::msleep(25);
        }
        if (sameProcessInstance(processId, startTime)) {
            fail(
                ExitCode::Timeout,
                QStringLiteral("closed-sidecar-stop-timeout"),
                QStringLiteral(
                    "Timed out waiting for the closed portal sidecar "
                    "process to exit"));
        }
        removeExactStateArtifacts(state);
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("command"), QStringLiteral("input-stop")},
            {QStringLiteral("pid"), processId},
            {QStringLiteral("stopped"), true},
            {QStringLiteral("sessionAlreadyClosed"), true},
        };
    }
    while ((exactStateExists(state)
            || sameProcessInstance(processId, startTime))
           && !deadline.hasExpired()) {
        QThread::msleep(25);
    }
    if (exactStateExists(state)
        || sameProcessInstance(processId, startTime)) {
        fail(
            ExitCode::Timeout,
            QStringLiteral("sidecar-stop-timeout"),
            QStringLiteral("Timed out waiting for the input sidecar to stop"));
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("command"), QStringLiteral("input-stop")},
        {QStringLiteral("pid"), processId},
        {QStringLiteral("stopped"), true},
        {QStringLiteral("sidecar"), response},
    };
}

void recordPortalInputStartupFailure(
    const ProbeError& error,
    const QString& startupToken) noexcept
{
    try {
        writePrivateJson(
            startupFailurePath(startupToken),
            errorJson(error));
    } catch (...) {
    }
}

}

#include "InputAdapter.moc"
