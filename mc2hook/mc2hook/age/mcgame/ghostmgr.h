#pragma once
#include <age/mcgame/ghost.h>

class mcGhostManager
{
public:
    static mcGhostManager Instance;

    mcGhostCar m_Ghost;

    void SpawnGhost(const char* carName);
    void DestroyGhost();

    void Update();
};
