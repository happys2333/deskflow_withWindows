/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/ClipboardFileBundle.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <limits>

namespace deskflow {
namespace {

constexpr char kMagic[] = {'D', 'F', 'C', 'F'};
constexpr quint16 kVersion = 1;
constexpr qsizetype kHashSize = 32;
constexpr quint64 kEntryFixedSize = 1 + 4 + 8 + 8 + kHashSize;
constexpr qint64 kCacheLifetimeMs = 24LL * 60LL * 60LL * 1000LL;
std::atomic<quint64> s_captureLimitBytes{ClipboardFileBundle::Limits{}.maxBytes};

enum class EntryType : quint8
{
  File = 0,
  Directory = 1,
};

struct Entry
{
  EntryType type;
  QString relativePath;
  qint64 modifiedMs = 0;
  QByteArray contents;
  QByteArray sha256;
};

struct DecodedBundle
{
  QUuid transferId;
  QList<Entry> entries;
  QStringList topLevelNames;
};

void setError(QString *error, const QString &message)
{
  if (error != nullptr)
    *error = message;
}

void appendUInt16(QByteArray &out, quint16 value)
{
  char bytes[sizeof(value)];
  qToBigEndian(value, reinterpret_cast<uchar *>(bytes));
  out.append(bytes, sizeof(bytes));
}

void appendUInt32(QByteArray &out, quint32 value)
{
  char bytes[sizeof(value)];
  qToBigEndian(value, reinterpret_cast<uchar *>(bytes));
  out.append(bytes, sizeof(bytes));
}

void appendUInt64(QByteArray &out, quint64 value)
{
  char bytes[sizeof(value)];
  qToBigEndian(value, reinterpret_cast<uchar *>(bytes));
  out.append(bytes, sizeof(bytes));
}

class Reader final
{
public:
  explicit Reader(const QByteArray &data) : m_data(data)
  {
  }

  bool readRaw(qsizetype size, QByteArray &value)
  {
    if (size < 0 || size > remaining())
      return false;
    value = QByteArray(m_data.constData() + m_offset, size);
    m_offset += size;
    return true;
  }

  bool readUInt8(quint8 &value)
  {
    if (remaining() < 1)
      return false;
    value = static_cast<quint8>(m_data.at(m_offset++));
    return true;
  }

  bool readUInt16(quint16 &value)
  {
    return readNumber(value);
  }

  bool readUInt32(quint32 &value)
  {
    return readNumber(value);
  }

  bool readUInt64(quint64 &value)
  {
    return readNumber(value);
  }

  qsizetype remaining() const
  {
    return m_data.size() - m_offset;
  }

private:
  template <class T> bool readNumber(T &value)
  {
    if (remaining() < static_cast<qsizetype>(sizeof(T)))
      return false;
    value = qFromBigEndian<T>(reinterpret_cast<const uchar *>(m_data.constData() + m_offset));
    m_offset += sizeof(T);
    return true;
  }

  const QByteArray &m_data;
  qsizetype m_offset = 0;
};

QString sanitizeSegment(QString segment)
{
  segment = segment.normalized(QString::NormalizationForm_C);
  static const QString invalidCharacters = QStringLiteral("<>:\"/\\|?*");
  for (auto &character : segment) {
    if (character.unicode() < 32 || invalidCharacters.contains(character))
      character = QLatin1Char('_');
  }

  while (segment.endsWith(QLatin1Char(' ')) || segment.endsWith(QLatin1Char('.')))
    segment.chop(1);

  if (segment.isEmpty() || segment == QStringLiteral(".") || segment == QStringLiteral(".."))
    segment = QStringLiteral("_");

  static const QSet<QString> reservedNames = {
      QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),  QStringLiteral("NUL"),
      QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
      QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
      QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
      QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
      QStringLiteral("LPT8"), QStringLiteral("LPT9"),
  };
  if (reservedNames.contains(segment.section(QLatin1Char('.'), 0, 0).toUpper()))
    segment.prepend(QLatin1Char('_'));

