/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class ClipboardFileBundleTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void singleFileRoundTrip();
  void directoryRoundTrip();
  void uriListRoundTrip();
  void rejectsPathTraversal();
  void rejectsCorruptContents();
  void enforcesSizeLimit();
  void negotiatedCaptureLimitIsClamped();
  void avoidsCaseInsensitiveNameCollisions();
  void cacheIdentityIncludesBundleContents();
  void preservesCompleteMarkerFilename();
  void rejectsDuplicatePaths();
  void rejectsEveryTruncatedPrefix();
};
