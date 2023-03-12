/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AESinkStarfish.h"

#include "utils/log.h"
#include "xbmc/cores/AudioEngine/AESinkFactory.h"

#include <nlohmann/json.hpp>

void CAESinkStarfish::Register()
{
  AE::AESinkRegEntry entry;
  entry.sinkName = "Starfish";
  entry.createFunc = CAESinkStarfish::Create;
  entry.enumerateFunc = CAESinkStarfish::EnumerateDevicesEx;
  AE::CAESinkFactory::RegisterSink(entry);
}

std::unique_ptr<IAESink> CAESinkStarfish::Create(std::string& device, AEAudioFormat& desiredFormat)
{
  auto sink = std::make_unique<CAESinkStarfish>();
  if (sink->Initialize(desiredFormat, device))
    return sink;

  return {};
}

void CAESinkStarfish::EnumerateDevicesEx(AEDeviceInfoList& list, bool force)
{
  CAEDeviceInfo info;
  info.m_deviceName = "Starfish";
  info.m_displayName = "Starfish (Passthrough only)";
  info.m_channels = AE_CH_LAYOUT_5_1;
  info.m_wantsIECPassthrough = false;

  // PCM disabled for now as the latency is just too high, needs more research
  // Thankfully, ALSA or PulseAudio do work as an alternative for PCM content
  /*info.m_dataFormats.push_back(AE_FMT_U8);
  info.m_dataFormats.push_back(AE_FMT_S16NE);
  info.m_dataFormats.push_back(AE_FMT_S16LE);
  info.m_dataFormats.push_back(AE_FMT_S16BE);
  info.m_dataFormats.push_back(AE_FMT_S32NE);
  info.m_dataFormats.push_back(AE_FMT_S32LE);
  info.m_dataFormats.push_back(AE_FMT_S32BE);
  info.m_dataFormats.push_back(AE_FMT_FLOAT);
  info.m_dataFormats.push_back(AE_FMT_DOUBLE);*/
  info.m_dataFormats.push_back(AE_FMT_RAW);

  info.m_deviceType = AE_DEVTYPE_IEC958;
  info.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_AC3);
  //info.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_EAC3);
  //info.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_TRUEHD);

  info.m_sampleRates.push_back(48000);
  info.m_sampleRates.push_back(44100);
  info.m_sampleRates.push_back(32000);
  info.m_sampleRates.push_back(24000);
  info.m_sampleRates.push_back(22050);
  info.m_sampleRates.push_back(16000);
  info.m_sampleRates.push_back(12000);
  info.m_sampleRates.push_back(8000);

  list.push_back(info);
}

CAESinkStarfish::CAESinkStarfish() :
m_starfishMediaAPI(std::make_unique<StarfishMediaAPIs>())
, m_pts(0)
, m_delay(0)
, m_playtime(0)
{
}

CAESinkStarfish::~CAESinkStarfish()
{
}


