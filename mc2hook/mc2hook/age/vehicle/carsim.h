#pragma once
#include <age/vehicle/wheel.h>

class vehAero;
class vehTransmission;
class vehEngine;
class vehDrivetrain;
class phCollider;
class vehNitro;
class mcCarSSTurbo;
class vehDamage;
class vehWheel;
class carAIInfo;
class vehEntity;

struct vehWheels
{
	vehWheel m_Wheel_FL;
	vehWheel m_Wheel_RL;
	vehWheel m_Wheel_FR;
	vehWheel m_Wheel_RR;
};

class vehCarSim {
public:
	void* m_Vtable;
	int dword_04;
	int dword_08;
	vehAero* m_Aero;
	void* m_Fluid;
	vehEngine* m_Engine;
	vehTransmission* m_Transmission;
	int m_NumWheels;
	vehWheels* m_WheelsStruct; //vehWheel* m_WheelFront;
	int m_NumDrivetrains;
	vehDrivetrain* m_Drivetrain;
	int m_NumAxles;
	void* m_Axle;
	int m_NumSuspensions;
	void* m_Suspension;
	int dword_3c;
	int dword_40;
	int dword_44;
	Vector3 m_InertiaScale;
	Vector3 m_ModelOffset;
	Vector3 m_CenterOfMass;
	phCollider* m_Collider;
	float m_Steer;
	float m_Throttle;
	float m_Brake;
	float m_Handbrake;
	float field_80;
	float field_84;
	float field_88;
	float field_8C;
	float field_90;
	float field_94;
	float field_98;
	float field_9C;
	float field_A0;
	float field_A4;
	float field_A8;
	float field_AC;
	float m_Speed;
	int field_B4;
	float m_Mass;
	Vector3 m_Size;
	Vector3 m_InertiaBox;
	float m_BoundFriction;
	float m_BoundElasticity;
	float m_BoundGravity;
	float m_AirGravity;
	int m_DrivetrainType;
	int* m_Freetrain;
	vehWheel* m_Wheels[4];
	//vehWheel* m_WheelFL;
	//vehWheel* m_WheelRL;
	//vehWheel* m_WheelFR;
	//vehWheel* m_WheelRR;
	int dword_fc;
	int dword_100;
	int dword_104;
	int dword_108;
	int dword_10c;
	int dword_110;
	int dword_114;
	int dword_118;
	int dword_11c;
	int dword_120;
	int dword_124;
	int dword_128;
	int dword_12c;
	int dword_130;
	int dword_134;
	int dword_138;
	int dword_13c;
	int dword_140;
	int dword_144;
	int dword_148;
	int dword_14c;
	int dword_150;
	float dword_154;
	float m_HillHorsepowerFactor;
	float m_CarFrictionHandlingWipeout;
	float dword_160;
	float m_LandingDampFactor;
	float m_AeroDampFactor;
	float m_MediumLod;
	float m_LowLod;
	float m_VeryLowLod;
	float m_SSSValue;
	int m_SSSThreshold;
	int dword_180;
	char field_184;
	bool m_BurnoutCharged;
	char field_186;
	char field_187;
	float m_BurnoutThresholdSpeed;
	float m_BurnoutValue;
	float m_BurnoutIncreaseSpeed;
	float m_BurnoutDecreaseSpeed;
	float m_BurnoutDamageAmount;
	float m_BurnoutBoostSpeed;
	int m_BurnoutCharging;
	float dword_1a4;
	float m_CenterOfMassY;
	float m_SteeringLimit;
	float m_Airtime;
	float m_SomeBrake;
	int dword_1b8;
	int dword_1bc;
	int dword_1c0;
	int dword_1c4;
	int dword_1c8;
	carAIInfo* m_AIInfo;
	vehNitro* m_Nitro;
	mcCarSSTurbo* m_SSTurbo;
	vehDamage* m_Damage;

public:
	vehCarSim()  { hook::Thunk<0x4D22E0>::Call<void>(this); }
	~vehCarSim() { hook::Thunk<0x4D23F0>::Call<void>(this); }

public:
	void UpdateControls();
	void UpdateControlsComp();

	int OnGround(); // Number of wheels on ground
	int BottomedOut();
	void SetDrivable(int a2);
	void SetCenterOfMass(const Vector3& cg); // Set the simulation center of mass offset from instance origin
	void SetFrictionHandling(float friction);

	void sub_4D2F60(); // ComputeConstants?
	float sub_4D2860(float a2);
	void sub_569A80(const char* carName); // Some Load
	void sub_575060(void* a2);

public:
	void MakeCollider(const char* carName, vehEntity* entity) { hook::Thunk<0x4D2440>::Call<void>(this, carName, entity); }
	void MakeAero(const char* carName)                        { hook::Thunk<0x4D2490>::Call<void>(this, carName); }
	void MakeFluid(const char* carName)                       { hook::Thunk<0x4D24E0>::Call<void>(this, carName); }
	void MakeTransmission(const char* carName)                { hook::Thunk<0x569370>::Call<void>(this, carName); }
	void MakeEngine(const char* carName)                      { hook::Thunk<0x569320>::Call<void>(this, carName); }
	void MakeWheels(const char* carName)                      { hook::Thunk<0x56AAB0>::Call<void>(this, carName); }
	void MakeDrivetrains(const char* carName)                 { hook::Thunk<0x5693C0>::Call<void>(this, carName); }
	void MakeAxles(const char* carName)                       { hook::Thunk<0x5694B0>::Call<void>(this, carName); }
	void MakeSuspensions(const char* carName)                 { hook::Thunk<0x5695E0>::Call<void>(this, carName); }
};

static_assert(sizeof(vehCarSim) == 0x1DC, "vehCarSim size mismatch");
