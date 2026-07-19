#include "GravityHandler.h"

static float gravity;

void GravityHandler::Install()
{
    bool speedrunMode = HookConfig::GetBool("General", "SpeedrunMode", false);
    gravity = HookConfig::GetFloat("Physics", "Gravity", -9.8f);
    if (!speedrunMode) mem::write(0x6449BC, gravity);
}
