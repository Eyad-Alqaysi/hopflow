# SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
# SPDX-License-Identifier: MIT

# Core Audio driver for the virtual microphone; deploy/mac/install-hopflow-mic.sh installs it
add_library(HopflowMic MODULE HopflowMicDriver.cpp LoopbackRing.h)

set_target_properties(HopflowMic PROPERTIES
  BUNDLE ON
  BUNDLE_EXTENSION driver
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${CMAKE_PROJECT_REV_FQDN}.mic"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "${CMAKE_PROJECT_COPYRIGHT}"
  CXX_VISIBILITY_PRESET hidden
  AUTOMOC OFF
)

find_library(COREAUDIO_LIBRARY CoreAudio REQUIRED)
find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
target_link_libraries(HopflowMic PRIVATE ${COREAUDIO_LIBRARY} ${COREFOUNDATION_LIBRARY})

# coreaudiod only loads signed drivers; signing the bundle seals its Info.plist too
if(APPLE_CODESIGN_DEV)
  set(mic_identity "${APPLE_CODESIGN_DEV}")
else()
  set(mic_identity "-")
endif()
add_custom_command(
  TARGET HopflowMic POST_BUILD
  COMMAND codesign --force --sign "${mic_identity}" "$<TARGET_BUNDLE_DIR:HopflowMic>"
)
