// Adapted and modified Angle, Vector2 and Vector3 from SFML 3

////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2026 Laurent Gomila (laurent@sfml-dev.org)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

module;
#include <array>
#include <cassert>
#include <cmath>
#include <numbers>
#include <type_traits>
export module actualklasterkraft.world.math;

namespace priv
{
    constexpr float pi = 3.141592654f;
    constexpr float tau = pi * 2.f;

    constexpr float positive_remainder(float a, float b)
    {
        assert(
            b > 0.f && "Cannot calculate remainder with non-positive divisor");
        const float val = a - static_cast<float>(static_cast<int>(a / b)) * b;
        return val >= 0.f ? val : val + b;
    }
}

export class Angle
{
private:
    constexpr explicit Angle(float radians)
        : m_radians(radians)
    {
    }

public:
    constexpr Angle() = default;

    [[nodiscard]] static constexpr Angle from_degrees(float angle)
    {
        return Angle(angle * (priv::pi / 180.f));
    }

    [[nodiscard]] static constexpr Angle from_radians(float angle)
    {
        return Angle(angle);
    }

    [[nodiscard]] constexpr float as_degrees() const
    {
        return m_radians * (180.f / priv::pi);
    }

    [[nodiscard]] constexpr float as_radians() const { return m_radians; }

    /// \brief Wrap to a range such that -180° <= angle < 180°
    ///
    /// Similar to a modulo operation, this returns a copy of the angle
    /// constrained to the range [-180°, 180°) == [-Pi, Pi).
    /// The resulting angle represents a rotation which is equivalent to `*this`.
    [[nodiscard]] constexpr Angle wrap_signed() const
    {
        return from_radians(
            priv::positive_remainder(m_radians + priv::pi, priv::tau)
            - priv::pi);
    }

    /// \brief Wrap to a range such that 0° <= angle < 360°
    ///
    /// Similar to a modulo operation, this returns a copy of the angle
    /// constrained to the range [0°, 360°) == [0, Tau) == [0, 2*Pi).
    /// The resulting angle represents a rotation which is equivalent to `*this`.
    ///
    /// The name "unsigned" originates from the similarity to unsigned integers.
    [[nodiscard]] constexpr Angle wrap_unsigned() const
    {
        return from_radians(priv::positive_remainder(m_radians, priv::tau));
    }

private:
    float m_radians { };
};

