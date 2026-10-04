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
    PUBLIC FILE_SET CXX_MODULES FILES
        src/generic/basepool.cppm
        src/generic/completiontokens.cppm
        src/generic/errc.cppm
        src/generic/formatters.cppm
        src/generic/math.cppm
        src/generic/pubsub.cppm
        src/generic/templates.cppm
        src/data/bitfields.cppm
        src/data/blockstates.cppm
        src/data/nbtbuilder.cppm
        src/data/prebuiltconfiguration.cppm
        src/data/protocolprimitives.cppm
        src/world/chunk.cppm
        src/world/player.cppm
        src/world/posrot.cppm
        src/net/base/acceptor.cppm
        src/net/base/disconnecthelpers.cppm
        src/net/base/packetops.cppm
        src/net/base/statecoroutines.cppm
        src/net/base/transport.cppm
        src/net/play/chat.cppm
        src/net/play/chunkserialization.cppm
        src/net/play/entityplacement.cppm
        src/net/play/initiation.cppm
        src/net/play/keepalive.cppm
        src/net/play/motion.cppm
        src/net/play/packetrouter.cppm
        src/net/play/playerlist.cppm
        src/net/play/session.cppm
        
    PRIVATE
        src/main.cpp
        src/world/chunkimpl/generation.cpp
        src/world/chunkimpl/storage.cpp
        src/net/statecoroutinesimpl/configuration.cpp
        src/net/statecoroutinesimpl/handshake.cpp
        src/net/statecoroutinesimpl/login.cpp
        src/net/statecoroutinesimpl/play.cpp
        src/net/statecoroutinesimpl/status.cpp
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
        <boost/container/flat_map.hpp>
        <boost/container/small_vector.hpp>
        <boost/container/static_vector.hpp>
        <boost/endian.hpp>
        <boost/intrusive/link_mode.hpp>
        <boost/intrusive/list.hpp>
        <boost/intrusive/options.hpp>
        <boost/intrusive/slist.hpp>
        <boost/json.hpp>
        <boost/smart_ptr/local_shared_ptr.hpp>
        <boost/smart_ptr/make_local_shared.hpp>
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
        <print>
        <random>
        <ranges>
        <span>
        <stdexcept>
        <string>
        <string_view>
        <system_error>
        <tuple>
        <type_traits>
        <utility>
        <variant>
        <vector>
    )
endif()