  return segment;
}

bool isSafeRelativePath(const QString &path)
{
  if (path.isEmpty() || path.contains(QLatin1Char('\\')) || QDir::isAbsolutePath(path) ||
      QDir::cleanPath(path) != path) {
    return false;
  }

  const auto segments = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
  if (segments.isEmpty())
    return false;
  for (const auto &segment : segments) {
    if (segment.isEmpty() || sanitizeSegment(segment) != segment)
      return false;
  }
  return true;
}

QString uniqueSegment(const QString &rawName, QSet<QString> &used)
{
  const QString base = sanitizeSegment(rawName);
  QString candidate = base;
  for (int suffix = 2; used.contains(candidate.toCaseFolded()); ++suffix)
    candidate = QStringLiteral("%1 (%2)").arg(base).arg(suffix);
  used.insert(candidate.toCaseFolded());
  return candidate;
}

bool reserveSize(quint64 amount, quint64 &total, const ClipboardFileBundle::Limits &limits, QString *error)
{
  if (amount > limits.maxBytes || total > limits.maxBytes - amount) {
    setError(error, QStringLiteral("clipboard files exceed the configured size limit"));
    return false;
  }
  total += amount;
  return true;
}

bool appendEntry(
    const QFileInfo &info, const QString &relativePath, quint32 depth, QList<Entry> &entries, quint64 &total,
    const ClipboardFileBundle::Limits &limits, QString *error
)
{
  if (depth > limits.maxDepth) {
    setError(error, QStringLiteral("clipboard directory exceeds the maximum depth"));
    return false;
  }
  if (info.isSymLink()) {
    setError(error, QStringLiteral("symbolic links are not supported: %1").arg(info.filePath()));
    return false;
  }
  if (!info.isDir() && !info.isFile()) {
    setError(error, QStringLiteral("unsupported clipboard file type: %1").arg(info.filePath()));
    return false;
  }
  if (entries.size() >= limits.maxItems) {
    setError(error, QStringLiteral("clipboard contains too many files"));
    return false;
  }

  const QByteArray encodedPath = relativePath.toUtf8();
  if (!reserveSize(kEntryFixedSize + static_cast<quint64>(encodedPath.size()), total, limits, error))
    return false;

  Entry entry;
  entry.type = info.isDir() ? EntryType::Directory : EntryType::File;
  entry.relativePath = relativePath;
  entry.modifiedMs = info.lastModified().toMSecsSinceEpoch();

  if (info.isFile()) {
    const auto size = info.size();
    if (size < 0 || size > std::numeric_limits<int>::max() ||
        !reserveSize(static_cast<quint64>(size), total, limits, error)) {
      if (error != nullptr && error->isEmpty())
        setError(error, QStringLiteral("clipboard file is too large: %1").arg(info.filePath()));
      return false;
    }

    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) {
      setError(error, QStringLiteral("failed to read clipboard file: %1").arg(info.filePath()));
      return false;
    }
    entry.contents = file.readAll();
    if (entry.contents.size() != size) {
      setError(error, QStringLiteral("clipboard file changed while being read: %1").arg(info.filePath()));
      return false;
    }
    entry.sha256 = QCryptographicHash::hash(entry.contents, QCryptographicHash::Sha256);
  } else {
    entry.sha256 = QByteArray(kHashSize, '\0');
  }
  entries.append(std::move(entry));

  if (!info.isDir())
    return true;

  QDir directory(info.filePath());
  const auto children = directory.entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::DirsFirst | QDir::Name
  );
  QSet<QString> usedNames;
  for (const auto &child : children) {
    const QString childName = uniqueSegment(child.fileName(), usedNames);
    if (!appendEntry(child, relativePath + QLatin1Char('/') + childName, depth + 1, entries, total, limits, error)) {
      return false;
    }
  }
  return true;
}

