/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDVideoCodecStarfish.h"

#include "DVDCodecs/DVDFactoryCodec.h"
#include "ServiceBroker.h"
#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "cores/VideoPlayer/Interface/DemuxCrypto.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "cores/VideoPlayer/VideoRenderers/RenderManager.h"
#include "media/decoderfilter/DecoderFilterManager.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/BitstreamConverter.h"
#include "utils/BitstreamWriter.h"
#include "utils/CPUInfo.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "windowing/wayland/WinSystemWaylandWebOS.h"

#include <cassert>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

using namespace KODI::MESSAGING;

enum STARFISH_STATES
{
  STARFISH_STATE_RESET,
  STARFISH_STATE_FLUSHED,
  STARFISH_STATE_RUNNING,
};

/*****************************************************************************/
/*****************************************************************************/
CDVDVideoCodecStarfish::CDVDVideoCodecStarfish(CProcessInfo &processInfo)
: CDVDVideoCodec(processInfo)
, m_starfishMediaAPI(std::make_unique<StarfishMediaAPIs>())
, m_formatname("starfish")
, m_opened(false)
, m_fpsDuration(0)
, m_state(STARFISH_STATE_FLUSHED)
, m_current_playtime(0)
{
}

CDVDVideoCodecStarfish::~CDVDVideoCodecStarfish()
{
  Dispose();
}

std::unique_ptr<CDVDVideoCodec> CDVDVideoCodecStarfish::Create(CProcessInfo& processInfo)
{
  return std::make_unique<CDVDVideoCodecStarfish>(processInfo);
}

bool CDVDVideoCodecStarfish::Register()
{
  CDVDFactoryCodec::RegisterHWVideoCodec("starfish_dec", &CDVDVideoCodecStarfish::Create);
  return true;
}

std::atomic<bool> CDVDVideoCodecStarfish::m_InstanceGuard(false);

