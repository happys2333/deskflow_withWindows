/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace deskflow {

class ClipboardFileBundle final
{
public:
  struct Limits
  {
    quint64 maxBytes = 512ULL * 1024ULL * 1024ULL;
    quint32 maxItems = 4096;
    quint32 maxDepth = 32;
  };

  static QByteArray fromPaths(const QStringList &paths, const Limits &limits, QString *error = nullptr);
  static QByteArray fromPaths(const QStringList &paths, QString *error = nullptr);

  static QStringList
  materialize(const QByteArray &bundle, const QString &cacheRoot, const Limits &limits, QString *error = nullptr);
  static QStringList materialize(const QByteArray &bundle, const QString &cacheRoot = {}, QString *error = nullptr);

  static QByteArray fromUriList(const QByteArray &uriList, const Limits &limits, QString *error = nullptr);
  static QByteArray fromUriList(const QByteArray &uriList, QString *error = nullptr);
  static QByteArray
  toUriList(const QByteArray &bundle, bool gnomeFormat, const QString &cacheRoot = {}, QString *error = nullptr);

  static Limits captureLimits();
  static void setCaptureLimitBytes(quint64 maxBytes);
  static QString defaultCacheRoot();
};

} // namespace deskflow
