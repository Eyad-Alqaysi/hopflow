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
