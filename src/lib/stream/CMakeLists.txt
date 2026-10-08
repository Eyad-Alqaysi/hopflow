# SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
# SPDX-License-Identifier: MIT

add_library(stream STATIC
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
)

target_link_libraries(stream PUBLIC common net Qt6::Network Qt6::Gui)
