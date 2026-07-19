#pragma once
#include <mc2hook\mc2hook.h>
#include <age/vehicle/car.h>
#include <age/physics/phinst.h>

class vehEntity
{
public:
	phInst m_PhysInst;
	mcCar m_Car;

public:
	vehEntity()  { hook::Thunk<0x4D1580>::Call<void>(this); }
	~vehEntity() { hook::Thunk<0x4D1DA0>::Call<void>(this); }
};
