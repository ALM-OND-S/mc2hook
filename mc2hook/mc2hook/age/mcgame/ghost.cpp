#include "ghost.h"
#include <age/mcgame/factory.h>
#include <age/vehicle/entity.h>

bool mcGhostCar::Spawn(const char* carName)
{
    // TODO: Check if it's possible to have a mcGhostFactory,
    // creating only what's needed,
    // and possibly having a custom Update in vehAutoMgr/vehManager.

    if (m_Entity)
        return false;

    vehFactory factory;

    factory.Build(carName, 1, nullptr); // Setting idx to higher than 0 enables collisions again (when other AIs are present)

    m_Entity = factory.Create();

    return m_Entity != nullptr;
}

void mcGhostCar::Destroy(bool a2)
{
    if (!m_Entity) return;

    m_Entity->Delete(true);
    m_Entity = nullptr;
}
