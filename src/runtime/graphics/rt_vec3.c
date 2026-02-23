//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_vec3.c
// Purpose: 3D vector mathematics (x, y, z doubles) for Viper graphics and
//   simulation. Provides immutable Vec3 objects with arithmetic (+,-,×,÷), dot
//   product, cross product, length/normalize, distance, linear interpolation,
//   reflection, and angle operations. Used for 3D positions, surface normals,
//   lighting directions, and RGB color triples (r=x, g=y, b=z).
//
// Key invariants:
//   - Vec3 stores three doubles (x, y, z); 24 bytes, no padding.
//   - Coordinate system: right-handed Cartesian (OpenGL convention):
//       +X = right,  +Y = up,  +Z = toward the viewer (out of screen).
//   - Cross product: v × w gives a vector perpendicular to both, following the
//     right-hand rule: curl fingers from v to w, thumb points in result direction.
//   - Normalize returns a unit vector (length 1). Normalizing a zero vector
//     returns Vec3(0,0,0) — no trap or NaN.
//   - All operations return new Vec3 objects (no mutation), making Vec3 safe
//     for concurrent reads without locking.
//   - Vec3 uses a thread-local LIFO free-list pool (VEC3_POOL_CAPACITY = 32)
//     identical in design to the Vec2 pool, to amortize GC pressure in
//     lighting and physics inner loops.
//
// Ownership/Lifetime:
//   - Vec3 objects are GC-managed. Pool slots are reclaimed by the pool's
//     finalizer path; non-pooled Vec3s are collected by the standard GC.
//     Callers must not free Vec3s manually.
//
// Links: src/runtime/graphics/rt_vec3.h (public API),
//        src/runtime/graphics/rt_vec2.c (2D counterpart),
//        src/runtime/graphics/rt_mat3.c (matrix–vector transform consumer)
//
//===----------------------------------------------------------------------===//

#include "rt_vec3.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>

// ============================================================================
// Thread-local free-list pool (P2-3.6)
// ============================================================================
#define VEC3_POOL_CAPACITY 32

static _Thread_local void *vec3_pool_buf_[VEC3_POOL_CAPACITY];
static _Thread_local int vec3_pool_top_ = 0;

static void vec3_pool_return(void *p)
{
    if (vec3_pool_top_ < VEC3_POOL_CAPACITY)
    {
        rt_obj_resurrect(p);
        rt_obj_set_finalizer(p, vec3_pool_return);
        vec3_pool_buf_[vec3_pool_top_++] = p;
    }
}

/// @brief Internal Vec3 implementation structure.
///
/// Stores the X, Y, and Z components of a 3D vector as double-precision
/// floating-point values. The structure is allocated as a Viper object
/// with reference counting support.
///
/// @note Vec3 is immutable - all operations create new instances.
typedef struct
{
    double x; ///< X component (horizontal axis, positive = right)
    double y; ///< Y component (vertical axis, positive = up)
    double z; ///< Z component (depth axis, positive = toward viewer in RH coords)
} ViperVec3;

/// @brief Allocate and initialize a new Vec3 with the given components.
///
/// This internal helper allocates a Vec3 object through the Viper object
/// system and initializes it with the provided X, Y, and Z values.
///
/// @param x The X component value.
/// @param y The Y component value.
/// @param z The Z component value.
///
/// @return Pointer to the newly allocated Vec3. Traps on allocation failure.
///
/// @note This is an internal function - use rt_vec3_new() for public API.
static ViperVec3 *vec3_alloc(double x, double y, double z)
{
    ViperVec3 *v;
    if (vec3_pool_top_ > 0)
    {
        v = (ViperVec3 *)vec3_pool_buf_[--vec3_pool_top_];
    }
    else
    {
        v = (ViperVec3 *)rt_obj_new_i64(0, (int64_t)sizeof(ViperVec3));
        if (!v)
        {
            rt_trap("Vec3: memory allocation failed");
            return NULL; // Unreachable after trap
        }
        rt_obj_set_finalizer(v, vec3_pool_return);
    }
    v->x = x;
    v->y = y;
    v->z = z;
    return v;
}

