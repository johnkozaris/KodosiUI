#include "models/DesktopSettings.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>

#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

constexpr auto settingsKey = "desktop/settings.v1";
constexpr auto currentVersion = 1;

const QStringList legacyKeys {
    QStringLiteral("term.fontFamily"),
    QStringLiteral("term.cursorStyle"),
    QStringLiteral("term.fontSize"),
    QStringLiteral("term.lineHeight"),
    QStringLiteral("term.scrollbackLines"),
    QStringLiteral("term.cursorBlink"),
    QStringLiteral("term.cursorBlinkEfficientDefaultApplied"),
    QStringLiteral("session.lastWorkingDir"),
    QStringLiteral("claude.toolApprovalAlerts"),
};

QString cursorName(const DesktopSettings::CursorStyle cursor)
{
    switch (cursor) {
    case DesktopSettings::CursorStyle::Block:
        return QStringLiteral("block");
    case DesktopSettings::CursorStyle::Bar:
        return QStringLiteral("bar");
    case DesktopSettings::CursorStyle::Underline:
        return QStringLiteral("underline");
    }
    return {};
}

std::optional<DesktopSettings::CursorStyle> cursorFromName(const QString& name)
{
    if (name == QStringLiteral("block")) {
        return DesktopSettings::CursorStyle::Block;
    }
    if (name == QStringLiteral("bar")) {
        return DesktopSettings::CursorStyle::Bar;
    }
    if (name == QStringLiteral("underline")) {
        return DesktopSettings::CursorStyle::Underline;
    }
    return std::nullopt;
}

bool validFamily(const QString& family)
{
    const auto trimmed = family.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= 256
        && !trimmed.contains(QChar::Null);
}

