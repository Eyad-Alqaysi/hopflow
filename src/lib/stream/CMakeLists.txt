# SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
# SPDX-License-Identifier: MIT

add_library(stream STATIC
  H264.cpp
  H264.h
  MediaFactory.cpp
  MediaInterfaces.h
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
  Yuv.cpp
  Yuv.h
)

target_link_libraries(stream PUBLIC common net Qt6::Network Qt6::Gui)

if(APPLE)
  target_sources(stream PRIVATE MacVideo.h MacVideo.mm)
  set_source_files_properties(MacVideo.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  find_library(APPKIT_LIBRARY AppKit REQUIRED)
  find_library(COREMEDIA_LIBRARY CoreMedia REQUIRED)
  find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
  find_library(VIDEOTOOLBOX_LIBRARY VideoToolbox REQUIRED)
  # ScreenCaptureKit is weak so the app still starts on macOS 12.0 to 12.2
  target_link_libraries(stream PRIVATE
    ${APPKIT_LIBRARY} ${COREMEDIA_LIBRARY} ${COREVIDEO_LIBRARY} ${VIDEOTOOLBOX_LIBRARY}
    "-weak_framework ScreenCaptureKit"
  )
endif()

if(WIN32)
  target_sources(stream PRIVATE WinVideo.cpp WinVideo.h)
  target_link_libraries(stream PRIVATE d3d11 dxgi mfplat mfuuid wmcodecdspuuid ole32 oleaut32)
endif()
