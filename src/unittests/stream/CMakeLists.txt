# SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
# SPDX-License-Identifier: MIT

if(WIN32)
  set(extra_libs version)
endif()

create_test(
  NAME StreamProtocolTests
  DEPENDS stream
  LIBS base arch ${extra_libs}
  SOURCE StreamProtocolTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
)

create_test(
  NAME StreamLoopbackTests
  DEPENDS stream
  LIBS base arch mt io ${extra_libs}
  SOURCE StreamLoopbackTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
)

create_test(
  NAME H264Tests
  DEPENDS stream
  LIBS base arch ${extra_libs}
  SOURCE H264Tests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
)

if(APPLE)
  create_test(
    NAME MacAudioTests
    DEPENDS stream
    LIBS base arch ${extra_libs}
    SOURCE MacAudioTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
  )

  create_test(
    NAME MacVideoTests
    DEPENDS stream
    LIBS base arch ${extra_libs}
    SOURCE MacVideoTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
  )
endif()

create_test(
  NAME AudioBufferTests
  DEPENDS stream
  LIBS base arch ${extra_libs}
  SOURCE AudioBufferTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
)

create_test(
  NAME VirtualDisplayModeTests
  DEPENDS stream
  LIBS base arch ${extra_libs}
  SOURCE VirtualDisplayModeTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
)

create_test(
  NAME MicrophoneTests
  DEPENDS stream
  LIBS base arch ${extra_libs}
  SOURCE MicrophoneTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
)

# the driver's ring buffer is header only, so it is tested on every platform
create_test(
  NAME LoopbackRingTests
  DEPENDS stream
  LIBS base arch ${extra_libs}
  SOURCE LoopbackRingTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
)
target_include_directories(LoopbackRingTests PRIVATE "${CMAKE_SOURCE_DIR}/src/apps/hopflow-mic-driver")

if(APPLE)
  create_test(
    NAME MicDriverTests
    DEPENDS stream
    LIBS base arch ${extra_libs}
    SOURCE MicDriverTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/stream"
  )
  add_dependencies(MicDriverTests HopflowMic)
  target_compile_definitions(MicDriverTests PRIVATE HOPFLOW_MIC_DRIVER="$<TARGET_BUNDLE_DIR:HopflowMic>")
  target_link_libraries(MicDriverTests "-framework CoreAudio" "-framework CoreFoundation")
endif()