bool validDirectoryValue(const std::optional<QString>& directory)
{
    return !directory
        || (directory->size() <= 4'096 && !directory->contains(QChar::Null));
}

bool validValues(const DesktopSettings::Values& values)
{
    return validFamily(values.fontFamily)
        && values.fontSize >= DesktopSettings::minimumFontSize()
        && values.fontSize <= DesktopSettings::maximumFontSize()
        && std::isfinite(values.lineHeight)
        && values.lineHeight >= DesktopSettings::minimumLineHeight()
        && values.lineHeight <= DesktopSettings::maximumLineHeight()
        && values.scrollbackLines >= DesktopSettings::minimumScrollbackLines()
        && values.scrollbackLines <= DesktopSettings::maximumScrollbackLines()
        && validDirectoryValue(values.lastWorkingDirectory);
}

DesktopSettings::Values normalized(DesktopSettings::Values values)
{
    const auto defaults = DesktopSettings::defaultValues();
    values.fontFamily = values.fontFamily.trimmed();
    if (!validFamily(values.fontFamily)) {
        values.fontFamily = defaults.fontFamily;
    }
    values.fontSize = std::clamp(
        values.fontSize,
        DesktopSettings::minimumFontSize(),
        DesktopSettings::maximumFontSize());
    if (!std::isfinite(values.lineHeight)) {
        values.lineHeight = defaults.lineHeight;
    } else {
        values.lineHeight = std::clamp(
            values.lineHeight,
            DesktopSettings::minimumLineHeight(),
            DesktopSettings::maximumLineHeight());
    }
    values.scrollbackLines = std::clamp(
        values.scrollbackLines,
        DesktopSettings::minimumScrollbackLines(),
        DesktopSettings::maximumScrollbackLines());
    if (values.lastWorkingDirectory) {
        *values.lastWorkingDirectory =
            values.lastWorkingDirectory->trimmed();
        if (values.lastWorkingDirectory->isEmpty()
            || !validDirectoryValue(values.lastWorkingDirectory)) {
            values.lastWorkingDirectory.reset();
        }
    }
    return values;
}

QByteArray encode(const DesktopSettings::Values& values)
{
    QJsonObject terminal {
        {QStringLiteral("fontFamily"), values.fontFamily},
        {QStringLiteral("fontSize"), values.fontSize},
        {QStringLiteral("cursorStyle"), cursorName(values.cursorStyle)},
        {QStringLiteral("lineHeight"), values.lineHeight},
        {QStringLiteral("scrollbackLines"), values.scrollbackLines},
        {QStringLiteral("cursorBlink"), values.cursorBlink},
    };
    QJsonObject root {
        {QStringLiteral("version"), currentVersion},
        {QStringLiteral("terminal"), terminal},
        {QStringLiteral("toolApprovalAlerts"), values.toolApprovalAlerts},
        {
            QStringLiteral("lastWorkingDirectory"),
            values.lastWorkingDirectory
                ? QJsonValue(*values.lastWorkingDirectory)
                : QJsonValue(QJsonValue::Null),
        },
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<int> exactInteger(
    const QJsonObject& object,
    const QString& key)
{
    const auto value = object.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < static_cast<double>(std::numeric_limits<int>::min())
        || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

std::optional<DesktopSettings::Values> decode(const QByteArray& json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const auto root = document.object();
    const auto version = exactInteger(root, QStringLiteral("version"));
    const auto terminalValue = root.value(QStringLiteral("terminal"));
    const auto alertsValue = root.value(QStringLiteral("toolApprovalAlerts"));
    const auto directoryValue = root.value(QStringLiteral("lastWorkingDirectory"));
    if (!version || *version != currentVersion || !terminalValue.isObject()
        || !alertsValue.isBool()
        || (!directoryValue.isNull() && !directoryValue.isString())) {
        return std::nullopt;
    }

    const auto terminal = terminalValue.toObject();
    const auto familyValue = terminal.value(QStringLiteral("fontFamily"));
    const auto cursorValue = terminal.value(QStringLiteral("cursorStyle"));
    const auto fontSize = exactInteger(terminal, QStringLiteral("fontSize"));
    const auto lineHeightValue = terminal.value(QStringLiteral("lineHeight"));
    const auto scrollback = exactInteger(terminal, QStringLiteral("scrollbackLines"));
    const auto blinkValue = terminal.value(QStringLiteral("cursorBlink"));
    if (!familyValue.isString() || !cursorValue.isString() || !fontSize
        || !lineHeightValue.isDouble() || !scrollback || !blinkValue.isBool()) {
        return std::nullopt;
    }
    const auto cursor = cursorFromName(cursorValue.toString());
    if (!cursor) {
        return std::nullopt;
    }

    DesktopSettings::Values values {
        .fontFamily = familyValue.toString(),
        .fontSize = *fontSize,
        .cursorStyle = *cursor,
        .lineHeight = lineHeightValue.toDouble(),
        .scrollbackLines = *scrollback,
        .cursorBlink = blinkValue.toBool(),
        .toolApprovalAlerts = alertsValue.toBool(),
        .lastWorkingDirectory = directoryValue.isString()
            ? std::optional<QString> {directoryValue.toString()}
            : std::nullopt,
    };
    return normalized(std::move(values));
}

bool exactVariantType(const QVariant& value, const QMetaType type)
{
    return value.isValid() && value.metaType() == type;
}

} // namespace

DesktopSettings::DesktopSettings(QObject* parent)
    : DesktopSettings(std::make_unique<QSettings>(), parent)
{
}

DesktopSettings::DesktopSettings(
    std::unique_ptr<QSettings> settings,
    QObject* parent)
    : QObject(parent)
    , m_settings(std::move(settings))
{
    Q_ASSERT(m_settings != nullptr);
    load();
}

DesktopSettings::Values DesktopSettings::defaultValues()
{
    return {
        .fontFamily = QStringLiteral("JetBrains Mono"),
        .fontSize = 14,
        .cursorStyle = CursorStyle::Block,
        .lineHeight = 1.1,
        .scrollbackLines = 10'000,
        .cursorBlink = false,
        .toolApprovalAlerts = true,
        .lastWorkingDirectory = std::nullopt,
    };
}

QString DesktopSettings::fontFamily() const
{
    return m_values.fontFamily;
}

int DesktopSettings::fontSize() const noexcept
{
    return m_values.fontSize;
}

DesktopSettings::CursorStyle DesktopSettings::cursorStyle() const noexcept
{
    return m_values.cursorStyle;
}

double DesktopSettings::lineHeight() const noexcept
{
    return m_values.lineHeight;
}

int DesktopSettings::scrollbackLines() const noexcept
{
    return m_values.scrollbackLines;
}

bool DesktopSettings::cursorBlink() const noexcept
{
    return m_values.cursorBlink;
}

bool DesktopSettings::toolApprovalAlerts() const noexcept
{
    return m_values.toolApprovalAlerts;
}

QString DesktopSettings::lastWorkingDirectory() const
{
    return m_values.lastWorkingDirectory.value_or(QString {});
}

QString DesktopSettings::effectiveWorkingDirectory() const
{
    if (m_values.lastWorkingDirectory) {
        const QFileInfo candidate(*m_values.lastWorkingDirectory);
        if (candidate.exists() && candidate.isDir() && candidate.isReadable()
            && candidate.isExecutable()) {
            const auto canonical = candidate.canonicalFilePath();
            if (!canonical.isEmpty()) {
                return canonical;
            }
        }
    }
    return QDir::homePath();
}

QString DesktopSettings::settingsError() const
{
    return m_settingsError;
}

bool DesktopSettings::apply(
    const QString& fontFamily,
    const int fontSize,
    const int cursorStyle,
    const double lineHeight,
    const int scrollbackLines,
    const bool cursorBlink,
    const bool toolApprovalAlerts,
    const QString& lastWorkingDirectory)
{
    const auto directory = lastWorkingDirectory.trimmed();
    Values candidate {
        .fontFamily = fontFamily.trimmed(),
        .fontSize = fontSize,
        .cursorStyle = cursorStyle >= static_cast<int>(CursorStyle::Block)
                && cursorStyle <= static_cast<int>(CursorStyle::Underline)
            ? static_cast<CursorStyle>(cursorStyle)
            : CursorStyle::Block,
        .lineHeight = lineHeight,
        .scrollbackLines = scrollbackLines,
        .cursorBlink = cursorBlink,
        .toolApprovalAlerts = toolApprovalAlerts,
        .lastWorkingDirectory = directory.isEmpty()
            ? std::nullopt
            : std::optional<QString> {directory},
    };
    return commit(normalized(std::move(candidate)));
}

bool DesktopSettings::reset()
{
    return commit(defaultValues());
}

bool DesktopSettings::resetTerminal()
{
    auto values = m_values;
    const auto defaults = defaultValues();
    values.fontFamily = defaults.fontFamily;
    values.fontSize = defaults.fontSize;
    values.cursorStyle = defaults.cursorStyle;
    values.lineHeight = defaults.lineHeight;
    values.scrollbackLines = defaults.scrollbackLines;
    values.cursorBlink = defaults.cursorBlink;
    return commit(values);
}

void DesktopSettings::clearError()
{
    setError({});
}

void DesktopSettings::load()
{
    m_values = defaultValues();
    const auto structured = m_settings->value(QString::fromLatin1(settingsKey));
    if (structured.isValid()) {
        if (exactVariantType(
                structured,
                QMetaType::fromType<QByteArray>())) {
            const auto decoded = decode(structured.toByteArray());
            if (decoded) {
                m_values = *decoded;
                if (encode(m_values) != structured.toByteArray()) {
                    (void)persist(m_values);
                }
                return;
            }
        }
    }

    const auto hasLegacy = std::ranges::any_of(
        legacyKeys,
        [this](const QString& key) { return m_settings->contains(key); });
    if (!hasLegacy) {
        if (structured.isValid()) {
            (void)persist(m_values);
        }
        return;
    }

    auto migrated = defaultValues();
    const auto readString = [this](const QString& key, QString& destination) {
        if (!m_settings->contains(key)) {
            return;
        }
        const auto value = m_settings->value(key);
        if (exactVariantType(value, QMetaType::fromType<QString>())) {
            destination = value.toString();
        }
    };
    const auto readInt = [this](const QString& key, int& destination) {
        if (!m_settings->contains(key)) {
            return;
        }
        const auto value = m_settings->value(key);
        if (exactVariantType(value, QMetaType::fromType<int>())) {
            destination = value.toInt();
        }
    };
    const auto readDouble = [this](const QString& key, double& destination) {
        if (!m_settings->contains(key)) {
            return;
        }
        const auto value = m_settings->value(key);
        if (exactVariantType(value, QMetaType::fromType<double>())) {
            destination = value.toDouble();
        }
    };
    const auto readBool = [this](const QString& key, bool& destination) {
        if (!m_settings->contains(key)) {
            return;
        }
        const auto value = m_settings->value(key);
        if (exactVariantType(value, QMetaType::fromType<bool>())) {
            destination = value.toBool();
        }
    };

    QString cursor = cursorName(migrated.cursorStyle);
    QString directory;
    bool cursorBlinkDefaultApplied = false;
    readString(QStringLiteral("term.fontFamily"), migrated.fontFamily);
    readString(QStringLiteral("term.cursorStyle"), cursor);
    readInt(QStringLiteral("term.fontSize"), migrated.fontSize);
    readDouble(QStringLiteral("term.lineHeight"), migrated.lineHeight);
    readInt(QStringLiteral("term.scrollbackLines"), migrated.scrollbackLines);
    readBool(QStringLiteral("term.cursorBlink"), migrated.cursorBlink);
    readBool(
        QStringLiteral("term.cursorBlinkEfficientDefaultApplied"),
        cursorBlinkDefaultApplied);
    readBool(
        QStringLiteral("claude.toolApprovalAlerts"),
        migrated.toolApprovalAlerts);
    readString(QStringLiteral("session.lastWorkingDir"), directory);
    const auto parsedCursor = cursorFromName(cursor);
    migrated.cursorStyle = parsedCursor.value_or(CursorStyle::Block);
    if (!cursorBlinkDefaultApplied) {
        migrated.cursorBlink = false;
    }
    directory = directory.trimmed();
    if (!directory.isEmpty()) {
        migrated.lastWorkingDirectory = directory;
    }
    migrated = normalized(std::move(migrated));
    if (!persist(migrated)) {
        return;
    }
    m_values = std::move(migrated);
    removeLegacyValues();
}

bool DesktopSettings::persist(const Values& values)
{
    const auto key = QString::fromLatin1(settingsKey);
    const auto existed = m_settings->contains(key);
    const auto previous = m_settings->value(key);
    m_settings->setValue(key, encode(values));
    m_settings->sync();
    if (m_settings->status() == QSettings::NoError) {
        return true;
    }

    if (existed) {
        m_settings->setValue(key, previous);
    } else {
        m_settings->remove(key);
    }
    m_settings->sync();
    setError(QStringLiteral(
        "Desktop settings could not be written. The previous settings remain active."));
    return false;
}

bool DesktopSettings::commit(const Values& values)
{
    if (!validValues(values) || !persist(values)) {
        return false;
    }
    const auto changed = m_values != values;
    m_values = values;
    setError({});
    removeLegacyValues();
    if (changed) {
        emit settingsChanged();
    }
    return true;
}

void DesktopSettings::setError(QString error)
{
    if (m_settingsError == error) {
        return;
    }
    m_settingsError = std::move(error);
    emit settingsErrorChanged();
}

void DesktopSettings::removeLegacyValues()
{
    for (const auto& key : legacyKeys) {
        m_settings->remove(key);
    }
    m_settings->sync();
}

} // namespace kodosi