bool CDVDVideoCodecStarfish::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  m_opened = false;
  std::string payload;
  std::string exportedWindowName;
  auto payloadArgs = nlohmann::json();
  auto payloadArg = nlohmann::json();

  // allow only 1 instance here
  if (m_InstanceGuard.exchange(true))
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecStarfish::Open - InstanceGuard locked");
    return false;
  }

  if (!hints.width || !hints.height)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecStarfish::Open - {}", "null size, cannot handle");
    goto FAIL;
  }
  else if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOPLAYER_USESTARFISH) &&
           !CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOPLAYER_USESTARFISHSURFACE))
    goto FAIL;

  CLog::Log(
      LOGDEBUG,
      "CDVDVideoCodecStarfish::Open hints: Width {} x Height {}, Fpsrate {} / Fpsscale "
      "{}, CodecID {}, Level {}, Profile {}, PTS_invalid {}, Tag {}, Extradata-Size: {}",
      hints.width, hints.height, hints.fpsrate, hints.fpsscale, hints.codec, hints.level,
      hints.profile, hints.ptsinvalid, hints.codec_tag, hints.extrasize);

  m_hints = hints;
  switch(m_hints.codec)
  {
    case AV_CODEC_ID_MPEG2VIDEO:
      m_mime = "video/mpeg2";
      m_formatname = "amc-mpeg2";
      m_codecname = "MPEG2";
      break;
    case AV_CODEC_ID_MPEG4:
      m_mime = "video/mp4v-es";
      m_formatname = "amc-mpeg4";
      m_codecname = "MPEG4";
      break;
    case AV_CODEC_ID_VP8:
      m_mime = "video/x-vnd.on2.vp8";
      m_formatname = "amc-vp8";
      m_codecname = "VP8";
      break;
    case AV_CODEC_ID_VP9:
      m_mime = "video/x-vnd.on2.vp9";
      m_formatname = "amc-vp9";
      m_codecname = "VP9";
      break;
    case AV_CODEC_ID_AVS:
    case AV_CODEC_ID_CAVS:
    case AV_CODEC_ID_H264:
      m_mime = "video/avc";
      m_formatname = "amc-h264";
      m_codecname = "H264";

      // check for h264-avcC and convert to h264-annex-b
      if (m_hints.extradata && !m_hints.cryptoSession)
      {
        m_bitstream = std::make_unique<CBitstreamConverter>();
        if (!m_bitstream->Open(m_hints.codec, (uint8_t*)m_hints.extradata, m_hints.extrasize, true))
        {
          m_bitstream.reset();
        }
      }
      break;
    case AV_CODEC_ID_HEVC:
    {
      m_mime = "video/hevc";
      m_formatname = "amc-hevc";
      m_codecname = "H265";

      bool isDvhe = (m_hints.codec_tag == MKTAG('d', 'v', 'h', 'e'));
      bool isDvh1 = (m_hints.codec_tag == MKTAG('d', 'v', 'h', '1'));

      // some files don't have dvhe or dvh1 tag set up but have Dolby Vision side data
      if (!isDvhe && !isDvh1 && m_hints.hdrType == StreamHdrType::HDR_TYPE_DOLBYVISION)
      {
        // page 10, table 2 from https://professional.dolby.com/siteassets/content-creation/dolby-vision-for-content-creators/dolby-vision-streams-within-the-http-live-streaming-format-v2.0-13-november-2018.pdf
        if (m_hints.codec_tag == MKTAG('h', 'v', 'c', '1'))
          isDvh1 = true;
        else
          isDvhe = true;
      }

      if (isDvhe || isDvh1)
      {
        bool supportsDovi = true;

        CLog::Log(LOGDEBUG, "CDVDVideoCodecStarfish::Open Dolby Vision playback support: {}",
                  supportsDovi);

        if (supportsDovi)
        {
          m_mime = "video/dolby-vision";
          m_formatname = isDvhe ? "amc-dvhe" : "amc-dvh1";

          payloadArg["option"]["externalStreamingInfo"]["contents"]["DolbyHdrInfo"]["encryptionType"] = "clear"; //"clear", "bl", "el", "all"
          payloadArg["option"]["externalStreamingInfo"]["contents"]["DolbyHdrInfo"]["profileId"] = 5; // profile 0-9
          payloadArg["option"]["externalStreamingInfo"]["contents"]["DolbyHdrInfo"]["trackType"] = "single"; // "single" / "dual"
        }
      }

      // check for hevc-hvcC and convert to h265-annex-b
      if (m_hints.extradata && !m_hints.cryptoSession)
      {
        m_bitstream = std::make_unique<CBitstreamConverter>();
        if (!m_bitstream->Open(m_hints.codec, (uint8_t*)m_hints.extradata, m_hints.extrasize, true))
        {
          m_bitstream.reset();
        }
      }
    }
      break;
    case AV_CODEC_ID_VC1:
      m_mime = "video/wvc1";
      m_formatname = "amc-vc1";
      m_codecname = "VC1";
      break;
    case AV_CODEC_ID_AV1:
      m_mime = "video/av01";
      m_formatname = "amc-av1";
      m_codecname = "AV1";
      break;
    default:
      CLog::Log(LOGDEBUG, "CDVDVideoCodecStarfish::Open Unknown hints.codec({})",
                hints.codec);
      goto FAIL;
  }

  m_starfishMediaAPI->notifyForeground();

  exportedWindowName = dynamic_cast<KODI::WINDOWING::WAYLAND::CWinSystemWaylandWebOS*>(CServiceBroker::GetWinSystem())
                           ->GetExportedWindowName();

  payloadArg["mediaTransportType"] = "BUFFERSTREAM";
  payloadArg["option"]["windowId"] = exportedWindowName;
  // enables the getCurrentPlaytime API
  payloadArg["option"]["queryPosition"] = true;
  payloadArg["option"]["appId"] = "org.xbmc.kodi";
  payloadArg["option"]["externalStreamingInfo"]["contents"]["codec"]["video"] = m_codecname;
  payloadArg["option"]["externalStreamingInfo"]["contents"]["esInfo"]["pauseAtDecodeTime"] = true;
  payloadArg["option"]["externalStreamingInfo"]["contents"]["esInfo"]["seperatedPTS"] = true;
  payloadArg["option"]["externalStreamingInfo"]["contents"]["esInfo"]["ptsToDecode"] = 0;
  payloadArg["option"]["externalStreamingInfo"]["contents"]["esInfo"]["videoWidth"] = m_hints.width;
  payloadArg["option"]["externalStreamingInfo"]["contents"]["esInfo"]["videoHeight"] = m_hints.height;
  payloadArg["option"]["externalStreamingInfo"]["contents"]["esInfo"]["videoFpsValue"] = m_hints.fpsrate;
  payloadArg["option"]["externalStreamingInfo"]["contents"]["esInfo"]["videoFpsScale"] = m_hints.fpsscale;
  payloadArg["option"]["externalStreamingInfo"]["contents"]["format"] = "RAW";
  payloadArg["option"]["transmission"]["contentsType"] = "LIVE"; // "LIVE", "WebRTC"
  payloadArg["option"]["needAudio"] = false;
  payloadArg["option"]["seekMode"] = "late_Iframe";
  payloadArg["option"]["lowDelayMode"] = true;

  payloadArg["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["preBufferByte"] = 0;
  payloadArg["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["bufferMinLevel"] = 0;
  payloadArg["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["bufferMaxLevel"] = 0;
  payloadArg["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["qBufferLevelVideo"] = 1048576;
  payloadArg["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["srcBufferLevelVideo"]["minimum"] = 1048576;
  payloadArg["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["srcBufferLevelVideo"]["maximum"] = 8388608;


  /*payloadArg["option"]["bufferControl"]["preBufferTime"] = 1000000000;
  payloadArg["option"]["bufferControl"]["userBufferCtrl"] = true;
  payloadArg["option"]["bufferControl"]["bufferingMinTime"] = 0;
  payloadArg["option"]["bufferControl"]["bufferingMaxTime"] = 1000000000;*/

  payloadArgs["args"] = { payloadArg };

  //payload = R"({"args":[{"mediaTransportType":"BUFFERSTREAM","option":{"queryPosition": true, "windowId":"%s","appId":"org.xbmc.kodi","externalStreamingInfo":{"contents":{"codec":{"video":"%s"},"esInfo":{"pauseAtDecodeTime":true,"seperatedPTS":true,"ptsToDecode":0, "videoWidth": %d, "videoHeight": %d, "videoFpsValue": %d, "videoFpsScale": %d},"format":"RAW"},"bufferingCtrInfo":{"bufferMaxLevel": 0,"bufferMinLevel":0,"srcBufferLevelVideo":{"minimum":0,"maximum":0},"preBufferByte":0,"qBufferLevelAudio":0,"qBufferLevelVideo":0,"srcBufferLevelAudio":{"minimum":1,"maximum":10}}},"transmission":{"contentsType":"LIVE"},"needAudio":false}}]})";
  //payload = R"({ "args": [ { "mediaTransportType": "BUFFERSTREAM", "option": { "transmission":{"contentsType":"WEBRTC"},"needAudio":false, "appId": "org.xbmc.kodi", "queryPosition": true, "windowId": "%s", "avSink": { "videoSink": { }, "audioSink": { } }, "externalStreamingInfo": { "streamQualityInfo": true, "totalStreamSize": 256, "restartStreaming": false, "contents": { "provider": "Chrome", "format": "RAW", "ac3PlusInfo": { "channels": 6, "frequency": 48.0 }, "immersive": "ATMOS", "codec": { "video": "%s", "audio": "AC3 PLUS" }, "esInfo": { "ptsToDecode": 0, "pauseAtDecodeTime": true, "seperatedPTS": true, "videoWidth": %d, "videoHeight": %d } }, "bufferingCtrInfo": { "preBufferByte": 0, "bufferMinLevel": 0, "bufferMaxLevel": 0, "qBufferLevelVideo": 1048576, "qBufferLevelAudio": 1048576, "srcBufferLevelVideo": { "minimum": 1048576, "maximum": 8388608 }, "srcBufferLevelAudio": { "minimum": 1048576, "maximum": 2097152 } } }, "seekMode": "late_Iframe", "lipsyncOffset": 100, "adaptiveStreaming": { "maxWidth": %d, "maxHeight": %d, "maxFrameRate": 60 } } } ] })";
  //payload = R"({ "args": [ { "mediaTransportType": "BUFFERSTREAM", "option": { "transmission":{"contentsType":"WEBRTC"},"needAudio":false, "appId": "org.xbmc.kodi", "queryPosition": true, "windowId": "%s", "avSink": { "videoSink": { }, "audioSink": { } }, "externalStreamingInfo": { "streamQualityInfo": true, "totalStreamSize": 256, "restartStreaming": false, "contents": { "provider": "Chrome", "format": "RAW", "codec": { "video": "%s" }, "esInfo": { "ptsToDecode": 0, "pauseAtDecodeTime": true, "seperatedPTS": true, "videoWidth": %d, "videoHeight": %d } }, "bufferingCtrInfo": { "preBufferByte": 0, "bufferMinLevel": 0, "bufferMaxLevel": 0, "qBufferLevelVideo": 1048576, "qBufferLevelAudio": 1048576, "srcBufferLevelVideo": { "minimum": 1048576, "maximum": 8388608 }, "srcBufferLevelAudio": { "minimum": 1048576, "maximum": 2097152 } } }, "seekMode": "late_Iframe", "lipsyncOffset": 100, "adaptiveStreaming": { "maxWidth": %d, "maxHeight": %d, "maxFrameRate": 60 } } } ] })";

  payload = payloadArgs.dump();
  CLog::Log(LOGDEBUG, "CDVDVideoCodecStarfish: Sending Load payload {}", payload);
  if (!m_starfishMediaAPI->Load(payload.c_str(), &CDVDVideoCodecStarfish::PlayerCallback, this)) {
    CLog::Log(LOGERROR, "CDVDVideoCodecStarfish: Load failed");
    goto FAIL;
  }

  SetHDR();

  m_codecControlFlags = 0;

  CLog::Log(LOGINFO,
            "CDVDVideoCodecStarfish:: "
            "Open Starfish {}",
            m_codecname);

  // setup a YUV420P VideoPicture buffer.
  // first make sure all properties are reset.
  m_videobuffer.Reset();

  m_videobuffer.iWidth  = m_hints.width;
  m_videobuffer.iHeight = m_hints.height;
  // these will get reset to crop values later
  m_videobuffer.iDisplayWidth  = m_hints.width;
  m_videobuffer.iDisplayHeight = m_hints.height;
  m_videobuffer.stereoMode = m_hints.stereo_mode;

  m_opened = true;

  m_processInfo.SetVideoDecoderName(m_formatname, true);
  m_processInfo.SetVideoPixelFormat("Surface");
  m_processInfo.SetVideoDimensions(m_hints.width, m_hints.height);
  m_processInfo.SetVideoDeintMethod("hardware");
  m_processInfo.SetVideoDAR(m_hints.aspect);

  UpdateFpsDuration();

  return true;

FAIL:
  m_InstanceGuard.exchange(false);

  return false;
}

void CDVDVideoCodecStarfish::Dispose()
{
  if (!m_opened)
    return;
  m_opened = false;

  m_starfishMediaAPI->Unload();

  m_InstanceGuard.exchange(false);
}

bool CDVDVideoCodecStarfish::AddData(const DemuxPacket &packet)
{

  if (!m_opened)
    return false;

  double pts(packet.pts), dts(packet.dts);

  if (CServiceBroker::GetLogging().CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG,
              "CDVDVideoCodecStarfish::AddData dts:{:0.2f} pts:{:0.2f} sz:{} "
              "current state ({})",
              dts, pts, packet.iSize, m_state);

  if (m_hints.ptsinvalid)
    pts = DVD_NOPTS_VALUE;

  uint8_t *pData(packet.pData);
  size_t iSize(packet.iSize);

  // we have an input buffer, fill it.
  if (pData && m_bitstream)
  {
    m_bitstream->Convert(pData, iSize);

    if (m_state == STARFISH_STATE_FLUSHED && !m_bitstream->CanStartDecode())
    {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecStarfish::AddData: waiting for keyframe (bitstream)");
      return true;
    }

    iSize = m_bitstream->GetConvertSize();
    pData = m_bitstream->GetConvertBuffer();
  }

  if (m_state == STARFISH_STATE_FLUSHED)
  {
    if (pts > 0)
      m_starfishMediaAPI->Seek(std::to_string(DVD_TIME_TO_MSEC(pts)).c_str());
    m_state = STARFISH_STATE_RUNNING;
  }

  if (pData && iSize)
  {
    nlohmann::json payload;
    payload["bufferAddr"] = fmt::format("{}", fmt::ptr(pData));
    payload["bufferSize"] = iSize;
    payload["pts"] = DVD_TIME_TO_MSEC(pts) * 1000000;
    payload["esData"] = 1;

    auto result = m_starfishMediaAPI->Feed(payload.dump().c_str());
    CLog::Log(LOGDEBUG, "Result: {}", result);
    std::size_t found = result.find(std::string("Ok"));
    if (found == std::string::npos) {
      found = result.find(std::string("BufferFull"));
      if (found != std::string::npos) {
        CLog::Log(LOGWARNING, "Buffer is full");
        return false;
      }
      CLog::Log(LOGWARNING, "Buffer submit returned error: {}", result.c_str());
      return true;
    }
  }

  return true;
}

void CDVDVideoCodecStarfish::Reset()
{
  CLog::Log(LOGDEBUG, "CDVDVideoCodecStarfish::Reset");
  if (!m_opened)
    return;

  m_starfishMediaAPI->flush();

  m_state = STARFISH_STATE_FLUSHED;

  // Invalidate our local VideoPicture bits
  m_videobuffer.pts = DVD_NOPTS_VALUE;

  if (m_bitstream)
    m_bitstream->ResetStartDecode();
}

bool CDVDVideoCodecStarfish::Reconfigure(CDVDStreamInfo &hints)
{
  if (m_hints.Equal(hints, CDVDStreamInfo::COMPARE_ALL &
                               ~(CDVDStreamInfo::COMPARE_ID | CDVDStreamInfo::COMPARE_EXTRADATA)))
  {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecStarfish::Reconfigure: true");
    m_hints = hints;
    return true;
  }
  CLog::Log(LOGDEBUG, "CDVDVideoCodecStarfish::Reconfigure: false");
  return false;
}

CDVDVideoCodec::VCReturn CDVDVideoCodecStarfish::GetPicture(VideoPicture* pVideoPicture)
{
  if (!m_opened)
    return VC_NONE;

  if (m_state == STARFISH_STATE_FLUSHED) {
    return VC_BUFFER;
  }

  CLog::Log(LOGDEBUG, "GetPlaytime is {}", m_starfishMediaAPI->getCurrentPlaytime());

  auto currentPlaytime = m_starfishMediaAPI->getCurrentPlaytime();

  // The playtime didn't advance probably we need more data
  if (currentPlaytime == m_current_playtime)
    return VC_BUFFER;

  m_current_playtime = currentPlaytime;

  pVideoPicture->videoBuffer = nullptr;
  pVideoPicture->SetParams(m_videobuffer);
  //pVideoPicture->videoBuffer = m_videobuffer.videoBuffer;
  pVideoPicture->videoBuffer = new CStarfishVideoBuffer(0);
  pVideoPicture->dts = 0;
  pVideoPicture->pts = DVD_MSEC_TO_TIME(m_starfishMediaAPI->getCurrentPlaytime() / 1000000.0);
  CLog::Log(LOGDEBUG, LOGVIDEO,
            "CDVDVideoCodecStarfish::GetPicture pts:{:0.4f}",
            pVideoPicture->pts);

  return VC_PICTURE;

}


void CDVDVideoCodecStarfish::SetCodecControl(int flags)
{
  if (m_codecControlFlags != flags)
  {
    CLog::Log(LOGDEBUG, LOGVIDEO, "CDVDVideoCodecStarfish::{} {:x}->{:x}", __func__,
              m_codecControlFlags, flags);

    if((flags & DVD_CODEC_CTRL_DRAIN) && !(m_codecControlFlags & DVD_CODEC_CTRL_DRAIN)) {
      m_starfishMediaAPI->Pause();
    }

    if(!(flags & DVD_CODEC_CTRL_DRAIN) && (m_codecControlFlags & DVD_CODEC_CTRL_DRAIN)) {
      m_starfishMediaAPI->Play();
    }

    m_codecControlFlags = flags;
  }
}

unsigned CDVDVideoCodecStarfish::GetAllowedReferences()
{
  return 4;
}

void CDVDVideoCodecStarfish::SignalEndOfStream()
{
    m_starfishMediaAPI->pushEOS();
}

void CDVDVideoCodecStarfish::SetHDR()
{
  if (m_hints.masteringMetadata)
  {
    auto hdrData = nlohmann::json();

    switch (m_hints.colorTransferCharacteristic)
    {
      case AVColorTransferCharacteristic::AVCOL_TRC_SMPTEST2084:
        hdrData["hdrType"] = "HDR10";
        break;
      case AVColorTransferCharacteristic::AVCOL_TRC_ARIB_STD_B67:
        hdrData["hdrType"] = "HLG";
        break;
      default:
        hdrData["hdrType"] = "none";
        break;
    }

    auto sei = nlohmann::json();
    // for more information, see CTA+861.3-A standard document
    static const double MAX_CHROMATICITY = 50000;
    static const double MAX_LUMINANCE = 10000;
    sei["displayPrimariesX0"] = static_cast<unsigned short>(
        av_q2d(m_hints.masteringMetadata->display_primaries[0][0]) * MAX_CHROMATICITY + 0.5);
    sei["displayPrimariesY0"] = static_cast<unsigned short>(
        av_q2d(m_hints.masteringMetadata->display_primaries[0][1]) * MAX_CHROMATICITY + 0.5);
    sei["displayPrimariesX1"] = static_cast<unsigned short>(
        av_q2d(m_hints.masteringMetadata->display_primaries[1][0]) * MAX_CHROMATICITY + 0.5);
    sei["displayPrimariesY1"] = static_cast<unsigned short>(
        av_q2d(m_hints.masteringMetadata->display_primaries[1][1]) * MAX_CHROMATICITY + 0.5);
    sei["displayPrimariesX2"] = static_cast<unsigned short>(
        av_q2d(m_hints.masteringMetadata->display_primaries[2][0]) * MAX_CHROMATICITY + 0.5);
    sei["displayPrimariesY2"] = static_cast<unsigned short>(
        av_q2d(m_hints.masteringMetadata->display_primaries[2][1]) * MAX_CHROMATICITY + 0.5);
    sei["whitePointX"] = static_cast<unsigned short>(
        av_q2d(m_hints.masteringMetadata->white_point[0]) * MAX_CHROMATICITY + 0.5);
    sei["whitePointY"] = static_cast<unsigned short>(
        av_q2d(m_hints.masteringMetadata->white_point[1]) * MAX_CHROMATICITY + 0.5);
    sei["minDisplayMasteringLuminance"] = static_cast<unsigned short>(
                                                       av_q2d(m_hints.masteringMetadata->min_luminance) + 0.5);
    sei["maxDisplayMasteringLuminance"] = static_cast<unsigned short>(
                                                                    av_q2d(m_hints.masteringMetadata->max_luminance) * MAX_LUMINANCE + 0.5);
    // we can have HDR content that does not provide content light level metadata
    if (m_hints.contentLightMetadata)
    {
      sei["maxContentLightLevel"] = static_cast<unsigned short>(m_hints.contentLightMetadata->MaxCLL);
      sei["maxPicAverageLightLevel"] = static_cast<unsigned short>(m_hints.contentLightMetadata->MaxFALL);
    }
    hdrData["sei"] = sei;

    auto vui = nlohmann::json();
    vui["transferCharacteristics"] = static_cast<int>(m_hints.colorTransferCharacteristic);
    vui["colorPrimaries"] = static_cast<int>(m_hints.colorPrimaries);
    vui["matrixCoeffs"] = static_cast<int>(m_hints.colorSpace);
    vui["videoFullRangeFlag"] = m_hints.colorRange == AVCOL_RANGE_JPEG;
    hdrData["vui"] = vui;

    std::string payload = hdrData;
    CLog::Log(LOGDEBUG, "CDVDVideoCodecStarfish::SetHDR setting hdr data payload {}", payload);
    m_starfishMediaAPI->setHdrInfo(payload.c_str());
  }
}

void CDVDVideoCodecStarfish::UpdateFpsDuration()
{

  if (m_hints.fpsrate > 0 && m_hints.fpsscale > 0)
    m_fpsDuration = static_cast<uint32_t>(1e9 * m_hints.fpsscale / m_hints.fpsrate);
  else
    m_fpsDuration = 1;

  m_processInfo.SetVideoFps(static_cast<float>(m_hints.fpsrate) / m_hints.fpsscale);

  CLog::Log(LOGDEBUG,
            "CDVDVideoCodecStarfish::UpdateFpsDuration fpsRate:{} fpsscale:{}",
            m_hints.fpsrate, m_hints.fpsscale);
}

void CDVDVideoCodecStarfish::PlayerCallback(const int32_t type,
                                            const int64_t numValue,
                                            const char* strValue)
{
  std::string logstr = "";
  if (strValue != nullptr) {
    logstr = std::string(strValue);
  }
  CLog::Log(LOGDEBUG, "CStarfishVideoCodec::PlayerCallback type: {}, numValue: {}, strValue: {}", type, numValue, logstr);

  switch(type) {
    case PF_EVENT_TYPE_STR_STATE_UPDATE__LOADCOMPLETED:
      m_starfishMediaAPI->Play();
      m_state = STARFISH_STATE_FLUSHED;
      break;
  }

}
void CDVDVideoCodecStarfish::PlayerCallback(const int32_t type,
                                            const int64_t numValue,
                                            const char* strValue,
                                            void* data)
{
  static_cast<CDVDVideoCodecStarfish*>(data)->PlayerCallback(type, numValue, strValue);
}
