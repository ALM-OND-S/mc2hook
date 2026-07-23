#include "archetype.h"

float phArchetype::sub_47B9D0()
{
	return this->dword_3C; // Gravity factor?
}

void phArchetype::SetTypeFlag(int type, char flag)
{
	hook::Thunk<0x58E800>::Call<void>(this, type, flag); // Call original
}