bool decode(const QByteArray &bundle, const ClipboardFileBundle::Limits &limits, DecodedBundle &decoded, QString *error)
{
  if (bundle.size() < 4 || static_cast<quint64>(bundle.size()) > limits.maxBytes) {
    setError(error, QStringLiteral("clipboard file bundle has an invalid size"));
    return false;
  }

  Reader reader(bundle);
  QByteArray magic;
  quint16 version = 0;
  QByteArray uuidBytes;
  quint32 itemCount = 0;
  if (!reader.readRaw(4, magic) || magic != QByteArray(kMagic, 4) || !reader.readUInt16(version) ||
      version != kVersion || !reader.readRaw(16, uuidBytes) || !reader.readUInt32(itemCount) || itemCount == 0 ||
      itemCount > limits.maxItems) {
    setError(error, QStringLiteral("clipboard file bundle header is invalid"));
    return false;
  }

  decoded.transferId = QUuid::fromRfc4122(uuidBytes);
  if (decoded.transferId.isNull()) {
    setError(error, QStringLiteral("clipboard file bundle transfer id is invalid"));
    return false;
  }

  QHash<QString, QString> canonicalPaths;
  QSet<QString> entryPaths;
  QSet<QString> topLevelKeys;
  for (quint32 index = 0; index < itemCount; ++index) {
    quint8 rawType = 0;
    quint32 pathSize = 0;
    QByteArray pathBytes;
    quint64 modifiedMs = 0;
    quint64 contentSize = 0;
    QByteArray sha256;
    QByteArray contents;
    if (!reader.readUInt8(rawType) || rawType > static_cast<quint8>(EntryType::Directory) ||
        !reader.readUInt32(pathSize) || pathSize == 0 || pathSize > static_cast<quint32>(reader.remaining()) ||
        !reader.readRaw(pathSize, pathBytes) || !reader.readUInt64(modifiedMs) || !reader.readUInt64(contentSize) ||
        !reader.readRaw(kHashSize, sha256) || contentSize > static_cast<quint64>(reader.remaining()) ||
        contentSize > static_cast<quint64>(std::numeric_limits<int>::max()) ||
        !reader.readRaw(static_cast<qsizetype>(contentSize), contents)) {
      setError(error, QStringLiteral("clipboard file bundle entry is truncated"));
      return false;
    }

    const QString relativePath = QString::fromUtf8(pathBytes);
    if (relativePath.toUtf8() != pathBytes || !isSafeRelativePath(relativePath)) {
      setError(error, QStringLiteral("clipboard file bundle contains an unsafe or duplicate path"));
      return false;
    }

    QString prefix;
    for (const auto &segment : relativePath.split(QLatin1Char('/'))) {
      prefix = prefix.isEmpty() ? segment : prefix + QLatin1Char('/') + segment;
      const QString key = prefix.toCaseFolded();
      if (canonicalPaths.contains(key) && canonicalPaths.value(key) != prefix) {
        setError(error, QStringLiteral("clipboard file bundle contains paths that collide on another platform"));
        return false;
      }
      canonicalPaths.insert(key, prefix);
    }

    const auto type = static_cast<EntryType>(rawType);
    const QString entryPath = relativePath.toCaseFolded();
    if (entryPaths.contains(entryPath)) {
      setError(error, QStringLiteral("clipboard file bundle contains an unsafe or duplicate path"));
      return false;
    }
    entryPaths.insert(entryPath);

    if ((type == EntryType::Directory && contentSize != 0) ||
        (type == EntryType::File && QCryptographicHash::hash(contents, QCryptographicHash::Sha256) != sha256)) {
      setError(error, QStringLiteral("clipboard file bundle integrity check failed"));
      return false;
    }

    const quint32 depth = static_cast<quint32>(relativePath.count(QLatin1Char('/')));
    if (depth > limits.maxDepth) {
      setError(error, QStringLiteral("clipboard file bundle exceeds the maximum depth"));
      return false;
    }

    Entry entry{type, relativePath, static_cast<qint64>(modifiedMs), std::move(contents), std::move(sha256)};
    decoded.entries.append(std::move(entry));
    const QString topLevelName = relativePath.section(QLatin1Char('/'), 0, 0);
    const QString topLevelKey = topLevelName.toCaseFolded();
    if (!topLevelKeys.contains(topLevelKey)) {
      decoded.topLevelNames.append(topLevelName);
      topLevelKeys.insert(topLevelKey);
    }
  }

  if (reader.remaining() != 0) {
    setError(error, QStringLiteral("clipboard file bundle contains trailing data"));
    return false;
  }

  decoded.topLevelNames.sort(Qt::CaseSensitive);
  return true;
}

