#pragma once
#include <mc2hook\mc2hook.h>

constexpr int NUM_NEW_VEHICLE_SLOTS = 256;
extern char* VEHICLE_BASENAMES_DYN[NUM_NEW_VEHICLE_SLOTS];

class CustomVehicleHandler
{
public:
    bool StringLoadHook(LPCSTR filename, int language, int flagsMask, bool readIdentifier);
    void asyncio_LoadVehicleResources();
    void asyncio_DestructHook();
    void carststs_AddVehHook(LPCSTR basename, int index);
    void* carstats_GetEquipmentIcon();
    void carstats_DrawHook();
    void vehicle3d_AddHook(int unk, LPCSTR name, int displayTypeIdx, int index);
    void vehicle3d_ConstructHook(char** a2, int a3);
    void* pausemenu_ConstructHook();
    int netmanager_GetVarHook();

    static void Install();
};