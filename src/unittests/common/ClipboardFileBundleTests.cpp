/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardFileBundleTests.h"

#include "common/ClipboardFileBundle.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

#include <limits>

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(contents), contents.size());
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}

} // namespace

void ClipboardFileBundleTests::singleFileRoundTrip()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  QVERIFY(source.isValid());
  QVERIFY(cache.isValid());
  const QString sourceFile = QDir(source.path()).filePath(QStringLiteral("hello.txt"));
  writeFile(sourceFile, QByteArrayLiteral("hello from deskflow"));

  QString error;
  const auto bundle = deskflow::ClipboardFileBundle::fromPaths({sourceFile}, &error);
  QVERIFY2(!bundle.isEmpty(), qPrintable(error));
  const auto paths = deskflow::ClipboardFileBundle::materialize(bundle, cache.path(), &error);

  QCOMPARE(paths.size(), 1);
  QCOMPARE(QFileInfo(paths.first()).fileName(), QStringLiteral("hello.txt"));
  QCOMPARE(readFile(paths.first()), QByteArrayLiteral("hello from deskflow"));
}

void ClipboardFileBundleTests::directoryRoundTrip()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  const QString folder = QDir(source.path()).filePath(QStringLiteral("folder"));
  QVERIFY(QDir().mkpath(QDir(folder).filePath(QStringLiteral("nested"))));
  writeFile(QDir(folder).filePath(QStringLiteral("root.txt")), QByteArrayLiteral("root"));
  writeFile(QDir(folder).filePath(QStringLiteral("nested/child.txt")), QByteArrayLiteral("child"));

  QString error;
  const auto bundle = deskflow::ClipboardFileBundle::fromPaths({folder}, &error);
  const auto paths = deskflow::ClipboardFileBundle::materialize(bundle, cache.path(), &error);

  QCOMPARE(paths.size(), 1);
  QCOMPARE(readFile(QDir(paths.first()).filePath(QStringLiteral("root.txt"))), QByteArrayLiteral("root"));
  QCOMPARE(readFile(QDir(paths.first()).filePath(QStringLiteral("nested/child.txt"))), QByteArrayLiteral("child"));
}

void ClipboardFileBundleTests::uriListRoundTrip()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  const QString sourceFile = QDir(source.path()).filePath(QStringLiteral("uri file.txt"));
  writeFile(sourceFile, QByteArrayLiteral("uri"));
  const QByteArray uriList = QUrl::fromLocalFile(sourceFile).toEncoded() + "\r\n";

  QString error;
  const auto bundle = deskflow::ClipboardFileBundle::fromUriList(uriList, &error);
  QVERIFY2(!bundle.isEmpty(), qPrintable(error));
  const auto output = deskflow::ClipboardFileBundle::toUriList(bundle, false, cache.path(), &error);

  QVERIFY2(!output.isEmpty(), qPrintable(error));
  const QUrl outputUrl = QUrl::fromEncoded(output.trimmed());
  QCOMPARE(readFile(outputUrl.toLocalFile()), QByteArrayLiteral("uri"));
}

void ClipboardFileBundleTests::rejectsPathTraversal()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  const QString sourceFile = QDir(source.path()).filePath(QStringLiteral("good.txt"));
  writeFile(sourceFile, QByteArrayLiteral("data"));
  auto bundle = deskflow::ClipboardFileBundle::fromPaths({sourceFile});
  const auto pathOffset = bundle.indexOf("good.txt");
  QVERIFY(pathOffset > 0);
  bundle.replace(pathOffset, 8, "../x.txt");

  QString error;
  QVERIFY(deskflow::ClipboardFileBundle::materialize(bundle, cache.path(), &error).isEmpty());
  QVERIFY(error.contains(QStringLiteral("unsafe")));
  QVERIFY(!QFileInfo::exists(QDir(cache.path()).filePath(QStringLiteral("../x.txt"))));
}

void ClipboardFileBundleTests::rejectsCorruptContents()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  const QString sourceFile = QDir(source.path()).filePath(QStringLiteral("file.txt"));
  writeFile(sourceFile, QByteArrayLiteral("data"));
  auto bundle = deskflow::ClipboardFileBundle::fromPaths({sourceFile});
  bundle[bundle.size() - 1] ^= 1;

  QString error;
  QVERIFY(deskflow::ClipboardFileBundle::materialize(bundle, cache.path(), &error).isEmpty());
  QVERIFY(error.contains(QStringLiteral("integrity")));
}

void ClipboardFileBundleTests::enforcesSizeLimit()
{
  QTemporaryDir source;
  const QString sourceFile = QDir(source.path()).filePath(QStringLiteral("large.bin"));
  writeFile(sourceFile, QByteArray(1024, 'x'));
  deskflow::ClipboardFileBundle::Limits limits;
  limits.maxBytes = 128;

  QString error;
  QVERIFY(deskflow::ClipboardFileBundle::fromPaths({sourceFile}, limits, &error).isEmpty());
  QVERIFY(error.contains(QStringLiteral("size limit")));
}

