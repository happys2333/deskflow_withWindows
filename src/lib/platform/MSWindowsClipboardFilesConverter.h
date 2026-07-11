/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

class MSWindowsClipboardFilesConverter : public IMSWindowsClipboardConverter
{
public:
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &data) const override;
  std::string toIClipboard(HANDLE data) const override;
};
