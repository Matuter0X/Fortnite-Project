// ============================================================================
//  MATH LIBRARY — Implementation
//  Where linear algebra meets wall hacks. Beautiful, really.
// ============================================================================

#include "math.h"
#include <algorithm>

namespace math {

// ============================================================================
//  Matrix4x4 — multiplication and identity
// ============================================================================

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& other) const {
    Matrix4x4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = 0;
            for (int k = 0; k < 4; ++k) {
                result.m[i][j] += m[i][k] * other.m[k][j];
            }
        }
    }
    return result;
}

Matrix4x4 Matrix4x4::Identity() {
    Matrix4x4 mat;
    mat.m[0][0] = 1; mat.m[1][1] = 1; mat.m[2][2] = 1; mat.m[3][3] = 1;
    return mat;
}

// ============================================================================
//  RotationMatrix — Euler angles (degrees) to rotation matrix
//  
//  UE coordinate system: X=forward, Y=right, Z=up (left-handed)
//  Rotation order: Yaw → Pitch → Roll
// ============================================================================

Matrix4x4 RotationMatrix(const FRotator& rotation) {
    float sp = std::sin(rotation.pitch * DEG2RAD);
    float cp = std::cos(rotation.pitch * DEG2RAD);
    float sy = std::sin(rotation.yaw * DEG2RAD);
    float cy = std::cos(rotation.yaw * DEG2RAD);
    float sr = std::sin(rotation.roll * DEG2RAD);
    float cr = std::cos(rotation.roll * DEG2RAD);

    Matrix4x4 mat;

    // Combined Yaw * Pitch * Roll rotation matrix
    // Row 0: Forward vector
    mat.m[0][0] = cp * cy;
    mat.m[0][1] = cp * sy;
    mat.m[0][2] = sp;
    mat.m[0][3] = 0;

    // Row 1: Right vector
    mat.m[1][0] = sr * sp * cy - cr * sy;
    mat.m[1][1] = sr * sp * sy + cr * cy;
    mat.m[1][2] = -sr * cp;
    mat.m[1][3] = 0;

    // Row 2: Up vector
    mat.m[2][0] = -(cr * sp * cy + sr * sy);
    mat.m[2][1] = cy * sr - cr * sp * sy;
    mat.m[2][2] = cr * cp;
    mat.m[2][3] = 0;

    // Row 3: Translation (none for pure rotation)
    mat.m[3][0] = 0;
    mat.m[3][1] = 0;
    mat.m[3][2] = 0;
    mat.m[3][3] = 1;

    return mat;
}

// ============================================================================
//  WorldToScreen — The bread and butter of ESP
//  
//  Pipeline: World → Clip (multiply by VP matrix) → NDC (perspective divide)
//            → Screen (map to pixel coordinates)
// ============================================================================

bool WorldToScreen(
    const Vector3& worldPos,
    const Matrix4x4& viewProjectionMatrix,
    int screenWidth, int screenHeight,
    Vector2& screenOut
) {
    const auto& vpm = viewProjectionMatrix;

    // Multiply world position by view-projection matrix → clip space
    // w = homogeneous coordinate for perspective divide
    float w = vpm.m[3][0] * worldPos.x 
            + vpm.m[3][1] * worldPos.y 
            + vpm.m[3][2] * worldPos.z 
            + vpm.m[3][3];

    // If w < 0, the point is behind the camera — bail
    if (w < 0.001f) {
        return false;
    }

    float x = vpm.m[0][0] * worldPos.x 
            + vpm.m[0][1] * worldPos.y 
            + vpm.m[0][2] * worldPos.z 
            + vpm.m[0][3];

    float y = vpm.m[1][0] * worldPos.x 
            + vpm.m[1][1] * worldPos.y 
            + vpm.m[1][2] * worldPos.z 
            + vpm.m[1][3];

    // Perspective divide: clip space → NDC (normalized device coordinates)
    // NDC range: [-1, 1] for both X and Y
    float ndcX = x / w;
    float ndcY = y / w;

    // NDC → Screen coordinates
    // Map [-1, 1] to [0, screenWidth] and [0, screenHeight]
    // Y is flipped because screen Y goes down, NDC Y goes up
    screenOut.x = (screenWidth  / 2.0f) * (1.0f + ndcX);
    screenOut.y = (screenHeight / 2.0f) * (1.0f - ndcY);

    return true;
}

// ============================================================================
//  BuildViewProjectionMatrix
//  
//  Combines the camera's view matrix (inverse of camera transform)
//  with a perspective projection matrix to create the full VP matrix
//  used by WorldToScreen.
// ============================================================================

