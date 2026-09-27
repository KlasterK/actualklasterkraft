cmake_minimum_required(VERSION 3.30)

project(ActualKlasterKraft VERSION 0.1.0 LANGUAGES C CXX)

option(ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32
    "Implement internal terminal functions used by std::print
        and friends (std::__open_terminal, std::__write_to_terminal)
        using Win32 API"
    OFF
)

option(ACTUALKLASTERKRAFT_ENABLE_VISUAL_CXX_FIXES
    "Enable fixes for building with Microsoft Visual C++ toolchain
        (cl.exe is not supported but Clang is supported)"
    OFF
)

option(ACTUALKLASTERKRAFT_ENABLE_PCH
    "Enable precompiling headers.
        It really speeds up building but it might be unstable
        because we're mixing PCH with modules
        (e.g. it causes clangd 22.1.6 to crash)"
    OFF
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(ACTUALKLASTERKRAFT_ENABLE_VISUAL_CXX_FIXES)
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
    PRIVATE
        src/main.cpp

    PUBLIC FILE_SET CXX_MODULES FILES
        src/core/basepool.cppm
        src/core/errc.cppm
        src/core/formatters.cppm
        src/core/pubsub.cppm
        src/core/templates.cppm
        
        src/net/acceptor.cppm
        src/net/packetops.cppm
        src/net/packetrouter.cppm
        src/net/protocolprimitives.cppm
        src/net/transport.cppm
        
        src/protocol/configuration.cppm
        src/protocol/handshake.cppm
        src/protocol/login.cppm
        src/protocol/play.cppm
        src/protocol/status.cppm
        
        src/utils/bitfields.cppm
        src/utils/disconnecthelpers.cppm
        src/utils/nbtbuilder.cppm
        src/utils/prebuiltconfiguration.cppm
        
        src/world/chunkimpl/generation.cppm
        src/world/chunkimpl/storage.cppm
        src/world/blockstates.cppm
        src/world/chunk.cppm
        src/world/math.cppm
        src/world/player.cppm        
)

target_link_libraries(actualklasterkraft PUBLIC Boost::boost Boost::json OpenSSL::Crypto)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(actualklasterkraft PRIVATE -Wall -Wextra -fdiagnostics-color=always)
endif()

if(WIN32)
    target_link_libraries(actualklasterkraft PUBLIC WS2_32 WSock32)
endif()

if(ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32)
    target_compile_definitions(actualklasterkraft PRIVATE 
        ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32
    )
endif()

if(ACTUALKLASTERKRAFT_ENABLE_VISUAL_CXX_FIXES)
    set(WIN32_CLANG_TARGET "x86_64-pc-windows-msvc")
    target_compile_options(actualklasterkraft PRIVATE -target ${WIN32_CLANG_TARGET})
    target_link_options(actualklasterkraft PRIVATE -target ${WIN32_CLANG_TARGET})
    target_link_options(actualklasterkraft PRIVATE "-Xlinker" "/nodefaultlib:libcmt")
    target_compile_options(actualklasterkraft PRIVATE "-include" "${CMAKE_CURRENT_SOURCE_DIR}/src/visualcxxboostfix.hpp")
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
