/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DVDStreamInfo.h"
#include "DVDVideoCodec.h"
#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "threads/SingleLock.h"
#include "threads/Thread.h"
#include "utils/Geometry.h"

#include <starfish-media-pipeline/StarfishMediaAPIs.h>

#include <atomic>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

class CBitstreamConverter;

struct DemuxCryptoInfo;
struct mpeg2_sequence;


typedef struct amc_demux
{
  uint8_t* pData;
  int iSize;
  double dts;
  double pts;
} amc_demux;


class CStarfishVideoBuffer : public CVideoBuffer
{
public:
  CStarfishVideoBuffer(int id) : CVideoBuffer(id) {}
  ~CStarfishVideoBuffer() override = default;
  AVPixelFormat GetFormat() override { return AV_PIX_FMT_NONE; }

private:
  int m_bufferId = -1;
  unsigned int m_textureId = 0;
  // shared_ptr bits, shared between
  // CDVDVideoCodecAndroidMediaCodec and LinuxRenderGLES.
};

class CDVDVideoCodecStarfish : public CDVDVideoCodec
{
public:
  CDVDVideoCodecStarfish(CProcessInfo& processInfo);
  ~CDVDVideoCodecStarfish() override;

  // registration
  static std::unique_ptr<CDVDVideoCodec> Create(CProcessInfo& processInfo);
  static bool Register();

  // required overrides
  bool Open(CDVDStreamInfo& hints, CDVDCodecOptions& options) override;
  bool AddData(const DemuxPacket& packet) override;
  void Reset() override;
  bool Reconfigure(CDVDStreamInfo& hints) override;
  VCReturn GetPicture(VideoPicture* pVideoPicture) override;
  const char* GetName() override { return m_formatname.c_str(); }
  void SetCodecControl(int flags) override;
  unsigned GetAllowedReferences() override;

protected:
  void Dispose();
  void SignalEndOfStream();
  void SetHDR();
  void UpdateFpsDuration();

  void PlayerCallback(const int32_t type, const int64_t numValue, const char *strValue);
  static void PlayerCallback(const int32_t type, const int64_t numValue, const char *strValue, void* data);
  std::unique_ptr<StarfishMediaAPIs> m_starfishMediaAPI;

  CDVDStreamInfo m_hints;
  std::string m_mime;
  std::string m_codecname;
  std::string m_formatname;
  bool m_opened;
  uint32_t m_fpsDuration;
  bool m_needSecureDecoder = false;
  int m_codecControlFlags;
  int m_state;
  int64_t m_current_playtime;

  VideoPicture m_videobuffer;
  std::unique_ptr<CBitstreamConverter> m_bitstream;

  static std::atomic<bool> m_InstanceGuard;
};
