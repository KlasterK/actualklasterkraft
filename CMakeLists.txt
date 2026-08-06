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
        src/statecoroutines/play.cppm
        src/statecoroutines/status.cppm
        src/world/autogentest.cppm
        src/acceptor.cppm
        src/bitfields.cppm
        src/disconnecthelpers.cppm
        src/errc.cppm
        src/formatters.cppm
        src/nbtbuilder.cppm
        src/packetops.cppm
        src/packetrouter.cppm
        src/prebuiltconfiguration.cppm
        src/session.cppm
        src/streambufops.cppm
    PRIVATE
        src/main.cpp
)
target_link_libraries(actualklasterkraft PUBLIC Boost::boost OpenSSL::Crypto)

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

if(ACTUALKLASTERKRAFT_ENABLE_PCH)
    target_precompile_headers(actualklasterkraft PRIVATE
        <boost/asio.hpp>
        <boost/asio/experimental/channel.hpp>
        <boost/container/static_vector.hpp>
        <boost/endian.hpp>
        <boost/intrusive_ptr.hpp>
        <boost/smart_ptr/intrusive_ref_counter.hpp>
        <boost/system.hpp>
        <boost/uuid.hpp>

        <openssl/md5.h>
        
        <array>
        <cassert>
        <chrono>
        <concepts>
        <coroutine>
        <cstddef>
        <cstdint>
        <cstdint>
        <cstdio>
        <exception>
        <expected>
        <format>
        <functional>
        <iterator>
        <new>
        <optional>
        <print>
        <random>
        <spanstream>
        <stdexcept>
        <string>
        <string_view>
        <system_error>
        <tuple>
        <vector>
    )
endif()
