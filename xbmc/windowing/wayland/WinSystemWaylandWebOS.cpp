/*
*  Copyright (C) 2023 Team Kodi
*  This file is part of Kodi - https://kodi.tv
*
*  SPDX-License-Identifier: GPL-2.0-or-later
*  See LICENSES/README.md for more information.
*/

#include "WinSystemWaylandWebOS.h"

#include "Connection.h"
#include "Registry.h"
#include "ShellSurfaceWebOSShell.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecStarfish.h"
#include "cores/VideoPlayer/VideoRenderers/HwDecRender/RendererStarfish.h"
#include "cores/AudioEngine/Sinks/AESinkStarfish.h"
#include "utils/log.h"

#include <CompileInfo.h>

namespace KODI::WINDOWING::WAYLAND
{

bool CWinSystemWaylandWebOS::InitWindowSystem()
{
  auto ok = CWinSystemWayland::InitWindowSystem();

  CDVDVideoCodecStarfish::Register();
  CRendererStarfish::Register();
  CAESinkStarfish::Register();

  m_registry.reset(new CRegistry{*GetConnection()});
  m_registry->RequestSingleton(m_webos_foreign, 1, 2);
  m_registry->Bind();

  return ok;
}

bool CWinSystemWaylandWebOS::CreateNewWindow(const std::string& name,
                                             bool fullScreen,
                                             RESOLUTION_INFO& res)
{
  auto ok = CWinSystemWayland::CreateNewWindow(name, fullScreen, res);

  m_exported_surface = m_webos_foreign.export_element(GetMainSurface(),
                                                      static_cast<uint32_t>(wayland::webos_foreign_webos_exported_type::video_object));
  m_exported_surface.on_window_id_assigned() = [this](std::string window_id, uint32_t exported_type)
  {
    CLog::Log(LOGDEBUG, "Wayland foreign video surface exported {}", window_id);
    this->m_exported_window_name = window_id;
  };

  return ok;
}

CWinSystemWaylandWebOS::~CWinSystemWaylandWebOS() noexcept
{
  m_exported_surface = wayland::webos_exported_t{};
  m_webos_foreign = wayland::webos_foreign_t{};

  if (m_registry)
  {
    m_registry->UnbindSingletons();
  }
  m_registry.reset();
}
bool CWinSystemWaylandWebOS::HasCursor()
{
  return false;
}

std::string CWinSystemWaylandWebOS::GetExportedWindowName()
{
  return m_exported_window_name;
}

bool CWinSystemWaylandWebOS::SetExportedWindow(int32_t srcWidth, int32_t srcHeight, int32_t dstWidth, int32_t dstHeight)
{
  auto srcRegion = GetCompositor().create_region();
  auto dstRegion = GetCompositor().create_region();
  srcRegion.add(0, 0, srcWidth, srcHeight);
  dstRegion.add(0, 0, dstWidth, dstHeight);
  m_exported_surface.set_exported_window(srcRegion, dstRegion);

  return true;
}

IShellSurface* CWinSystemWaylandWebOS::CreateShellSurface(const std::string& name)
{
  return new CShellSurfaceWebOSShell(*this, *GetConnection(), GetMainSurface(), name, std::string(CCompileInfo::GetAppName()));
}

}