Matrix4x4 BuildViewProjectionMatrix(
    const Vector3& cameraLocation,
    const FRotator& cameraRotation,
    float fovDegrees,
    float screenWidth,
    float screenHeight
) {
    // Step 1: Build rotation matrix from camera rotation
    Matrix4x4 rotMatrix = RotationMatrix(cameraRotation);

    // Extract basis vectors (forward, right, up) from rotation matrix
    Vector3 forward = { rotMatrix.m[0][0], rotMatrix.m[0][1], rotMatrix.m[0][2] };
    Vector3 right   = { rotMatrix.m[1][0], rotMatrix.m[1][1], rotMatrix.m[1][2] };
    Vector3 up      = { rotMatrix.m[2][0], rotMatrix.m[2][1], rotMatrix.m[2][2] };

    // Step 2: Build view matrix (camera-space transform)
    // This is the inverse of the camera's world transform
    // For an orthonormal basis, the inverse rotation is the transpose
    Matrix4x4 view = Matrix4x4::Identity();

    // Transpose of rotation (rows become columns)
    view.m[0][0] = right.x;   view.m[0][1] = right.y;   view.m[0][2] = right.z;
    view.m[1][0] = up.x;      view.m[1][1] = up.y;      view.m[1][2] = up.z;
    view.m[2][0] = forward.x; view.m[2][1] = forward.y; view.m[2][2] = forward.z;

    // Translation (negative dot product with camera position)
    view.m[0][3] = -(right.x * cameraLocation.x + right.y * cameraLocation.y + right.z * cameraLocation.z);
    view.m[1][3] = -(up.x * cameraLocation.x + up.y * cameraLocation.y + up.z * cameraLocation.z);
    view.m[2][3] = -(forward.x * cameraLocation.x + forward.y * cameraLocation.y + forward.z * cameraLocation.z);
    view.m[3][3] = 1.0f;

    // Step 3: Build perspective projection matrix
    float aspectRatio = screenWidth / screenHeight;
    float fovRadHalf = (fovDegrees * DEG2RAD) / 2.0f;
    float tanHalfFov = std::tan(fovRadHalf);

    // Near/far planes (UE defaults)
    float zNear = 10.0f;     // 10 cm
    float zFar = 100000.0f;  // 1 km

    Matrix4x4 proj;
    proj.m[0][0] = 1.0f / (aspectRatio * tanHalfFov);
    proj.m[1][1] = 1.0f / tanHalfFov;
    proj.m[2][2] = zFar / (zFar - zNear);
    proj.m[2][3] = -(zFar * zNear) / (zFar - zNear);
    proj.m[3][2] = 1.0f;
    proj.m[3][3] = 0.0f;

    // Step 4: Combine view * projection
    return view * proj;
}

// ============================================================================
//  GetBoneWorldPosition
//  
//  Transforms a bone from local (component) space to world space.
//  Uses the component's ComponentToWorld FTransform to apply the
//  rotation (quaternion) and translation.
// ============================================================================

Vector3 GetBoneWorldPosition(
    const FTransform& bone,
    const FTransform& componentToWorld
) {
    // Apply quaternion rotation from componentToWorld to bone's translation
    // Quaternion rotation formula: v' = q * v * q^(-1)
    // Optimized version using the rotation matrix form:

    float qx = componentToWorld.rotX;
    float qy = componentToWorld.rotY;
    float qz = componentToWorld.rotZ;
    float qw = componentToWorld.rotW;

    // Rotate bone position by quaternion
    float bx = bone.translation.x;
    float by = bone.translation.y;
    float bz = bone.translation.z;

    // Quaternion rotation matrix elements (computed from q)
    float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    Vector3 rotated;
    rotated.x = (1.0f - 2.0f * (yy + zz)) * bx + (2.0f * (xy - wz)) * by + (2.0f * (xz + wy)) * bz;
    rotated.y = (2.0f * (xy + wz)) * bx + (1.0f - 2.0f * (xx + zz)) * by + (2.0f * (yz - wx)) * bz;
    rotated.z = (2.0f * (xz - wy)) * bx + (2.0f * (yz + wx)) * by + (1.0f - 2.0f * (xx + yy)) * bz;

    // Apply scale
    rotated.x *= componentToWorld.scale.x;
    rotated.y *= componentToWorld.scale.y;
    rotated.z *= componentToWorld.scale.z;

    // Add world translation
    return rotated + componentToWorld.translation;
}

} // namespace math
