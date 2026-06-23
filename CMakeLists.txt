cmake_minimum_required(VERSION 3.30)
project(ActualKlasterKraft VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(actualklasterkraft)
target_sources(actualklasterkraft PUBLIC 
    FILE_SET CXX_MODULES FILES
        src/statecoroutines/configuration.cppm
        src/statecoroutines/handshake.cppm
        src/statecoroutines/login.cppm
        src/statecoroutines/status.cppm
        src/acceptor.cppm
        src/errc.cppm
        src/packetops.cppm
        src/prebuiltconfiguration.cppm
        src/session.cppm
        src/streambufops.cppm
    PRIVATE
        src/main.cpp
)
target_link_libraries(actualklasterkraft PUBLIC Boost::boost OpenSSL::Crypto)
target_compile_options(actualklasterkraft PRIVATE -Wall -Wextra -fdiagnostics-color=always)