export template <typename T> class Vec2
{
public:
    constexpr Vec2() = default;

    constexpr Vec2(T x, T y)
        : x(x)
        , z(y)
    {
    }

    template <typename U> constexpr explicit operator Vec2<U>() const
    {
        return Vec2<U>(static_cast<U>(x), static_cast<U>(z));
    }

    /// \brief Construct the vector from polar coordinates <i><b>(floating-point)</b></i>
    ///
    /// \param r   Length of vector (can be negative)
    /// \param phi Angle from X axis
    ///
    /// Note that this constructor is lossy: calling `length()` and `angle()`
    /// may return values different to those provided in this constructor.
    ///
    /// In particular, these transforms can be applied:
    /// - `Vector2(r, phi) == Vector2(-r, phi + 180_deg)`
    /// - `Vector2(r, phi) == Vector2(r, phi + n * 360_deg)`
    Vec2(T r, Angle phi)
        : x(r * static_cast<T>(std::cos(phi.as_radians())))
        , z(r * static_cast<T>(std::sin(phi.as_radians())))
    {
        static_assert(std::is_floating_point_v<T>,
            "Vector2::Vector2(T, Angle) is only supported for floating point types");
    }

    [[nodiscard]] T length() const
    {
        static_assert(std::is_floating_point_v<T>,
            "Vector2::length() is only supported for floating point types");

        // don't use std::hypot because of slow performance
        return std::sqrt(x * x + z * z);
    }

    [[nodiscard]] constexpr T length_squared() const { return dot(*this); }

    [[nodiscard]] Vec2 normalized() const
    {
        static_assert(std::is_floating_point_v<T>,
            "Vector2::normalized() is only supported for floating point types");

        assert(*this != Vec2()
            && "Vector2::normalized() cannot normalize a zero vector");
        return (*this) / length();
    }

    /// \brief Signed angle from `*this` to `rhs` <i><b>(floating-point)</b></i>.
    ///
    /// \return The smallest angle which rotates `*this` in positive
    /// or negative direction, until it has the same direction as `rhs`.
    /// The result has a sign and lies in the range [-180, 180) degrees.
    /// \pre Neither `*this` nor `rhs` is a zero vector.
    [[nodiscard]] Angle angle_to(Vec2 rhs) const
    {
        static_assert(std::is_floating_point_v<T>,
            "Vector2::angleTo() is only supported for floating point types");

        assert(*this != Vec2()
            && "Vector2::angleTo() cannot calculate angle from a zero vector");
        assert(rhs != Vec2()
            && "Vector2::angleTo() cannot calculate angle to a zero vector");
        return Angle::from_radians(
            static_cast<float>(std::atan2(cross(rhs), dot(rhs))));
    }

    /// \brief Signed angle from +X or (1,0) vector <i><b>(floating-point)</b></i>.
    ///
    /// For example, the vector (1,0) corresponds to 0 degrees, (0,1) corresponds to 90 degrees.
    ///
    /// \return Angle in the range [-180, 180) degrees.
    /// \pre This vector is no zero vector.
    [[nodiscard]] Angle angle() const
    {
        static_assert(std::is_floating_point_v<T>,
            "Vector2::angle() is only supported for floating point types");

        assert(*this != Vec2()
            && "Vector2::angle() cannot calculate angle from a zero vector");
        return Angle::from_radians(static_cast<float>(std::atan2(z, x)));
    }

    /// \brief Projection of this vector onto `axis` <i><b>(floating-point)</b></i>.
    ///
    /// \param axis Vector being projected onto. Need not be normalized.
    /// \pre `axis` must not have length zero.
    [[nodiscard]] constexpr Vec2 projected_onto(Vec2 axis) const
    {
        static_assert(std::is_floating_point_v<T>,
            "Vector2::projectedOnto() is only supported for floating point types");

        assert(axis != Vec2<T>()
            && "Vector2::projectedOnto() cannot project onto a zero vector");
        return dot(axis) / axis.length_squared() * axis;
    }

    [[nodiscard]] constexpr T dot(Vec2 rhs) const
    {
        return x * rhs.x + z * rhs.z;
    }

    /// \brief Y component of the cross product of two 2D vectors.
    ///
    /// Treats the operands as 3D vectors, computes their cross product
    /// and returns the result's Y component (X and Z components are always zero).
    [[nodiscard]] constexpr T cross(Vec2 rhs) const
    {
        return x * rhs.z - z * rhs.x;
    }

    [[nodiscard]] constexpr Vec2 component_wise_mul(Vec2 rhs) const
    {
        return Vec2<T>(x * rhs.x, z * rhs.z);
    }

    [[nodiscard]] constexpr Vec2 component_wise_div(Vec2 rhs) const
    {
        assert(rhs.x != 0 && "Vector2::componentWiseDiv() cannot divide by 0");
        assert(rhs.z != 0 && "Vector2::componentWiseDiv() cannot divide by 0");
        return Vec2<T>(x / rhs.x, z / rhs.z);
    }

    constexpr std::array<T, 2> to_array() const { return { x, z }; }

    T x { };
    T z { };
};

