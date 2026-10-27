#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>

// Views of the game structures we touch. Only the fields actually used, padded out to the
// byte offsets they sit at in Fallout4.exe 1.11.240. The static_asserts pin every offset -
// get one wrong and it fails the build instead of corrupting memory at runtime.

struct NiPoint3
{
	float x, y, z;
};

inline NiPoint3 operator+(const NiPoint3& a, const NiPoint3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline NiPoint3 operator-(const NiPoint3& a, const NiPoint3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline NiPoint3 operator*(const NiPoint3& a, float s)           { return { a.x * s, a.y * s, a.z * s }; }
inline float    Length(const NiPoint3& a)                        { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline NiPoint3 Normalized(const NiPoint3& a)
{
	float l = Length(a);
	return l > 1e-5f ? a * (1.0f / l) : NiPoint3{ 0.0f, 0.0f, 0.0f };
}

// NiTypes.h: 3x4 matrix, data[row][col]. VERIFIED IN-GAME (heading check in the log): FO4
// stores a node's local axes as the matrix ROWS - the column reading gave a mirrored
// heading (-99.5 vs player 99.6), the row reading matched exactly. All math in Bodycam.cpp
// works in "column form" (axes as columns, M * v), so convert with ToColumnForm() when
// reading a game transform and ToGameForm() when writing it back. Axes: X right, Y forward, Z up.
struct NiMatrix43
{
	float data[3][4];

	NiPoint3 Column(int c) const { return { data[0][c], data[1][c], data[2][c] }; }
	NiPoint3 Right()   const { return Column(0); }
	NiPoint3 Forward() const { return Column(1); }
	NiPoint3 Up()      const { return Column(2); }

	NiPoint3 Mul(const NiPoint3& v) const
	{
		return {
			data[0][0] * v.x + data[0][1] * v.y + data[0][2] * v.z,
			data[1][0] * v.x + data[1][1] * v.y + data[1][2] * v.z,
			data[2][0] * v.x + data[2][1] * v.y + data[2][2] * v.z,
		};
	}
};

// Rotation of `angle` radians about a unit world-space `axis` (Rodrigues).
inline NiMatrix43 AxisAngle(const NiPoint3& axis, float angle)
{
	float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
	float x = axis.x, y = axis.y, z = axis.z;
	NiMatrix43 m{};
	m.data[0][0] = t * x * x + c;     m.data[0][1] = t * x * y - s * z; m.data[0][2] = t * x * z + s * y;
	m.data[1][0] = t * x * y + s * z; m.data[1][1] = t * y * y + c;     m.data[1][2] = t * y * z - s * x;
	m.data[2][0] = t * x * z - s * y; m.data[2][1] = t * y * z + s * x; m.data[2][2] = t * z * z + c;
	return m;
}

inline NiMatrix43 Identity()
{
	NiMatrix43 m{};
	m.data[0][0] = m.data[1][1] = m.data[2][2] = 1.0f;
	return m;
}

inline NiMatrix43 Mul(const NiMatrix43& a, const NiMatrix43& b)
{
	NiMatrix43 r{};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			r.data[i][j] = a.data[i][0] * b.data[0][j] + a.data[i][1] * b.data[1][j] + a.data[i][2] * b.data[2][j];
	return r;
}

// NiTypes.h: NiTransform, size 0x40.
struct NiTransform
{
	NiMatrix43 rot;   // 0x00
	NiPoint3   pos;   // 0x30
	float      scale; // 0x3C
};
static_assert(sizeof(NiTransform) == 0x40, "NiTransform size must match F4SE NiTypes.h");

inline NiMatrix43 Transposed(const NiMatrix43& m)
{
	NiMatrix43 r{};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			r.data[i][j] = m.data[j][i];
	return r;
}

inline NiMatrix43 ToColumnForm(const NiMatrix43& gameRot) { return Transposed(gameRot); }
inline NiMatrix43 ToGameForm(const NiMatrix43& columnRot) { return Transposed(columnRot); }

// NiObjects.h: NiAVObject (m_localTransform @ 0x30, m_worldTransform @ 0x70,
// m_worldBound @ 0xB0, m_previousWorld @ 0xC0, flags @ 0x108).
struct NiAVObjectView
{
	uint8_t      _pad0[0x30];
	NiTransform  localTransform; // 0x30
	NiTransform  worldTransform; // 0x70
	uint8_t      _worldBound[0xC0 - 0xB0];
	NiTransform  previousWorld;  // 0xC0 - last frame's world transform; the renderer differences
	                             // this against worldTransform to build per-object motion vectors,
	                             // which is what TAA / DLSS / frame generation reproject with.
	uint8_t      _pad100[0x108 - 0x100];
	uint64_t     flags;          // 0x108 - bit 0 = app-culled (hidden)
};
static_assert(offsetof(NiAVObjectView, worldTransform) == 0x70, "worldTransform offset must match F4SE NiObjects.h");
static_assert(offsetof(NiAVObjectView, previousWorld) == 0xC0, "previousWorld offset must match F4SE NiObjects.h");
static_assert(offsetof(NiAVObjectView, flags) == 0x108, "flags offset must match F4SE NiObjects.h");

// GameCamera.h: TESCamera base fields (cameraNode @ 0x20, active cameraState @ 0x28) and
// PlayerCamera::cameraStates[] @ 0xE0. cameraState/cameraStates are only used for logging.
struct PlayerCameraView
{
	void*            _vtable;       // 0x00
	uint8_t          _pad08[0x18];
	NiAVObjectView*  cameraNode;    // 0x20
	void*            cameraState;   // 0x28
	uint8_t          _pad30[0xE0 - 0x30];
	void*            cameraStates[13]; // 0xE0
};
static_assert(offsetof(PlayerCameraView, cameraNode) == 0x20, "cameraNode offset must match F4SE GameCamera.h");
static_assert(offsetof(PlayerCameraView, cameraState) == 0x28, "cameraState offset must match F4SE GameCamera.h");
static_assert(offsetof(PlayerCameraView, cameraStates) == 0xE0, "cameraStates offset must match F4SE GameCamera.h");