void cleanupExpired(const QString &cacheRoot)
{
  QDir root(cacheRoot);
  if (!root.exists())
    return;

  const auto cutoff = QDateTime::currentDateTimeUtc().addMSecs(-kCacheLifetimeMs);
  const auto entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  for (const auto &entry : entries) {
    if (entry.lastModified().toUTC() < cutoff) {
      QDir(entry.filePath()).removeRecursively();
      QFile::remove(entry.filePath() + QStringLiteral(".complete"));
    }
  }
}

QStringList materializedTopLevelPaths(const QString &bundleRoot, const QStringList &names)
{
  QStringList paths;
  paths.reserve(names.size());
  for (const auto &name : names)
    paths.append(QDir(bundleRoot).filePath(name));
  return paths;
}

QStringList pathsFromUriList(const QByteArray &uriList)
{
  QStringList paths;
  QSet<QString> seen;
  const auto lines = uriList.split('\n');
  for (QByteArray line : lines) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith('#') || line == "copy" || line == "cut")
      continue;

    const QUrl url = QUrl::fromEncoded(line, QUrl::StrictMode);
    const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
    if (!path.isEmpty() && !seen.contains(path)) {
      paths.append(path);
      seen.insert(path);
    }
  }
  return paths;
}

} // namespace

QByteArray ClipboardFileBundle::fromPaths(const QStringList &paths, const Limits &limits, QString *error)
{
  if (error != nullptr)
    error->clear();
  if (paths.isEmpty()) {
    setError(error, QStringLiteral("clipboard file list is empty"));
    return {};
  }

  QList<Entry> entries;
  quint64 total = 4 + 2 + 16 + 4;
  QSet<QString> usedTopLevelNames;
  for (const auto &path : paths) {
    QFileInfo info(path);
    if (!info.exists()) {
      setError(error, QStringLiteral("clipboard file does not exist: %1").arg(path));
      return {};
    }
    const QString relativePath = uniqueSegment(info.fileName(), usedTopLevelNames);
    if (!appendEntry(info, relativePath, 0, entries, total, limits, error))
      return {};
  }

  QByteArray out;
  out.reserve(static_cast<qsizetype>(total));
  out.append(kMagic, 4);
  appendUInt16(out, kVersion);
  out.append(QUuid::createUuid().toRfc4122());
  appendUInt32(out, static_cast<quint32>(entries.size()));
  for (const auto &entry : entries) {
    const QByteArray path = entry.relativePath.toUtf8();
    out.append(static_cast<char>(entry.type));
    appendUInt32(out, static_cast<quint32>(path.size()));
    out.append(path);
    appendUInt64(out, static_cast<quint64>(entry.modifiedMs));
    appendUInt64(out, static_cast<quint64>(entry.contents.size()));
    out.append(entry.sha256);
    out.append(entry.contents);
  }
  return out;
}

QByteArray ClipboardFileBundle::fromPaths(const QStringList &paths, QString *error)
{
  return fromPaths(paths, Limits{}, error);
}