//=============================================================================
// Constructors
//=============================================================================

/// @brief Creates a new 3D vector with the specified X, Y, and Z components.
///
/// This is the primary constructor for creating Vec3 instances with custom
/// component values.
///
/// **Usage example:**
/// ```
/// Dim position = Vec3.New(100.0, 50.0, 25.0)  ' 3D position
/// Dim velocity = Vec3.New(5.0, -2.0, 1.0)    ' 3D velocity
/// Dim normal = Vec3.New(0.0, 1.0, 0.0)       ' Up direction
/// Dim color = Vec3.New(1.0, 0.5, 0.0)        ' Orange as RGB
/// ```
///
/// @param x The X component (horizontal position/direction).
/// @param y The Y component (vertical position/direction).
/// @param z The Z component (depth position/direction).
///
/// @return A new Vec3 object with the specified components.
///
/// @note O(1) time complexity.
/// @note The returned Vec3 is reference-counted and garbage collected.
///
/// @see rt_vec3_zero For creating a zero vector
/// @see rt_vec3_one For creating a unit vector (1, 1, 1)
void *rt_vec3_new(double x, double y, double z)
{
    return vec3_alloc(x, y, z);
}

/// @brief Creates a zero vector (0, 0, 0).
///
/// Returns a Vec3 with all components set to zero. The zero vector is the
/// identity element for vector addition and represents "no direction" or
/// "origin point."
///
/// **Mathematical Properties:**
/// - v + Vec3.Zero() = v (additive identity)
/// - v * 0 = Vec3.Zero()
/// - Length of zero vector = 0
///
/// **Usage example:**
/// ```
/// Dim origin = Vec3.Zero()       ' World origin (0, 0, 0)
/// Dim velocity = Vec3.Zero()     ' Not moving
/// Dim acceleration = Vec3.Zero() ' No acceleration
/// ```
///
/// @return A new Vec3 object with components (0, 0, 0).
///
/// @note O(1) time complexity.
///
/// @see rt_vec3_one For a unit vector
/// @see rt_vec3_len For checking if a vector is zero-length
void *rt_vec3_zero(void)
{
    return vec3_alloc(0.0, 0.0, 0.0);
}

/// @brief Creates a unit vector (1, 1, 1).
///
/// Returns a Vec3 with all components set to one. Note that this vector
/// has a length of sqrt(3) ≈ 1.732, not 1. For true unit vectors, use
/// cardinal directions or normalize any non-zero vector.
///
/// **Usage example:**
/// ```
/// Dim scale = Vec3.One()              ' (1, 1, 1)
/// Dim doubled = scale.Mul(2)          ' (2, 2, 2)
///
/// ' Uniform scaling
/// Dim objectScale = Vec3.One().Mul(0.5)  ' Half size in all dimensions
/// ```
///
/// **Note on Length:**
/// ```
/// Vec3.One() length = sqrt(1² + 1² + 1²) = sqrt(3) ≈ 1.732
/// ```
///
/// @return A new Vec3 object with components (1, 1, 1).
///
/// @note O(1) time complexity.
/// @note Length is sqrt(3), not 1. Use rt_vec3_norm() for true unit vectors.
///
/// @see rt_vec3_zero For a zero vector
/// @see rt_vec3_norm For creating unit vectors
void *rt_vec3_one(void)
{
    return vec3_alloc(1.0, 1.0, 1.0);
}

//=============================================================================
// Property Accessors
//=============================================================================

