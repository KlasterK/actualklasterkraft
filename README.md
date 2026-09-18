# ActualKlasterKraft

Minecraft Java Edition server implementation in C++.

## Architectural Choices

- Modern C++23 and Boost
- Heavy usage of coroutines (preventing callback hell and explicit FSMs)
- C++ modules, not headers/TUs
- Less heap allocations, more pools, arenas and stack

## Dependencies 

- GCC or Clang supporting C++23
- Boost
- CMake 3.30+
- OpenSSL

## Status

The server responds Server List Pings, i.e. shows information about self in the server list.

You're spawned in a world with only one chunk which looks like a stack of Air and Grass Block 16\*16\*16 cubes.

You're in Creative Mode, you can fly and break blocks. You can also cheat items into your inventory (since you're in Creative Mode) and place blocks.

You can see how other player move around and see their and your chat messages. Blocks and items are not synchronised.
