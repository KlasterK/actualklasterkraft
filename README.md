# ActualKlasterKraft

Minecraft Java Edition server implementation in C++.

## Architectural Choices

- Modern C++23 and Boost
- Heavy usage of coroutines (preventing callback hell and explicit FSMs)
- C++ modules, not headers/TUs
- Less allocations (maybe we'll use custom allocators and pools in the future)

## Dependencies 

- GCC or Clang supporting C++23
- Boost
- CMake 3.30+
- OpenSSL

## Status

Can show MOTD in servers list. Implementing connecting to an empty world now.
