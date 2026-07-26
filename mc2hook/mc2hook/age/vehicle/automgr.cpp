#include "automgr.h"

declfield(vehAutoMgr::Instance)(0x6C523C);

vehAutoMgr* vehAutoMgr::GetInstance()
{
    return vehAutoMgr::Instance.get();
}

void vehAutoMgr::AddEntry(mcCar* car) // vehManager
{
    hook::Thunk<0x4CE990>::Call<void>(this, car);
}

void vehAutoMgr::DelEntry(mcCar* car) // vehManager
{
    hook::Thunk<0x4CEA00>::Call<void>(this, car);
}
