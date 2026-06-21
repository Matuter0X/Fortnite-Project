// ============================================================================
//  MATH LIBRARY — Vectors, Matrices, and World-to-Screen
//  The geometry behind seeing through walls like they're made of glass.
// ============================================================================

#pragma once

#include <cmath>
#include <unordered_map>

namespace math {

// ============================================================================
//  Constants
// ============================================================================

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;

// ============================================================================
//  Vector2 — 2D screen coordinates
// ============================================================================

struct Vector2 {
    float x = 0, y = 0;

    Vector2() = default;
    Vector2(float x, float y) : x(x), y(y) {}

    float Length() const { return std::sqrt(x * x + y * y); }

    Vector2 operator+(const Vector2& v) const { return { x + v.x, y + v.y }; }
    Vector2 operator-(const Vector2& v) const { return { x - v.x, y - v.y }; }
    Vector2 operator*(float s) const { return { x * s, y * s }; }
    bool operator==(const Vector2& v) const { return x == v.x && y == v.y; }
};

// ============================================================================
//  Vector3 — 3D world coordinates (Unreal: X=forward, Y=right, Z=up)
// ============================================================================

struct Vector3 {
    float x = 0, y = 0, z = 0;

    Vector3() = default;
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    float Length() const { return std::sqrt(x * x + y * y + z * z); }

    float Distance(const Vector3& other) const {
        return (*this - other).Length();
    }

    Vector3 operator+(const Vector3& v) const { return { x + v.x, y + v.y, z + v.z }; }
    Vector3 operator-(const Vector3& v) const { return { x - v.x, y - v.y, z - v.z }; }
    Vector3 operator*(float s) const { return { x * s, y * s, z * s }; }

    bool IsZero() const {
        return std::abs(x) < 0.001f && std::abs(y) < 0.001f && std::abs(z) < 0.001f;
    }
};

// ============================================================================
//  FRotator — Unreal rotation (degrees, pitch/yaw/roll)
// ============================================================================

struct FRotator {
    float pitch = 0;  // Up/Down
    float yaw = 0;    // Left/Right
    float roll = 0;   // Tilt
};

// ============================================================================
//  FTransform — Position + Rotation + Scale (how UE stores bone transforms)
// ============================================================================

struct FTransform {
    // Quaternion rotation (x, y, z, w)
    float rotX = 0, rotY = 0, rotZ = 0, rotW = 1;
    // Translation
    Vector3 translation;
    // Scale
    Vector3 scale{ 1, 1, 1 };
};

// ============================================================================
//  Matrix4x4 — The workhorse for coordinate transforms
// ============================================================================

struct Matrix4x4 {
    float m[4][4] = {};

    Matrix4x4 operator*(const Matrix4x4& other) const;

    // Identity matrix
    static Matrix4x4 Identity();
};

// ============================================================================
//  Core math functions
// ============================================================================

/// Build a rotation matrix from Unreal-style Euler angles (degrees)
Matrix4x4 RotationMatrix(const FRotator& rotation);

/// World-to-screen projection
/// Transforms a 3D world position to 2D screen coordinates using
/// the view-projection matrix. Returns true if the point is in front
/// of the camera (visible).
bool WorldToScreen(
    const Vector3& worldPos,
    const Matrix4x4& viewProjectionMatrix,
    int screenWidth, int screenHeight,
    Vector2& screenOut
);

/// Build the full view-projection matrix from camera parameters
/// This combines the view matrix (camera transform inverse) and
/// the perspective projection matrix (FOV, aspect ratio)
Matrix4x4 BuildViewProjectionMatrix(
    const Vector3& cameraLocation,
    const FRotator& cameraRotation,
    float fovDegrees,
    float screenWidth,
    float screenHeight
);

/// Transform a bone's local-space FTransform to world-space position
/// using the mesh component's ComponentToWorld transform
Vector3 GetBoneWorldPosition(
    const FTransform& bone,
    const FTransform& componentToWorld
);

/// Convert Unreal Units to meters (1 UU = 1 cm)
inline float UnrealUnitsToMeters(float uu) { return uu / 100.0f; }

} // namespace math