export template <typename T> class Vec3
{
public:
    constexpr Vec3() = default;

    constexpr Vec3(T x, T y, T z)
        : x(x)
        , y(y)
        , z(z)
    {
    }

    template <typename U> constexpr explicit operator Vec3<U>() const
    {
        return Vec3<U>(static_cast<U>(x), static_cast<U>(y), static_cast<U>(z));
    }

    [[nodiscard]] T length() const
    {
        static_assert(std::is_floating_point_v<T>,
            "Vector3::length() is only supported for floating point types");

        // don't use std::hypot because of slow performance
        return std::sqrt(x * x + y * y + z * z);
    }

    [[nodiscard]] constexpr T length_squared() const { return dot(*this); }

    [[nodiscard]] Vec3 normalized() const
    {
        static_assert(std::is_floating_point_v<T>,
            "Vector3::normalized() is only supported for floating point types");

        assert(*this != Vec3<T>()
            && "Vector3::normalized() cannot normalize a zero vector");
        return (*this) / length();
    }

    [[nodiscard]] constexpr T dot(const Vec3 &rhs) const
    {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    [[nodiscard]] constexpr Vec3 cross(const Vec3 &rhs) const
    {
        return Vec3((y * rhs.z) - (z * rhs.y), (z * rhs.x) - (x * rhs.z),
            (x * rhs.y) - (y * rhs.x));
    }

    [[nodiscard]] constexpr Vec3 component_wise_mul(const Vec3 &rhs) const
    {
        return Vec3<T>(x * rhs.x, y * rhs.y, z * rhs.z);
    }

    [[nodiscard]] constexpr Vec3 component_wise_div(const Vec3 &rhs) const
    {
        assert(rhs.x != 0 && "Vector3::componentWiseDiv() cannot divide by 0");
        assert(rhs.y != 0 && "Vector3::componentWiseDiv() cannot divide by 0");
        assert(rhs.z != 0 && "Vector3::componentWiseDiv() cannot divide by 0");
        return Vec3<T>(x / rhs.x, y / rhs.y, z / rhs.z);
    }

    constexpr std::array<T, 3> to_array() const { return { x, y, z }; }

    T x { };
    T y { };
    T z { };
};

export {
    [[nodiscard]] constexpr bool operator==(Angle left, Angle right)
    {
        return left.as_radians() == right.as_radians();
    }

    [[nodiscard]] constexpr bool operator!=(Angle left, Angle right)
    {
        return left.as_radians() != right.as_radians();
    }

    [[nodiscard]] constexpr bool operator<(Angle left, Angle right)
    {
        return left.as_radians() < right.as_radians();
    }

    [[nodiscard]] constexpr bool operator>(Angle left, Angle right)
    {
        return left.as_radians() > right.as_radians();
    }

    [[nodiscard]] constexpr bool operator<=(Angle left, Angle right)
    {
        return left.as_radians() <= right.as_radians();
    }

    [[nodiscard]] constexpr bool operator>=(Angle left, Angle right)
    {
        return left.as_radians() >= right.as_radians();
    }

    [[nodiscard]] constexpr Angle operator-(Angle right)
    {
        return Angle::from_radians(-right.as_radians());
    }

    [[nodiscard]] constexpr Angle operator+(Angle left, Angle right)
    {
        return Angle::from_radians(left.as_radians() + right.as_radians());
    }

    constexpr Angle &operator+=(Angle &left, Angle right)
    {
        return left = left + right;
    }

    [[nodiscard]] constexpr Angle operator-(Angle left, Angle right)
    {
        return Angle::from_radians(left.as_radians() - right.as_radians());
    }

    constexpr Angle &operator-=(Angle &left, Angle right)
    {
        return left = left - right;
    }

    [[nodiscard]] constexpr Angle operator*(Angle left, float right)
    {
        return Angle::from_radians(left.as_radians() * right);
    }

    [[nodiscard]] constexpr Angle operator*(float left, Angle right)
    {
        return right * left;
    }

    constexpr Angle &operator*=(Angle &left, float right)
    {
        return left = left * right;
    }

    [[nodiscard]] constexpr Angle operator/(Angle left, float right)
    {
        assert(right != 0.f && "Angle::operator/ cannot divide by 0");
        return Angle::from_radians(left.as_radians() / right);
    }

    constexpr Angle &operator/=(Angle &left, float right)
    {
        assert(right != 0.f && "Angle::operator/= cannot divide by 0");
        return left = left / right;
    }

    [[nodiscard]] constexpr float operator/(Angle left, Angle right)
    {
        assert(
            right.as_radians() != 0.f && "Angle::operator/ cannot divide by 0");
        return left.as_radians() / right.as_radians();
    }

    [[nodiscard]] constexpr Angle operator%(Angle left, Angle right)
    {
        assert(right.as_radians() != 0.f
            && "Angle::operator% cannot modulus by 0");
        return Angle::from_radians(
            priv::positive_remainder(left.as_radians(), right.as_radians()));
    }

    constexpr Angle &operator%=(Angle &left, Angle right)
    {
        assert(right.as_radians() != 0.f
            && "Angle::operator%= cannot modulus by 0");
        return left = left % right;
    }

    template <typename T>
    [[nodiscard]] constexpr Vec2<T> operator-(Vec2<T> right)
    {
        return Vec2<T>(-right.x, -right.z);
    }

    template <typename T>
    constexpr Vec2<T> &operator+=(Vec2<T> &left, Vec2<T> right)
    {
        left.x += right.x;
        left.z += right.z;

        return left;
    }

    template <typename T>
    constexpr Vec2<T> &operator-=(Vec2<T> &left, Vec2<T> right)
    {
        left.x -= right.x;
        left.z -= right.z;

        return left;
    }

    template <typename T>
    [[nodiscard]] constexpr Vec2<T> operator+(Vec2<T> left, Vec2<T> right)
    {
        return Vec2<T>(left.x + right.x, left.z + right.z);
    }

    template <typename T>
    [[nodiscard]] constexpr Vec2<T> operator-(Vec2<T> left, Vec2<T> right)
    {
        return Vec2<T>(left.x - right.x, left.z - right.z);
    }

    template <typename T>
    [[nodiscard]] constexpr Vec2<T> operator*(Vec2<T> left, T right)
    {
        return Vec2<T>(left.x * right, left.z * right);
    }

    template <typename T>
    [[nodiscard]] constexpr Vec2<T> operator*(T left, Vec2<T> right)
    {
        return Vec2<T>(right.x * left, right.z * left);
    }

    template <typename T> constexpr Vec2<T> &operator*=(Vec2<T> &left, T right)
    {
        left.x *= right;
        left.z *= right;

        return left;
    }
    template <typename T>
    [[nodiscard]] constexpr Vec2<T> operator/(Vec2<T> left, T right)
    {
        assert(right != 0 && "Vector2::operator/ cannot divide by 0");
        return Vec2<T>(left.x / right, left.z / right);
    }

    template <typename T> constexpr Vec2<T> &operator/=(Vec2<T> &left, T right)
    {
        assert(right != 0 && "Vector2::operator/= cannot divide by 0");
        left.x /= right;
        left.z /= right;

        return left;
    }

    template <typename T>
    [[nodiscard]] constexpr bool operator==(Vec2<T> left, Vec2<T> right)
    {
        return (left.x == right.x) && (left.z == right.z);
    }

    template <typename T>
    [[nodiscard]] constexpr bool operator!=(Vec2<T> left, Vec2<T> right)
    {
        return !(left == right);
    }

    template <typename T>
    [[nodiscard]] constexpr Vec3<T> operator-(const Vec3<T> &left)
    {
        return Vec3<T>(-left.x, -left.y, -left.z);
    }

    template <typename T>
    constexpr Vec3<T> &operator+=(Vec3<T> &left, const Vec3<T> &right)
    {
        left.x += right.x;
        left.y += right.y;
        left.z += right.z;

        return left;
    }

    template <typename T>
    constexpr Vec3<T> &operator-=(Vec3<T> &left, const Vec3<T> &right)
    {
        left.x -= right.x;
        left.y -= right.y;
        left.z -= right.z;

        return left;
    }

    template <typename T>
    [[nodiscard]] constexpr Vec3<T> operator+(
        const Vec3<T> &left, const Vec3<T> &right)
    {
        return Vec3<T>(left.x + right.x, left.y + right.y, left.z + right.z);
    }

    template <typename T>
    [[nodiscard]] constexpr Vec3<T> operator-(
        const Vec3<T> &left, const Vec3<T> &right)
    {
        return Vec3<T>(left.x - right.x, left.y - right.y, left.z - right.z);
    }

    template <typename T>
    [[nodiscard]] constexpr Vec3<T> operator*(const Vec3<T> &left, T right)
    {
        return Vec3<T>(left.x * right, left.y * right, left.z * right);
    }

    template <typename T>
    [[nodiscard]] constexpr Vec3<T> operator*(T left, const Vec3<T> &right)
    {
        return Vec3<T>(right.x * left, right.y * left, right.z * left);
    }

    template <typename T> constexpr Vec3<T> &operator*=(Vec3<T> &left, T right)
    {
        left.x *= right;
        left.y *= right;
        left.z *= right;

        return left;
    }

    template <typename T>
    [[nodiscard]] constexpr Vec3<T> operator/(const Vec3<T> &left, T right)
    {
        assert(right != 0 && "Vector3::operator/ cannot divide by 0");
        return Vec3<T>(left.x / right, left.y / right, left.z / right);
    }

    template <typename T> constexpr Vec3<T> &operator/=(Vec3<T> &left, T right)
    {
        assert(right != 0 && "Vector3::operator/= cannot divide by 0");
        left.x /= right;
        left.y /= right;
        left.z /= right;

        return left;
    }

    template <typename T>
    [[nodiscard]] constexpr bool operator==(
        const Vec3<T> &left, const Vec3<T> &right)
    {
        return (left.x == right.x) && (left.y == right.y)
            && (left.z == right.z);
    }

    template <typename T>
    [[nodiscard]] constexpr bool operator!=(
        const Vec3<T> &left, const Vec3<T> &right)
    {
        return !(left == right);
    }
}

namespace mathliterals
{
    [[nodiscard]] constexpr Angle operator""_deg(long double angle)
    {
        return Angle::from_degrees(static_cast<float>(angle));
    }

    [[nodiscard]] constexpr Angle operator""_deg(unsigned long long angle)
    {
        return Angle::from_degrees(static_cast<float>(angle));
    }

    [[nodiscard]] constexpr Angle operator""_rad(long double angle)
    {
        return Angle::from_radians(static_cast<float>(angle));
    }

    [[nodiscard]] constexpr Angle operator""_rad(unsigned long long angle)
    {
        return Angle::from_radians(static_cast<float>(angle));
    }
}
