/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MainWindowTests.h"

#include "common/Settings.h"
#include "gui/MainWindow.h"
#include "gui/widgets/LogDock.h"

#include <QMetaObject>

void MainWindowTests::initTestCase()
{
  QVERIFY(m_settingsDir.isValid());

  Settings::setSettingsFile(m_settingsDir.filePath(QStringLiteral("Deskflow.conf")));
  Settings::setStateFile(m_settingsDir.filePath(QStringLiteral("Deskflow.state")));
  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("main-window-test"));
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Desktop);
  Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("127.0.0.1"));
  Settings::setValue(Settings::Gui::AutoStartCore, false);
  Settings::setValue(Settings::Gui::LogExpanded, false);
  Settings::setValue(Settings::Security::TlsEnabled, false);
}

void MainWindowTests::diagnosticReceivedWhileHiddenRevealsLogWhenShown()
{
  MainWindow window(QCoreApplication::applicationFilePath());
  auto *logDock = window.findChild<LogDock *>();
  QVERIFY(logDock != nullptr);
  QVERIFY(logDock->isHidden());

  QVERIFY(
      QMetaObject::invokeMethod(
          &window, "handleLogLine", Qt::DirectConnection,
          Q_ARG(QString, QStringLiteral("WARNING: failed to connect to server: Timed out"))
      )
  );
  QVERIFY(logDock->isHidden());

  window.show();
  QTRY_VERIFY_WITH_TIMEOUT(!logDock->isHidden(), 1000);
}

QTEST_MAIN(MainWindowTests)
