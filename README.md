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

Can show MOTD in servers list.

You're spawned in a world with only one chunk which looks like a stack of Air and Grass Block 16\*16\*16 cubes. You're in Creative Mode, you can fly and break blocks. You can also cheat items into your inventory (since you're in Creative Mode) and place blocks but it all is client-side, server doesn't know anything about it, and it's never saved or synchronised with other players.
