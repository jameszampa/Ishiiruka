set(HEADLESS_SOURCES
    HeadlessRenderer.cpp
    HeadlessRenderer.h
    HeadlessVertexLoader.cpp
    HeadlessVertexLoader.h
    main.cpp
    VideoBackend.h
)

add_library(HeadlessVideoBackend STATIC ${HEADLESS_SOURCES})

target_link_libraries(HeadlessVideoBackend
    videocommon
    common
    core
)

target_include_directories(HeadlessVideoBackend PRIVATE
    ${CMAKE_SOURCE_DIR}/Source/Core/VideoBackends/Headless
) 