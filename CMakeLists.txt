cmake_minimum_required(VERSION 3.30)
project(ActualKlasterKraft VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Boost REQUIRED)

add_executable(actualklasterkraft
    src/statecoroutines/handshake.cpp
    src/statecoroutines/status.cpp
    src/acceptor.cpp
    src/errc.cpp
    src/main.cpp
    src/packetops.cpp
    src/session.cpp

    include/acceptor.hpp
    include/errc.hpp
    include/packetops.hpp
    include/session.hpp
    include/statecoroutines.hpp
    include/streambufops.hpp
)
target_link_libraries(actualklasterkraft PUBLIC Boost::boost)
target_include_directories(actualklasterkraft PRIVATE include)
