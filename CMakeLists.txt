cmake_minimum_required(VERSION 3.30)

project(ActualKlasterKraft VERSION 0.1.0 LANGUAGES C CXX)

option(ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32
    "Implement internal terminal functions used by std::print
        and friends (std::__open_terminal, std::__write_to_terminal)
        using Win32 API"
    OFF
)

option(ACTUALKLASTERKRAFT_ENABLE_PCH
    "Enable precompiling headers.
        It really speeds up building but it might be unstable
        because we're mixing PCH with modules
        (e.g. it causes clangd 22.1.6 to crash)"
    OFF
)

option(ACTUALKLASTERKRAFT_FIX_DEPENDENCIES_MSVC
    "Enable fixes for Boost and OpenSSL when building using
        Microsoft Visual C++ toolchain (cl.exe is not supported
        but Clang is supported)"
    ON
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND WIN32)
    set(WIN32_CLANG_TARGET "x86_64-pc-windows-msvc")
    add_compile_options(-target ${WIN32_CLANG_TARGET})
    add_link_options(-target ${WIN32_CLANG_TARGET})
    add_link_options("-Xlinker" "/nodefaultlib:libcmt")
endif()

if(ACTUALKLASTERKRAFT_FIX_DEPENDENCIES_MSVC)
    set(Boost_COMPILER "-vc145")
    find_package(Boost REQUIRED COMPONENTS json)

    add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_ROOT_DIR}/include"
        IMPORTED_LOCATION             "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD/libcrypto.lib" 
    )

    add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_ROOT_DIR}/include"
        IMPORTED_LOCATION             "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD/libssl.lib"    
        INTERFACE_LINK_LIBRARIES      OpenSSL::Crypto
    )
else()
    find_package(Boost REQUIRED COMPONENTS json)
    find_package(OpenSSL REQUIRED)
endif()

add_executable(actualklasterkraft)

target_sources(actualklasterkraft
    PUBLIC FILE_SET CXX_MODULES FILES
        src/statecoroutines/configuration.cppm
        src/statecoroutines/handshake.cppm
        src/statecoroutines/login.cppm
        src/statecoroutines/play.cppm
        src/statecoroutines/status.cppm
        src/world/blockstates.cppm
        src/world/chunk.cppm
        src/world/math.cppm
        src/world/player.cppm
        src/acceptor.cppm
        src/basepool.cppm
        src/bitfields.cppm
        src/disconnecthelpers.cppm
        src/errc.cppm
        src/formatters.cppm
        src/nbtbuilder.cppm
        src/packetops.cppm
        src/packetrouter.cppm
        src/prebuiltconfiguration.cppm
        src/protocolprimitives.cppm
        src/pubsub.cppm
        src/templates.cppm
        src/transport.cppm
    PRIVATE
        src/main.cpp
)

target_link_libraries(actualklasterkraft PUBLIC Boost::boost Boost::json OpenSSL::Crypto)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(actualklasterkraft PRIVATE -Wall -Wextra -fdiagnostics-color=always)
    
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/boost_fix.h" "
        #pragma once
        #define BOOST_ALL_NO_LIB
        #define BOOST_JSON_STATIC_LINK
    ")
    target_compile_options(actualklasterkraft PRIVATE "-include" "${CMAKE_CURRENT_BINARY_DIR}/boost_fix.h")
endif()

if(WIN32)
    target_link_libraries(actualklasterkraft PUBLIC WS2_32 WSock32)
endif()

if(ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32)
    target_compile_definitions(actualklasterkraft PRIVATE 
        ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32
    )
endif()

if(ACTUALKLASTERKRAFT_ENABLE_PCH)
    target_precompile_headers(actualklasterkraft PRIVATE
        <boost/asio.hpp>
        <boost/asio/experimental/awaitable_operators.hpp>
        <boost/asio/experimental/channel.hpp>
        <boost/container/small_vector.hpp>
        <boost/container/static_vector.hpp>
        <boost/endian.hpp>
        <boost/intrusive/link_mode.hpp>
        <boost/intrusive/list.hpp>
        <boost/intrusive/options.hpp>
        <boost/intrusive/slist.hpp>
        <boost/json.hpp>
        <boost/system.hpp>

        <openssl/err.h>
        <openssl/evp.h>

        <algorithm>
        <array>
        <bit>
        <cassert>
        <chrono>
        <cmath>
        <concepts>
        <cstddef>
        <cstdint>
        <cstdio>
        <exception>
        <format>
        <functional>
        <iterator>
        <limits>
        <memory>
        <numbers>
        <numeric>
        <optional>
        <print>
        <random>
        <ranges>
        <span>
        <stdexcept>
        <string>
        <string_view>
        <tuple>
        <type_traits>
        <utility>
        <vector>
    )
endif()
