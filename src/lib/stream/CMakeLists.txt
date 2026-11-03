# SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
# SPDX-License-Identifier: MIT

add_library(stream STATIC
  AudioBuffer.cpp
  AudioBuffer.h
  H264.cpp
  H264.h
  MediaFactory.cpp
  MediaInterfaces.h
  Microphone.cpp
  Microphone.h
  MutedSpeakers.cpp
  MutedSpeakers.h
  RateController.cpp
  RateController.h
  StreamConnection.cpp
  StreamConnection.h
  StreamPeers.cpp
  StreamPeers.h
  StreamPreset.h
  StreamProtocol.cpp
  StreamProtocol.h
  StreamReceiver.cpp
  StreamReceiver.h
  StreamSender.cpp
  StreamSender.h
  TestPattern.cpp
  TestPattern.h
  VideoLatencyGuard.cpp
  VideoLatencyGuard.h
  VirtualDisplayDriver.cpp
  VirtualDisplayDriver.h
  VirtualDisplayMode.h
  Yuv.cpp
  Yuv.h
)

target_link_libraries(stream PUBLIC common net Qt6::Network Qt6::Gui)

if(APPLE)
  target_sources(stream PRIVATE MacAudio.h MacAudio.mm MacAudioTap.h MacAudioTap.mm MacMicrophone.h MacMicrophone.mm)
  target_sources(stream PRIVATE MacVideo.h MacVideo.mm CGVirtualDisplayPrivate.h MacVirtualDisplay.h MacVirtualDisplay.mm)
  set_source_files_properties(
    MacAudio.mm MacAudioTap.mm MacMicrophone.mm MacVideo.mm MacVirtualDisplay.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
  )
  find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
  find_library(AVFOUNDATION_LIBRARY AVFoundation REQUIRED)
  find_library(COREAUDIO_LIBRARY CoreAudio REQUIRED)
  find_library(APPKIT_LIBRARY AppKit REQUIRED)
  find_library(COREAUDIO_LIBRARY CoreAudio REQUIRED)
  find_library(COREGRAPHICS_LIBRARY CoreGraphics REQUIRED)
  find_library(COREMEDIA_LIBRARY CoreMedia REQUIRED)
  find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
  find_library(VIDEOTOOLBOX_LIBRARY VideoToolbox REQUIRED)
  # ScreenCaptureKit is weak so the app still starts on macOS 12.0 to 12.2; process taps
  # (macOS 14.2) are weak through their availability markings
  target_link_libraries(stream PRIVATE
    ${APPKIT_LIBRARY} ${AUDIOTOOLBOX_LIBRARY} ${AVFOUNDATION_LIBRARY} ${COREAUDIO_LIBRARY} ${COREGRAPHICS_LIBRARY}
    ${COREMEDIA_LIBRARY}
    ${COREVIDEO_LIBRARY} ${VIDEOTOOLBOX_LIBRARY}
    "-weak_framework ScreenCaptureKit"
  )
endif()

if(WIN32)
  target_sources(stream PRIVATE
    WinAudio.cpp WinAudio.h WinAudioDevice.cpp WinAudioDevice.h WinMedia.cpp WinMedia.h WinMicrophone.cpp WinMicrophone.h WinVideo.cpp WinVideo.h
    WinVirtualDisplay.cpp WinVirtualDisplay.h
  )
  target_link_libraries(stream PRIVATE
    d3d11 dxgi mfplat mfuuid wmcodecdspuuid ole32 oleaut32 setupapi cfgmgr32 user32 advapi32
  )
  # windows.h would otherwise define min and max macros that break std::min and std::max
  target_compile_definitions(stream PRIVATE NOMINMAX)
endif()
