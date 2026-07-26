#include "entity.h"

void vehEntity::Delete(bool a2)
{
	hook::Thunk<0x4D1E40>::Call<void>(this, a2); // Call original
}
