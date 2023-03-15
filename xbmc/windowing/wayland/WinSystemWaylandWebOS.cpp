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
  // available since webOS 5.0
  m_registry->RequestSingleton(m_webos_foreign, 1, 2, false);
  m_registry->Bind();

  return ok;
}

bool CWinSystemWaylandWebOS::CreateNewWindow(const std::string& name,
                                             bool fullScreen,
                                             RESOLUTION_INFO& res)
{
  auto ok = CWinSystemWayland::CreateNewWindow(name, fullScreen, res);

  if (m_webos_foreign)
  {
    m_exported_surface = m_webos_foreign.export_element(
        GetMainSurface(),
        static_cast<uint32_t>(wayland::webos_foreign_webos_exported_type::video_object));
    m_exported_surface.on_window_id_assigned() =
        [this](std::string window_id, uint32_t exported_type)
    {
      CLog::Log(LOGDEBUG, "Wayland foreign video surface exported {}", window_id);
      this->m_exported_window_name = window_id;
    };
  }

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

bool CWinSystemWaylandWebOS::SetExportedWindow(CRect orig, CRect src, CRect dest)
{
  if (m_webos_foreign)
  {
    CLog::Log(LOGINFO, "CWinSystemWaylandWebOS::SetExportedWindow orig {} {} {} {} src {} {} {} {} -> dest {} {} {} {}",
              orig.x1, orig.y1, orig.x2, orig.y2, src.x1, src.y1, src.x2, src.y2, dest.x1, dest.y1, dest.x2, dest.y2);
    auto origRegion = GetCompositor().create_region();
    auto srcRegion = GetCompositor().create_region();
    auto dstRegion = GetCompositor().create_region();
    origRegion.add(static_cast<int>(orig.x1), static_cast<int>(orig.y1), static_cast<int>(orig.Width()), static_cast<int>(orig.Height()));
    srcRegion.add(static_cast<int>(src.x1), static_cast<int>(src.y1), static_cast<int>(src.Width()), static_cast<int>(src.Height()));
    dstRegion.add(static_cast<int>(dest.x1), static_cast<int>(dest.y1), static_cast<int>(dest.Width()), static_cast<int>(dest.Height()));
    m_exported_surface.set_crop_region(origRegion, srcRegion, dstRegion);

    return true;
  }

  return false;
}

IShellSurface* CWinSystemWaylandWebOS::CreateShellSurface(const std::string& name)
{
  return new CShellSurfaceWebOSShell(*this, *GetConnection(), GetMainSurface(), name, std::string(CCompileInfo::GetAppName()));
}

}
