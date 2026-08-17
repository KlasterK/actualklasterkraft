module;
#include <cstdint>
export module actualklasterkraft.bitfields;

export namespace TeleportFlags
{
    using IntT = uint32_t;
    constexpr IntT IsXRelative = 0x0001;
    constexpr IntT IsYRelative = 0x0002;
    constexpr IntT IsZRelative = 0x0004;
    constexpr IntT IsYawRelative = 0x0008;
    constexpr IntT IsPitchRelative = 0x0010;
    constexpr IntT IsXVelocityRelative = 0x0020;
    constexpr IntT IsYVelocityRelative = 0x0040;
    constexpr IntT IsZVelocityRelative = 0x0080;
    constexpr IntT IsVelocityRelativeToNewRotation = 0x0100;
}

export namespace PositionAndRotationFlags
{
    using IntT = uint8_t;
    /// Original flag from the protocol
    constexpr IntT IsOnGround = 0x01;
    /// Original flag from the protocol
    constexpr IntT IsPushingAgainstWall = 0x02;
    /// Custom flag, should be cleared before sending
    constexpr IntT IsPositionPresent = 0x04;
    /// Custom flag, should be cleared before sending
    constexpr IntT IsRotationPresent = 0x04;
};
