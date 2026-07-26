#pragma once

class vehEntity;
class vehInput;
class vehCarSim;

class mcGhostCar
{
public:
    vehEntity* m_Entity = nullptr;

    bool Spawn(const char* carName);
    void Destroy(bool a2);

    //void Update();

    //vehInput* GetInput();
    //vehCarSim* GetSim();
};
