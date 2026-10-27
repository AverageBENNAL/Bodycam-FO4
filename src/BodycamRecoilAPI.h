#pragma once
#include <cstdint>

struct BodycamRecoilPoseV1
{
	float aimPitchUpDeg, aimYawDeg;
	float gunPitchUpDeg, gunYawDeg, gunRollDeg, gunBack;
	float cameraPitchUpDeg, cameraYawDeg, cameraRollDeg;
};

struct BodycamRecoilHostV1
{
	uint32_t size;
	uint32_t version;
	void (*OnShot)(uint32_t weaponFormID, int weaponType, bool aiming);
	void (*Update)(float dt, bool aiming, bool active, BodycamRecoilPoseV1* pose);
};

constexpr uint32_t kBodycamRecoilAPIVersion = 1;
