module;
#include <cstdint>
export module actualklasterkraft.bitfields;

export namespace TeleportFlags
{
    using IntT = uint32_t;
    constexpr IntT RelativeX = 0x0001;
    constexpr IntT RelativeY = 0x0002;
    constexpr IntT RelativeZ = 0x0004;
    constexpr IntT RelativeYaw = 0x0008;
    constexpr IntT RelativePitch = 0x0010;
    constexpr IntT RelativeVelocityX = 0x0020;
    constexpr IntT RelativeVelocityY = 0x0040;
    constexpr IntT RelativeVelocityZ = 0x0080;
    constexpr IntT RelativeVelocityToNewRotation = 0x0100;
}
