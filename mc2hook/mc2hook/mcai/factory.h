#pragma once
#include <mc2hook/mc2hook.h>
#include <veh_base/factory.h>

class vehEntity;
class mcCar;

class aiOpponentFactory : vehFactory
{
public:
	mcCar* m_Car;
	int m_Idx;
	void* m_Owner;
	char buffer[48];

public:
	aiOpponentFactory::aiOpponentFactory(const char* carName, int idx, void* owner); // (const char *, int, aiOpponent *)

	vehEntity* Construct();
	vehEntity* GetEntity() const;

	void MakeEntity();
	void MakeAIInput() { hook::Thunk<0x4BF530>::Call<void>(this); } // 0x4BF530 //
	void MakeSim();
	void MakeModel();
	void MakeDamage();
	void MakeAudio() { hook::Thunk<0x4BF5E0>::Call<void>(this); } // 0x4BF5E0
	void MakeGyro();
	void MakeDriver() { hook::Thunk<0x4BF650>::Call<void>(this); } // 0x4BF650
};
