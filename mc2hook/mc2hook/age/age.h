#pragma once
#include <mc2hook\mc2hook.h>

void ageEndFrame();

class Timer
{
public:
    DWORD TickCount;

public:
    static hook::Type<uint32_t> s_HostTimer;
    static hook::Type<float> s_HostTime; // gfxHostTime, gfx.h?
    static hook::Type<float> s_TickToMilliseconds;
    static hook::Type<int> g_FrameTimeMode;
    static hook::Type<float> s_FrameTime;
    static hook::Type<float> s_CPUUpdateTime;
    static hook::Type<float> flt_858344;
    static hook::Type<float> flt_85833C;
    static hook::Type<uint32_t> s_LastFrameTick;

    Timer() { hook::Thunk<0x611C60>::Call<void>(this); }

    static uint32_t QuickTicks();
    static uint32_t Ticks() { return hook::StaticThunk<0x611C40>::Call<uint32_t>(); }
};