QStringList ClipboardFileBundle::materialize(
    const QByteArray &bundle, const QString &cacheRoot, const Limits &limits, QString *error
)
{
  if (error != nullptr)
    error->clear();

  DecodedBundle decoded;
  if (!decode(bundle, limits, decoded, error))
    return {};

  const QString root = cacheRoot.isEmpty() ? defaultCacheRoot() : cacheRoot;
  if (!QDir().mkpath(root)) {
    setError(error, QStringLiteral("failed to create clipboard cache directory"));
    return {};
  }
  cleanupExpired(root);

  const QString transferName = decoded.transferId.toString(QUuid::WithoutBraces);
  const QString bundleRoot = QDir(root).filePath(transferName);
  const QString completeMarker = bundleRoot + QStringLiteral(".complete");
  const QByteArray bundleHash = QCryptographicHash::hash(bundle, QCryptographicHash::Sha256).toHex();
  if (QFile marker(completeMarker); marker.open(QIODevice::ReadOnly) && marker.readAll() == bundleHash) {
    const auto paths = materializedTopLevelPaths(bundleRoot, decoded.topLevelNames);
    if (std::all_of(paths.cbegin(), paths.cend(), [](const QString &path) { return QFileInfo::exists(path); }))
      return paths;
  }

  if (QFileInfo::exists(bundleRoot) && !QDir(bundleRoot).removeRecursively()) {
    setError(error, QStringLiteral("failed to clear incomplete clipboard cache"));
    return {};
  }
  QFile::remove(completeMarker);

  const QString stagingRoot = bundleRoot + QStringLiteral(".tmp-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!QDir().mkpath(stagingRoot)) {
    setError(error, QStringLiteral("failed to create clipboard staging directory"));
    return {};
  }

  auto fail = [&](const QString &message) {
    QDir(stagingRoot).removeRecursively();
    setError(error, message);
    return QStringList{};
  };

  for (const auto &entry : decoded.entries) {
    const QString path = QDir(stagingRoot).filePath(entry.relativePath);
    if (entry.type == EntryType::Directory) {
      if (!QDir().mkpath(path))
        return fail(QStringLiteral("failed to create clipboard directory: %1").arg(entry.relativePath));
      continue;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
      return fail(QStringLiteral("failed to create clipboard file parent directory"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(entry.contents) != entry.contents.size() || !file.commit())
      return fail(QStringLiteral("failed to materialize clipboard file: %1").arg(entry.relativePath));

    QFile timestampFile(path);
    if (timestampFile.open(QIODevice::ReadOnly)) {
      timestampFile.setFileTime(QDateTime::fromMSecsSinceEpoch(entry.modifiedMs), QFileDevice::FileModificationTime);
      timestampFile.close();
    }
  }

  if (!QDir().rename(stagingRoot, bundleRoot))
    return fail(QStringLiteral("failed to publish clipboard cache"));

  QSaveFile marker(completeMarker);
  if (!marker.open(QIODevice::WriteOnly) || marker.write(bundleHash) != bundleHash.size() || !marker.commit()) {
    QDir(bundleRoot).removeRecursively();
    setError(error, QStringLiteral("failed to finalize clipboard cache"));
    return {};
  }

  return materializedTopLevelPaths(bundleRoot, decoded.topLevelNames);
}

QStringList ClipboardFileBundle::materialize(const QByteArray &bundle, const QString &cacheRoot, QString *error)
{
  return materialize(bundle, cacheRoot, Limits{}, error);
}

QByteArray ClipboardFileBundle::fromUriList(const QByteArray &uriList, const Limits &limits, QString *error)
{
  const auto paths = pathsFromUriList(uriList);
  if (paths.isEmpty()) {
    setError(error, QStringLiteral("clipboard URI list contains no local files"));
    return {};
  }
  return fromPaths(paths, limits, error);
}

QByteArray ClipboardFileBundle::fromUriList(const QByteArray &uriList, QString *error)
{
  return fromUriList(uriList, Limits{}, error);
}

QByteArray
ClipboardFileBundle::toUriList(const QByteArray &bundle, bool gnomeFormat, const QString &cacheRoot, QString *error)
{
  const auto paths = materialize(bundle, cacheRoot, captureLimits(), error);
  if (paths.isEmpty())
    return {};

  QByteArrayList urls;
  urls.reserve(paths.size());
  for (const auto &path : paths)
    urls.append(QUrl::fromLocalFile(path).toEncoded(QUrl::FullyEncoded));

  if (gnomeFormat)
    return QByteArrayLiteral("copy\n") + urls.join('\n') + '\n';
  return urls.join("\r\n") + "\r\n";
}

QString ClipboardFileBundle::defaultCacheRoot()
{
  QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (base.isEmpty())
    base = QDir::tempPath() + QStringLiteral("/deskflow");
  return QDir(base).filePath(QStringLiteral("ClipboardFiles"));
}

ClipboardFileBundle::Limits ClipboardFileBundle::captureLimits()
{
  Limits limits;
  limits.maxBytes = s_captureLimitBytes.load(std::memory_order_relaxed);
  return limits;
}

void ClipboardFileBundle::setCaptureLimitBytes(quint64 maxBytes)
{
  s_captureLimitBytes.store(std::min(maxBytes, Limits{}.maxBytes), std::memory_order_relaxed);
}

} // namespace deskflow
