/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXClipboardTests.h"

#include "common/ClipboardFileBundle.h"
#include "platform/OSXClipboard.h"
#include "platform/OSXClipboardUTF8Converter.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

void OSXClipboardTests::open()
{
  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.close();
}

void OSXClipboardTests::singleFormat()
{
  using enum IClipboard::Format;

  OSXClipboard clipboard;
  QVERIFY(clipboard.empty());
  clipboard.add(Text, m_testString);
  QVERIFY(clipboard.has(Text));
  QCOMPARE(clipboard.get(Text), m_testString);
}

void OSXClipboardTests::filesRoundTrip()
{
  QTemporaryDir source;
  QTemporaryDir cache;
  const QString sourceFile = QDir(source.path()).filePath(QStringLiteral("mac file.txt"));
  QFile file(sourceFile);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write("mac clipboard"), 13);
  file.close();

  QString error;
  const auto bundle = deskflow::ClipboardFileBundle::fromPaths({sourceFile}, &error);
  QVERIFY2(!bundle.isEmpty(), qPrintable(error));

  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.add(IClipboard::Format::Files, bundle.toStdString());
  QVERIFY(clipboard.has(IClipboard::Format::Files));
  const auto captured = QByteArray::fromStdString(clipboard.get(IClipboard::Format::Files));
  clipboard.close();

  const auto paths = deskflow::ClipboardFileBundle::materialize(captured, cache.path(), &error);
  QCOMPARE(paths.size(), 1);
  QFile received(paths.first());
  QVERIFY(received.open(QIODevice::ReadOnly));
  QCOMPARE(received.readAll(), QByteArrayLiteral("mac clipboard"));
}

void OSXClipboardTests::formatConvert_UTF8()
{
  OSXClipboardUTF8Converter converter;
  QCOMPARE(IClipboard::Format::Text, converter.getFormat());
  QCOMPARE(converter.getOSXFormat(), CFSTR("public.utf8-plain-text"));
  QCOMPARE(converter.fromIClipboard("test data\n"), "test data\r");
  QCOMPARE(converter.toIClipboard("test data\r"), "test data\n");
}

QTEST_MAIN(OSXClipboardTests)
