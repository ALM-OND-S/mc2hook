#pragma once

class vehFactory
{
public:
	void* m_Vtable;
	char m_CarName[64];

public:
	vehFactory()  { hook::Thunk<0x575390>::Call<void>(this); }
	~vehFactory() { hook::Thunk<0x5753C0>::Call<void>(this); }
};
