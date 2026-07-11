/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFilesConverter.h"

#include "base/Log.h"
#include "common/ClipboardFileBundle.h"

#include <QDir>

#include <Shellapi.h>
#include <ShlObj_core.h>

#include <cstring>
#include <limits>
#include <vector>

IClipboard::Format MSWindowsClipboardFilesConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

UINT MSWindowsClipboardFilesConverter::getWin32Format() const
{
  return CF_HDROP;
}

HANDLE MSWindowsClipboardFilesConverter::fromIClipboard(const std::string &data) const
{
  QString error;
  const QByteArray bundle(data.data(), static_cast<qsizetype>(data.size()));
  const auto paths =
      deskflow::ClipboardFileBundle::materialize(bundle, {}, deskflow::ClipboardFileBundle::captureLimits(), &error);
  if (paths.isEmpty()) {
    LOG_WARN("failed to materialize clipboard files: %s", error.toUtf8().constData());
    return nullptr;
  }

  std::vector<std::wstring> nativePaths;
  SIZE_T bytes = sizeof(DROPFILES) + sizeof(wchar_t);
  for (const auto &path : paths) {
    auto nativePath = QDir::toNativeSeparators(path).toStdWString();
    const SIZE_T pathBytes = (nativePath.size() + 1) * sizeof(wchar_t);
    if (pathBytes > std::numeric_limits<SIZE_T>::max() - bytes)
      return nullptr;
    bytes += pathBytes;
    nativePaths.push_back(std::move(nativePath));
  }

  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
  if (memory == nullptr)
    return nullptr;

  auto *dropFiles = static_cast<DROPFILES *>(GlobalLock(memory));
  if (dropFiles == nullptr) {
    GlobalFree(memory);
    return nullptr;
  }

  dropFiles->pFiles = sizeof(DROPFILES);
  dropFiles->fWide = TRUE;
  auto *cursor = reinterpret_cast<wchar_t *>(reinterpret_cast<BYTE *>(dropFiles) + sizeof(DROPFILES));
  for (const auto &path : nativePaths) {
    const auto pathBytes = (path.size() + 1) * sizeof(wchar_t);
    std::memcpy(cursor, path.c_str(), pathBytes);
    cursor += path.size() + 1;
  }
  *cursor = L'\0';
  GlobalUnlock(memory);
  return memory;
}

std::string MSWindowsClipboardFilesConverter::toIClipboard(HANDLE data) const
{
  const auto drop = static_cast<HDROP>(data);
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  QStringList paths;
  paths.reserve(static_cast<qsizetype>(count));
  for (UINT index = 0; index < count; ++index) {
    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
    std::wstring path(length + 1, L'\0');
    if (DragQueryFileW(drop, index, path.data(), length + 1) == 0)
      continue;
    path.resize(length);
    paths.append(QString::fromStdWString(path));
  }

  QString error;
  const auto bundle =
      deskflow::ClipboardFileBundle::fromPaths(paths, deskflow::ClipboardFileBundle::captureLimits(), &error);
  if (bundle.isEmpty()) {
    LOG_WARN("failed to capture clipboard files: %s", error.toUtf8().constData());
    return {};
  }
  return std::string(bundle.constData(), static_cast<size_t>(bundle.size()));
}
