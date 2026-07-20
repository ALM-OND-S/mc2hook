#include "TimeWarpHandler.h"

void TimeWarpHandler::Install()
{
    bool speedrunMode = HookConfig::GetBool("General", "SpeedrunMode", false);
    float timeWarp = HookConfig::GetFloat("General", "TimeWarp", 1.0f);

    if (!speedrunMode)
    {
        mem::write(0x613FA9 + 6, static_cast<float>(timeWarp));
        mem::write(0x4CFBD3 + 6, static_cast<float>(timeWarp));
    }
}
