/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreProcessTests.h"

#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/core/CoreProcess.h"

#include <QMetaObject>
#include <QSignalSpy>

using namespace deskflow::core;
using namespace deskflow::gui;

void CoreProcessTests::initTestCase()
{
  QVERIFY(m_settingsDir.isValid());

  Settings::setSettingsFile(m_settingsDir.filePath(QStringLiteral("Deskflow.conf")));
  Settings::setStateFile(m_settingsDir.filePath(QStringLiteral("Deskflow.state")));
  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("core-process-test"));
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Desktop);
  Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("127.0.0.1"));
  Settings::setValue(Settings::Security::TlsEnabled, false);
}

void CoreProcessTests::retryMessageTransitionsToConnecting()
{
  ServerConfig serverConfig;
  CoreProcess process(serverConfig, QCoreApplication::applicationFilePath());
  QSignalSpy retrySpy(&process, &CoreProcess::retryIn);
  QSignalSpy connectionSpy(&process, &CoreProcess::connectionStateChanged);

  QVERIFY(
      QMetaObject::invokeMethod(
          &process, "onCoreIpcMessageReceived", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("retryIn")),
          Q_ARG(QString, QStringLiteral("7"))
      )
  );

  QCOMPARE(retrySpy.count(), 1);
  QCOMPARE(retrySpy.constFirst().constFirst().toInt(), 7);
  QCOMPARE(process.connectionState(), ConnectionState::Connecting);
  QCOMPARE(connectionSpy.count(), 1);
}

QTEST_MAIN(CoreProcessTests)