/// @brief Gets the X component of the vector.
///
/// Returns the horizontal component of the 3D vector. In a standard
/// right-handed coordinate system, positive X points to the right.
///
/// **Usage example:**
/// ```
/// Dim pos = Vec3.New(100.0, 50.0, 25.0)
/// Print pos.X    ' Outputs: 100
/// ```
///
/// @param v Pointer to a Vec3 object. Must not be NULL.
///
/// @return The X component value as a double.
///
/// @note O(1) time complexity.
/// @note Traps with "Vec3.X: null vector" if v is NULL.
///
/// @see rt_vec3_y For the Y component
/// @see rt_vec3_z For the Z component
double rt_vec3_x(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.X: null vector");
        return 0.0;
    }
    return ((ViperVec3 *)v)->x;
}

/// @brief Gets the Y component of the vector.
///
/// Returns the vertical component of the 3D vector. In a standard
/// coordinate system, positive Y typically points upward.
///
/// **Usage example:**
/// ```
/// Dim pos = Vec3.New(100.0, 50.0, 25.0)
/// Print pos.Y    ' Outputs: 50
///
/// ' Check if above ground
/// If pos.Y > 0 Then
///     Print "Above ground level"
/// End If
/// ```
///
/// @param v Pointer to a Vec3 object. Must not be NULL.
///
/// @return The Y component value as a double.
///
/// @note O(1) time complexity.
/// @note Traps with "Vec3.Y: null vector" if v is NULL.
///
/// @see rt_vec3_x For the X component
/// @see rt_vec3_z For the Z component
double rt_vec3_y(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.Y: null vector");
        return 0.0;
    }
    return ((ViperVec3 *)v)->y;
}

/// @brief Gets the Z component of the vector.
///
/// Returns the depth component of the 3D vector. In a right-handed
/// coordinate system, positive Z points toward the viewer.
///
/// **Usage example:**
/// ```
/// Dim pos = Vec3.New(100.0, 50.0, 25.0)
/// Print pos.Z    ' Outputs: 25
///
/// ' Check depth for rendering order
/// If objectA.Z > objectB.Z Then
///     Print "Object A is in front"
/// End If
/// ```
///
/// @param v Pointer to a Vec3 object. Must not be NULL.
///
/// @return The Z component value as a double.
///
/// @note O(1) time complexity.
/// @note Traps with "Vec3.Z: null vector" if v is NULL.
///
/// @see rt_vec3_x For the X component
/// @see rt_vec3_y For the Y component
double rt_vec3_z(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.Z: null vector");
        return 0.0;
    }
    return ((ViperVec3 *)v)->z;
}

//=============================================================================
// Arithmetic Operations
//=============================================================================

/// @brief Adds two vectors component-wise.
///
/// Performs vector addition: result = (a.x + b.x, a.y + b.y, a.z + b.z)
///
/// **Usage example:**
/// ```
/// Dim pos = Vec3.New(100.0, 50.0, 25.0)
/// Dim velocity = Vec3.New(5.0, -2.0, 1.0)
/// Dim newPos = pos.Add(velocity)   ' (105, 48, 26)
///
/// ' Combine forces
/// Dim totalForce = gravity.Add(wind).Add(thrust)
/// ```
///
/// @param a First vector operand. Must not be NULL.
/// @param b Second vector operand. Must not be NULL.
///
/// @return A new Vec3 containing the component-wise sum.
///
/// @note O(1) time complexity.
/// @note Vector addition is commutative: a + b = b + a
/// @note Traps with "Vec3.Add: null vector" if either operand is NULL.
///
/// @see rt_vec3_sub For vector subtraction
void *rt_vec3_add(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Add: null vector");
        return NULL;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    return vec3_alloc(va->x + vb->x, va->y + vb->y, va->z + vb->z);
}