bool CAESinkStarfish::Initialize(AEAudioFormat& format, std::string& device)
{
  m_format = format;
  m_pts = 0;

  if (m_format.m_dataFormat != AE_FMT_RAW) return false;
  m_format.m_frameSize = 1;

  CLog::Log(LOGDEBUG, "CAESinkStarfish: Channel count is {}", m_format.m_channelLayout.Count());

  format = m_format;

  nlohmann::json payload;
  payload["isAudioOnly"] = true;
  payload["mediaTransportType"] = "BUFFERSTREAM";
  payload["option"]["appId"] = "org.xbmc.kodi";
  payload["option"]["needAudio"] = true;
  payload["option"]["externalStreamingInfo"]["contents"]["esInfo"]["pauseAtDecodeTime"] = true;
  payload["option"]["externalStreamingInfo"]["contents"]["esInfo"]["seperatedPTS"] = true;
  payload["option"]["externalStreamingInfo"]["contents"]["esInfo"]["ptsToDecode"] = 0;
  payload["option"]["externalStreamingInfo"]["contents"]["format"] = "RAW";
  payload["option"]["transmission"]["contentsType"] = "LIVE"; // "LIVE", "WEBRTC"

  payload["option"]["lowDelayMode"] = true;
  /*payload["option"]["bufferControl"]["preBufferTime"] = 0;
  payload["option"]["bufferControl"]["userBufferCtrl"] = true;
  payload["option"]["bufferControl"]["bufferingMinTime"] = 0;
  payload["option"]["bufferControl"]["bufferingMaxTime"] = 100;*/

  m_bufferSize = 12288;
  if (m_format.m_dataFormat == AE_FMT_RAW)
  {
    switch (m_format.m_streamInfo.m_type)
    {
      case CAEStreamInfo::STREAM_TYPE_AC3:
        payload["option"]["externalStreamingInfo"]["contents"]["codec"]["audio"] = "AC3";
        if (!format.m_streamInfo.m_ac3FrameSize)
          format.m_streamInfo.m_ac3FrameSize = 1536;
        format.m_frames = format.m_streamInfo.m_ac3FrameSize;
        m_bufferSize = format.m_frames * 8;
        break;
      case CAEStreamInfo::STREAM_TYPE_EAC3:
        payload["option"]["externalStreamingInfo"]["contents"]["codec"]["audio"] = "AC3 PLUS";
        payload["option"]["externalStreamingInfo"]["contents"]["ac3PlusInfo"]["channels"] =
            m_format.m_streamInfo.m_channels;
        payload["option"]["externalStreamingInfo"]["contents"]["ac3PlusInfo"]["frequency"] =
            static_cast<double>(m_format.m_streamInfo.m_sampleRate) / 1000;

        if (!format.m_streamInfo.m_ac3FrameSize)
          format.m_streamInfo.m_ac3FrameSize = 1536;
        format.m_frames = format.m_streamInfo.m_ac3FrameSize;
        m_bufferSize = format.m_frames * 8;

        //payload["option"]["externalStreamingInfo"]["contents"]["immersive"] = "ATMOS";
        break;
      default:
        return false;
    }
  }
  else
  {
    payload["option"]["externalStreamingInfo"]["contents"]["pcmInfo"]["bitsPerSample"] = CAEUtil::DataFormatToBits(m_format.m_dataFormat);
    payload["option"]["externalStreamingInfo"]["contents"]["pcmInfo"]["sampleRate"] = m_format.m_sampleRate;
    payload["option"]["externalStreamingInfo"]["contents"]["pcmInfo"]["layout"] = AE_IS_PLANAR(m_format.m_dataFormat) ? "non-interleaved" : "interleaved";

    std::string channel;
    switch (m_format.m_channelLayout.Count())
    {
      case 1: channel = "mono"; break;
      case 2: channel = "stereo"; break;
      case 6: channel = "6-channel"; break;
      default: return false;
    }
    payload["option"]["externalStreamingInfo"]["contents"]["pcmInfo"]["channelMode"] = channel;
    payload["option"]["externalStreamingInfo"]["contents"]["pcmInfo"]["format"] = AEFormatToStarfishFormat(m_format.m_dataFormat);
    payload["option"]["externalStreamingInfo"]["contents"]["codec"]["audio"] = "PCM";
  }

  payload["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["preBufferByte"] = 0;
  payload["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["bufferMinLevel"] = 0;
  payload["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["bufferMaxLevel"] = 0;
  // This is the size after which the sink starts blocking
  payload["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["qBufferLevelAudio"] = m_bufferSize;
  // Internal buffer?
  payload["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["srcBufferLevelAudio"]["minimum"] = format.m_frames;
  payload["option"]["externalStreamingInfo"]["bufferingCtrInfo"]["srcBufferLevelAudio"]["maximum"] = m_bufferSize;

  auto payloadArgs = nlohmann::json();
  payloadArgs["args"] = { payload };
  auto p = payloadArgs.dump();

  m_starfishMediaAPI->notifyForeground();
  CLog::Log(LOGDEBUG, "CAESinkStarfish: Sending Load payload {}", p);
  if (!m_starfishMediaAPI->Load(p.c_str(), &CAESinkStarfish::PlayerCallback, this)) {
    CLog::Log(LOGERROR, "CAESinkStarfish: Load failed");
    return false;
  }

  return true;
}

void CAESinkStarfish::Deinitialize()
{
  m_starfishMediaAPI->Unload();
}

double CAESinkStarfish::GetCacheTotal()
{
  if (m_format.m_dataFormat == AE_FMT_RAW)
    return 8 * m_format.m_streamInfo.GetDuration();
  else
    return 0.0;
}

double CAESinkStarfish::GetLatency()
{
  return 0.0;
}

unsigned int CAESinkStarfish::AddPackets(uint8_t** data, unsigned int frames, unsigned int offset)
{
  //CLog::Log(LOGDEBUG, "CAESinkStarfish::AddPackets frames: {} framesize: {} offset: {}",frames, m_format.m_frameSize, offset);

  nlohmann::json payload;
  auto buffer = data[0] + offset * m_format.m_frameSize;
  payload["bufferAddr"] = fmt::format("{}", fmt::ptr(buffer));
  payload["bufferSize"] = frames * m_format.m_frameSize;
  payload["pts"] = m_pts;
  payload["esData"] = 2;

  int64_t frameTime;
  if (m_format.m_dataFormat == AE_FMT_RAW)
    frameTime = static_cast<int64_t>(m_format.m_streamInfo.GetDuration() * 1000000.0);
  else
    frameTime = 1000000000 * frames / m_format.m_sampleRate;

  // on transcoded content we get 1024 + 1536 frames but we don't want to advance the pts twice
  if (frames != 1024)
    m_pts += frameTime;
  CLog::Log(LOGDEBUG, "CAESinkStarfish::AddPackets payload: {}", payload.dump());

  auto result = m_starfishMediaAPI->Feed(payload.dump().c_str());
  while (result.find(std::string("BufferFull")) != std::string::npos)
  {
    usleep(frameTime / 1000);
    result = m_starfishMediaAPI->Feed(payload.dump().c_str());
  }

  if (result.find(std::string("Ok")) != std::string::npos)
    return frames;

  CLog::Log(LOGWARNING, "CAESinkStarfish::AddPackets Buffer submit returned error: {}",
            result.c_str());
  return 0;
}

void CAESinkStarfish::AddPause(unsigned int millis)
{
  //m_starfishMediaAPI->pushAudioDiscontinuityGap(m_playtime, m_playtime + millis * 1000000);
  //m_pts += millis * 1000000;
  m_starfishMediaAPI->Pause();
  usleep(millis * 1000);
  m_starfishMediaAPI->Play();
}

void CAESinkStarfish::GetDelay(AEDelayStatus& status)
{
  const double hw_latency = 0.25;
  status.SetDelay(hw_latency + static_cast<double>(m_delay) / 1000000000.0);
}

void CAESinkStarfish::Drain()
{
  m_starfishMediaAPI->flush();
}

bool CAESinkStarfish::HasVolume()
{
  return false;
}

void CAESinkStarfish::SetVolume(float volume)
{
  //m_starfishMediaAPI->SetVolume(volume * 100);
}

void CAESinkStarfish::PlayerCallback(const int32_t type,
                                     const int64_t numValue,
                                     const char* strValue)
{
  switch(type) {
    case PF_EVENT_T::PF_EVENT_TYPE_FRAMEREADY:
      m_playtime = numValue;
      m_delay = m_pts - numValue;
      break;
    case PF_EVENT_TYPE_STR_STATE_UPDATE__LOADCOMPLETED:
      m_starfishMediaAPI->Play();
      break;
    default:
      std::string logstr = "";
      if (strValue != nullptr) {
        logstr = std::string(strValue);
      }
      CLog::Log(LOGDEBUG, "CAESinkStarfish::PlayerCallback type: {}, numValue: {}, strValue: {}", type, numValue, logstr);
  }
}

void CAESinkStarfish::PlayerCallback(const int32_t type,
                                     const int64_t numValue,
                                     const char* strValue,
                                     void* data)
{
  static_cast<CAESinkStarfish*>(data)->PlayerCallback(type, numValue, strValue);
}

std::string CAESinkStarfish::AEFormatToStarfishFormat(const enum AEDataFormat format)
{
  switch (format)
  {
    case AE_FMT_U8    : return "U8";
    case AE_FMT_S16NE : return "S16LE";
    case AE_FMT_S16LE : return "S16LE";
    case AE_FMT_S16BE : return "S16BE";
    case AE_FMT_S32NE : return "S32LE";
    case AE_FMT_S32LE : return "S32LE";
    case AE_FMT_S32BE : return "S32BE";
    case AE_FMT_FLOAT : return "F32LE";
    case AE_FMT_DOUBLE : return "F64LE";
  }
  return "";
}