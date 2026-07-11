/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/XWindowsClipboardFilesConverter.h"
#include "common/ClipboardFileBundle.h"

XWindowsClipboardFilesConverter::XWindowsClipboardFilesConverter(Display *display, const char *name, bool gnomeFormat)
    : m_atom(XInternAtom(display, name, False)),
      m_gnomeFormat(gnomeFormat)
{
}

IClipboard::Format XWindowsClipboardFilesConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

Atom XWindowsClipboardFilesConverter::getAtom() const
{
  return m_atom;
}

int XWindowsClipboardFilesConverter::getDataSize() const
{
  return 8;
}

std::string XWindowsClipboardFilesConverter::fromIClipboard(const std::string &data) const
{
  const QByteArray bundle(data.data(), static_cast<qsizetype>(data.size()));
  const auto uriList = deskflow::ClipboardFileBundle::toUriList(bundle, m_gnomeFormat);
  return uriList.toStdString();
}

std::string XWindowsClipboardFilesConverter::toIClipboard(const std::string &data) const
{
  const QByteArray uriList(data.data(), static_cast<qsizetype>(data.size()));
  const auto bundle =
      deskflow::ClipboardFileBundle::fromUriList(uriList, deskflow::ClipboardFileBundle::captureLimits(), nullptr);
  return std::string(bundle.constData(), static_cast<size_t>(bundle.size()));
}