/// @brief Subtracts vector b from vector a component-wise.
///
/// Performs vector subtraction: result = (a.x - b.x, a.y - b.y, a.z - b.z)
///
/// Subtraction finds the vector from b to a.
///
/// **Usage example:**
/// ```
/// Dim target = Vec3.New(200.0, 150.0, 100.0)
/// Dim position = Vec3.New(100.0, 100.0, 50.0)
/// Dim direction = target.Sub(position)  ' Vector from position to target
///
/// ' Calculate relative velocity
/// Dim relativeVel = shipVelocity.Sub(asteroidVelocity)
/// ```
///
/// @param a Vector to subtract from (minuend). Must not be NULL.
/// @param b Vector to subtract (subtrahend). Must not be NULL.
///
/// @return A new Vec3 containing the component-wise difference (a - b).
///
/// @note O(1) time complexity.
/// @note Not commutative: a - b ≠ b - a
/// @note Traps with "Vec3.Sub: null vector" if either operand is NULL.
///
/// @see rt_vec3_add For vector addition
void *rt_vec3_sub(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Sub: null vector");
        return NULL;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    return vec3_alloc(va->x - vb->x, va->y - vb->y, va->z - vb->z);
}

/// @brief Multiplies a vector by a scalar value.
///
/// Scales all components of the vector by the given scalar:
/// result = (v.x * s, v.y * s, v.z * s)
///
/// **Effect of scalar values:**
/// - s > 1: Lengthens the vector
/// - 0 < s < 1: Shortens the vector
/// - s = 0: Returns zero vector
/// - s < 0: Reverses direction and scales
///
/// **Usage example:**
/// ```
/// Dim direction = Vec3.New(1.0, 0.0, 0.0)
/// Dim speed = 5.0
/// Dim velocity = direction.Mul(speed)  ' (5, 0, 0)
/// ```
///
/// @param v Vector to scale. Must not be NULL.
/// @param s Scalar multiplier.
///
/// @return A new Vec3 with components scaled by s.
///
/// @note O(1) time complexity.
/// @note Traps with "Vec3.Mul: null vector" if v is NULL.
///
/// @see rt_vec3_div For scalar division
void *rt_vec3_mul(void *v, double s)
{
    if (!v)
    {
        rt_trap("Vec3.Mul: null vector");
        return NULL;
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec3_alloc(vec->x * s, vec->y * s, vec->z * s);
}

/// @brief Divides a vector by a scalar value.
///
/// Divides all components of the vector by the given scalar:
/// result = (v.x / s, v.y / s, v.z / s)
///
/// **Usage example:**
/// ```
/// Dim velocity = Vec3.New(10.0, 6.0, 4.0)
/// Dim halfSpeed = velocity.Div(2.0)     ' (5, 3, 2)
///
/// ' Normalize manually
/// Dim direction = velocity.Div(velocity.Len())
/// ```
///
/// @param v Vector to divide. Must not be NULL.
/// @param s Scalar divisor. Must not be zero.
///
/// @return A new Vec3 with components divided by s.
///
/// @note O(1) time complexity.
/// @note Traps with "Vec3.Div: null vector" if v is NULL.
/// @note Traps with "Vec3.Div: division by zero" if s is 0.
///
/// @see rt_vec3_mul For scalar multiplication
/// @see rt_vec3_norm For normalizing to unit length
void *rt_vec3_div(void *v, double s)
{
    if (!v)
    {
        rt_trap("Vec3.Div: null vector");
        return NULL;
    }
    if (s == 0.0)
    {
        rt_trap("Vec3.Div: division by zero");
        return NULL;
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec3_alloc(vec->x / s, vec->y / s, vec->z / s);
}

/// @brief Negates a vector (reverses its direction).
///
/// Returns a vector pointing in the opposite direction with the same magnitude:
/// result = (-v.x, -v.y, -v.z)
///
/// **Usage example:**
/// ```
/// Dim forward = Vec3.New(0.0, 0.0, 1.0)
/// Dim backward = forward.Neg()          ' (0, 0, -1)
///
/// ' Reflect velocity on collision
/// velocity = velocity.Neg()
/// ```
///
/// @param v Vector to negate. Must not be NULL.
///
/// @return A new Vec3 pointing in the opposite direction.
///
/// @note O(1) time complexity.
/// @note Traps with "Vec3.Neg: null vector" if v is NULL.
/// @note Equivalent to v.Mul(-1)
///
/// @see rt_vec3_mul For scalar multiplication
void *rt_vec3_neg(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.Neg: null vector");
        return NULL;
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec3_alloc(-vec->x, -vec->y, -vec->z);
}

//=============================================================================
// Vector Products
//=============================================================================

/// @brief Computes the dot product (scalar product) of two vectors.
///
/// The dot product is a fundamental operation that returns a scalar value:
/// a · b = a.x * b.x + a.y * b.y + a.z * b.z = |a| * |b| * cos(θ)
///
/// where θ is the angle between the vectors.
///
/// **Common Uses:**
/// - Check if vectors are perpendicular: dot == 0
/// - Check if vectors point in same direction: dot > 0
/// - Calculate lighting intensity (N · L for diffuse lighting)
/// - Project one vector onto another
///
/// **Usage example:**
/// ```
/// Dim normal = Vec3.New(0.0, 1.0, 0.0)    ' Surface normal (up)
/// Dim lightDir = Vec3.New(0.5, 0.5, 0.0).Norm()
/// Dim intensity = normal.Dot(lightDir)    ' Diffuse lighting
///
/// ' Check if in front of camera
/// Dim toObject = objectPos.Sub(cameraPos).Norm()
/// If cameraForward.Dot(toObject) > 0 Then
///     Print "Object is in front of camera"
/// End If
/// ```
///
/// @param a First vector. Must not be NULL.
/// @param b Second vector. Must not be NULL.
///
/// @return The dot product as a scalar value.
///
/// @note O(1) time complexity.
/// @note Dot product is commutative: a · b = b · a
/// @note Traps with "Vec3.Dot: null vector" if either operand is NULL.
///
/// @see rt_vec3_cross For the cross product
double rt_vec3_dot(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Dot: null vector");
        return 0.0;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    return va->x * vb->x + va->y * vb->y + va->z * vb->z;
}

/// @brief Computes the cross product of two vectors.
///
/// The 3D cross product returns a vector perpendicular to both input vectors:
/// a × b = (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx)
///
/// **Properties:**
/// - Result is perpendicular to both a and b
/// - |a × b| = |a| * |b| * sin(θ)
/// - Direction follows right-hand rule
/// - Anti-commutative: a × b = -(b × a)
///
/// **Right-Hand Rule:**
/// ```
/// Point fingers in direction of a
/// Curl fingers toward b
/// Thumb points in direction of a × b
///
///        a × b (result)
///          ↑
///          |
///          |  b
///          | /
///          |/_____ a
/// ```
///
/// **Common Uses:**
/// - Calculate surface normals: normal = (v1 - v0) × (v2 - v0)
/// - Calculate torque: τ = r × F
/// - Find perpendicular vectors
/// - Determine winding order of triangles
///
/// **Usage example:**
/// ```
/// ' Calculate surface normal from triangle vertices
/// Dim edge1 = v1.Sub(v0)
/// Dim edge2 = v2.Sub(v0)
/// Dim normal = edge1.Cross(edge2).Norm()
///
/// ' Calculate torque
/// Dim torque = leverArm.Cross(force)
///
/// ' Create coordinate frame
/// Dim right = Vec3.New(1, 0, 0)
/// Dim up = Vec3.New(0, 1, 0)
/// Dim forward = right.Cross(up)  ' (0, 0, -1) in RH system
/// ```
///
/// @param a First vector. Must not be NULL.
/// @param b Second vector. Must not be NULL.
///
/// @return A new Vec3 perpendicular to both inputs.
///
/// @note O(1) time complexity.
/// @note Cross product is anti-commutative: a × b = -(b × a)
/// @note Traps with "Vec3.Cross: null vector" if either operand is NULL.
///
/// @see rt_vec3_dot For the dot product
/// @see rt_vec2_cross For the 2D cross product (returns scalar)
void *rt_vec3_cross(void *a, void *b)
{
    // 3D cross product: a × b = (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx)
    if (!a || !b)
    {
        rt_trap("Vec3.Cross: null vector");
        return NULL;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    double x = va->y * vb->z - va->z * vb->y;
    double y = va->z * vb->x - va->x * vb->z;
    double z = va->x * vb->y - va->y * vb->x;
    return vec3_alloc(x, y, z);
}

//=============================================================================
// Length and Distance
//=============================================================================

/// @brief Computes the squared length (magnitude squared) of the vector.
///
/// Returns |v|² = v.x² + v.y² + v.z²
///
/// The squared length avoids the expensive square root operation, making it
/// ideal for comparisons where the actual length isn't needed.
///
/// **Performance Optimization:**
/// ```
/// ' Instead of: If a.Len() < b.Len() Then ...
/// If a.LenSq() < b.LenSq() Then ...   ' Faster!
///
/// ' Instead of: If v.Len() < 10 Then ...
/// If v.LenSq() < 100 Then ...          ' 100 = 10²
/// ```
///
/// **Usage example:**
/// ```
/// Dim v = Vec3.New(1.0, 2.0, 2.0)
/// Print v.LenSq()    ' 9 (= 1² + 2² + 2²)
/// Print v.Len()      ' 3 (= sqrt(9))
/// ```
///
/// @param v Vector to measure. Must not be NULL.
///
/// @return The squared length as a non-negative value.
///
/// @note O(1) time complexity.
/// @note Prefer LenSq over Len when only comparing magnitudes.
/// @note Traps with "Vec3.LenSq: null vector" if v is NULL.
///
/// @see rt_vec3_len For the actual length
double rt_vec3_len_sq(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.LenSq: null vector");
        return 0.0;
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec->x * vec->x + vec->y * vec->y + vec->z * vec->z;
}

/// @brief Computes the length (magnitude) of the vector.
///
/// Returns the Euclidean length: |v| = sqrt(v.x² + v.y² + v.z²)
///
/// **Usage example:**
/// ```
/// Dim velocity = Vec3.New(1.0, 2.0, 2.0)
/// Dim speed = velocity.Len()    ' 3.0
///
/// ' Get direction (unit vector)
/// Dim direction = velocity.Div(speed)
/// ```
///
/// @param v Vector to measure. Must not be NULL.
///
/// @return The length as a non-negative value.
///
/// @note O(1) time complexity (involves sqrt).
/// @note For comparisons, prefer rt_vec3_len_sq to avoid sqrt.
/// @note Traps if v is NULL (via rt_vec3_len_sq).
///
/// @see rt_vec3_len_sq For squared length (faster for comparisons)
/// @see rt_vec3_norm For getting a unit-length vector
double rt_vec3_len(void *v)
{
    return sqrt(rt_vec3_len_sq(v));
}

/// @brief Computes the Euclidean distance between two points.
///
/// Returns the straight-line distance in 3D space:
/// dist = |b - a| = sqrt((b.x-a.x)² + (b.y-a.y)² + (b.z-a.z)²)
///
/// **Usage example:**
/// ```
/// Dim player = Vec3.New(100.0, 0.0, 100.0)
/// Dim enemy = Vec3.New(150.0, 10.0, 130.0)
/// Dim distance = player.Dist(enemy)
///
/// If distance < 50.0 Then
///     Print "Enemy is nearby!"
/// End If
/// ```
///
/// @param a First point (start). Must not be NULL.
/// @param b Second point (end). Must not be NULL.
///
/// @return The distance as a non-negative value.
///
/// @note O(1) time complexity.
/// @note Distance is symmetric: a.Dist(b) = b.Dist(a)
/// @note Traps with "Vec3.Dist: null vector" if either point is NULL.
///
/// @see rt_vec3_len For the length of a single vector
double rt_vec3_dist(void *a, void *b)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Dist: null vector");
        return 0.0;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    double dx = vb->x - va->x;
    double dy = vb->y - va->y;
    double dz = vb->z - va->z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

//=============================================================================
// Normalization and Interpolation
//=============================================================================

/// @brief Normalizes the vector to unit length (length = 1).
///
/// Returns a vector pointing in the same direction with length 1:
/// result = v / |v|
///
/// Unit vectors are essential for representing pure direction without magnitude.
/// They are used extensively in graphics for normals, directions, and lighting.
///
/// **Special Case:**
/// If the input vector has zero length, returns a zero vector (0, 0, 0) rather
/// than trapping. This prevents division by zero.
///
/// **Usage example:**
/// ```
/// Dim velocity = Vec3.New(3.0, 4.0, 0.0)
/// Dim direction = velocity.Norm()   ' (0.6, 0.8, 0) - length is 1.0
///
/// ' Use unit vector to move at constant speed
/// Dim speed = 10.0
/// Dim movement = direction.Mul(speed)
///
/// ' Surface normal
/// Dim normal = surfaceDirection.Norm()
/// ```
///
/// @param v Vector to normalize. Must not be NULL.
///
/// @return A new unit vector (length = 1), or zero vector if input has zero length.
///
/// @note O(1) time complexity.
/// @note Safe for zero-length vectors (returns zero vector).
/// @note Traps with "Vec3.Norm: null vector" if v is NULL.
///
/// @see rt_vec3_len For getting the length
/// @see rt_vec3_div For manual normalization
void *rt_vec3_norm(void *v)
{
    if (!v)
    {
        rt_trap("Vec3.Norm: null vector");
        return NULL;
    }
    double len = rt_vec3_len(v);
    if (len == 0.0)
    {
        // Return zero vector for zero-length input
        return vec3_alloc(0.0, 0.0, 0.0);
    }
    ViperVec3 *vec = (ViperVec3 *)v;
    return vec3_alloc(vec->x / len, vec->y / len, vec->z / len);
}

/// @brief Linearly interpolates between two vectors.
///
/// Returns a point along the line from a to b based on parameter t:
/// result = a + (b - a) * t = a * (1 - t) + b * t
///
/// **Interpolation values:**
/// - t = 0: Returns a
/// - t = 0.5: Returns midpoint between a and b
/// - t = 1: Returns b
/// - t < 0 or t > 1: Extrapolates beyond a and b
///
/// **Usage example:**
/// ```
/// Dim start = Vec3.New(0.0, 0.0, 0.0)
/// Dim target = Vec3.New(100.0, 50.0, 25.0)
///
/// ' Animate position over time
/// Dim progress = elapsedTime / totalTime   ' 0.0 to 1.0
/// Dim currentPos = start.Lerp(target, progress)
///
/// ' Smooth camera follow
/// camera = camera.Lerp(targetPos, 0.1)
///
/// ' Blend between two colors (using Vec3 as RGB)
/// Dim red = Vec3.New(1, 0, 0)
/// Dim blue = Vec3.New(0, 0, 1)
/// Dim purple = red.Lerp(blue, 0.5)  ' (0.5, 0, 0.5)
/// ```
///
/// @param a Starting vector (returned when t = 0). Must not be NULL.
/// @param b Ending vector (returned when t = 1). Must not be NULL.
/// @param t Interpolation parameter (typically 0.0 to 1.0).
///
/// @return A new Vec3 representing the interpolated position.
///
/// @note O(1) time complexity.
/// @note Values of t outside [0, 1] will extrapolate beyond a and b.
/// @note Traps with "Vec3.Lerp: null vector" if either vector is NULL.
///
/// @see rt_vec3_add For vector addition
void *rt_vec3_lerp(void *a, void *b, double t)
{
    if (!a || !b)
    {
        rt_trap("Vec3.Lerp: null vector");
        return NULL;
    }
    ViperVec3 *va = (ViperVec3 *)a;
    ViperVec3 *vb = (ViperVec3 *)b;
    // lerp(a, b, t) = a + (b - a) * t = a * (1 - t) + b * t
    double x = va->x + (vb->x - va->x) * t;
    double y = va->y + (vb->y - va->y) * t;
    double z = va->z + (vb->z - va->z) * t;
    return vec3_alloc(x, y, z);
}