void ClipboardFileBundleTests::negotiatedCaptureLimitIsClamped()
{
  constexpr quint64 hardLimit = 512ULL * 1024ULL * 1024ULL;
  deskflow::ClipboardFileBundle::setCaptureLimitBytes(1024);
  QCOMPARE(deskflow::ClipboardFileBundle::captureLimits().maxBytes, quint64(1024));

  deskflow::ClipboardFileBundle::setCaptureLimitBytes(std::numeric_limits<quint64>::max());
  QCOMPARE(deskflow::ClipboardFileBundle::captureLimits().maxBytes, hardLimit);
}

void ClipboardFileBundleTests::avoidsCaseInsensitiveNameCollisions()
{
  QTemporaryDir firstSource;
  QTemporaryDir secondSource;
  QTemporaryDir cache;
  const QString upper = QDir(firstSource.path()).filePath(QStringLiteral("File.txt"));
  const QString lower = QDir(secondSource.path()).filePath(QStringLiteral("file.txt"));
  writeFile(upper, QByteArrayLiteral("upper"));
  writeFile(lower, QByteArrayLiteral("lower"));

  QString error;
  const auto bundle = deskflow::ClipboardFileBundle::fromPaths({upper, lower}, &error);
  const auto paths = deskflow::ClipboardFileBundle::materialize(bundle, cache.path(), &error);

  QCOMPARE(paths.size(), 2);
  QVERIFY(paths.at(0).compare(paths.at(1), Qt::CaseInsensitive) != 0);
  QSet<QByteArray> contents{readFile(paths.at(0)), readFile(paths.at(1))};
  QCOMPARE(contents, QSet<QByteArray>({QByteArrayLiteral("upper"), QByteArrayLiteral("lower")}));
}

void ClipboardFileBundleTests::cacheIdentityIncludesBundleContents()
{
  QTemporaryDir firstSource;
  QTemporaryDir secondSource;
  QTemporaryDir cache;
  const QString first = QDir(firstSource.path()).filePath(QStringLiteral("file.txt"));
  const QString second = QDir(secondSource.path()).filePath(QStringLiteral("file.txt"));
  writeFile(first, QByteArrayLiteral("first"));
  writeFile(second, QByteArrayLiteral("second"));

  auto firstBundle = deskflow::ClipboardFileBundle::fromPaths({first});
  auto secondBundle = deskflow::ClipboardFileBundle::fromPaths({second});
  secondBundle.replace(6, 16, firstBundle.mid(6, 16));

  QString error;
  auto paths = deskflow::ClipboardFileBundle::materialize(firstBundle, cache.path(), &error);
  QCOMPARE(readFile(paths.first()), QByteArrayLiteral("first"));
  paths = deskflow::ClipboardFileBundle::materialize(secondBundle, cache.path(), &error);
  QCOMPARE(readFile(paths.first()), QByteArrayLiteral("second"));
}

void ClipboardFileBundleTests::preservesCompleteMarkerFilename()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  const QString sourceFile = QDir(source.path()).filePath(QStringLiteral(".complete"));
  writeFile(sourceFile, QByteArrayLiteral("user data"));

  QString error;
  const auto bundle = deskflow::ClipboardFileBundle::fromPaths({sourceFile}, &error);
  const auto paths = deskflow::ClipboardFileBundle::materialize(bundle, cache.path(), &error);

  QCOMPARE(paths.size(), 1);
  QCOMPARE(QFileInfo(paths.first()).fileName(), QStringLiteral(".complete"));
  QCOMPARE(readFile(paths.first()), QByteArrayLiteral("user data"));
}

void ClipboardFileBundleTests::rejectsDuplicatePaths()
{
  QTemporaryDir firstSource;
  QTemporaryDir secondSource;
  QTemporaryDir cache;
  const QString first = QDir(firstSource.path()).filePath(QStringLiteral("a.txt"));
  const QString second = QDir(secondSource.path()).filePath(QStringLiteral("b.txt"));
  writeFile(first, QByteArrayLiteral("same"));
  writeFile(second, QByteArrayLiteral("same"));

  auto bundle = deskflow::ClipboardFileBundle::fromPaths({first, second});
  const auto secondPath = bundle.indexOf("b.txt");
  QVERIFY(secondPath > 0);
  bundle.replace(secondPath, 5, "a.txt");

  QString error;
  QVERIFY(deskflow::ClipboardFileBundle::materialize(bundle, cache.path(), &error).isEmpty());
  QVERIFY(error.contains(QStringLiteral("duplicate")));
}

void ClipboardFileBundleTests::rejectsEveryTruncatedPrefix()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  const QString sourceFile = QDir(source.path()).filePath(QStringLiteral("file.txt"));
  writeFile(sourceFile, QByteArrayLiteral("contents"));
  const auto bundle = deskflow::ClipboardFileBundle::fromPaths({sourceFile});

  for (qsizetype size = 0; size < bundle.size(); ++size) {
    QString error;
    QVERIFY2(
        deskflow::ClipboardFileBundle::materialize(bundle.left(size), cache.path(), &error).isEmpty(),
        qPrintable(QStringLiteral("accepted truncated bundle of %1 bytes").arg(size))
    );
  }
}

QTEST_MAIN(ClipboardFileBundleTests)
