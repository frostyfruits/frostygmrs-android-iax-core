cmake_minimum_required(VERSION 3.22.1)

project("frostygmrs_mobile")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FROSTY_NATIVE_SOURCES
    NativeIaxBridge.cpp
    IaxNativeClient.cpp
    libiax2_adapter.cpp
    iax_session.cpp
    iax_udp_transport.cpp
    iax_builder.cpp
    iax_auth.cpp
)

add_library(
    realiax
    SHARED
    ${FROSTY_NATIVE_SOURCES}
)

target_include_directories(
    realiax
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_library(
    log-lib
    log
)

target_link_libraries(
    realiax
    ${log-lib}
)