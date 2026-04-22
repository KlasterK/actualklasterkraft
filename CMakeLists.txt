cmake_minimum_required(VERSION 3.30)
project(ActualKlasterKraft VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Boost REQUIRED)

add_executable(actualklasterkraft 
    src/main.cpp
    src/errc.cpp        include/errc.hpp
    src/acceptor.cpp    include/acceptor.hpp
    src/session.cpp     include/session.hpp
                        include/streambufops.hpp
)
target_link_libraries(actualklasterkraft PUBLIC Boost::boost)
target_include_directories(actualklasterkraft PRIVATE include)
