/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QTemporaryDir>
#include <QtTest>

class CoreProcessTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void retryMessageTransitionsToConnecting();

private:
  QTemporaryDir m_settingsDir;
};
