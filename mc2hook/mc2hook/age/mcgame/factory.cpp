#include "factory.h"
#include <age/vehicle/entity.h>
#include <age/vehicle/carsim.h>
#include <age/vehicle/aero.h>
#include <age/vehicle/automgr.h>
#include <age/vehicle/car.h>
#include <age/vehicle/nitro.h>
#include <age/vehicle/carSSTurbo.h>
#include <age/vehicle/aiinfo.h>
#include <age/data/parse.h>
#include <age/physics/archetype.h>
#include <age/vehicle/vehinput.h>
#include <age/memory/memory.h>

#include <age/core/output.h> //

//////////////////// mcPlayerFactory ////////////////////

void mcPlayerFactory::Build(const char* carName, int idx, void* owner)
{
    hook::Thunk<0x46BDC0>::Call<void>(this, carName, idx, owner);
}

vehEntity* mcPlayerFactory::Create()
{
	//int ghost = hook::Thunk<0x46BE10>::Call<int>(this); // This creates a second car already

    MakeEntity();
    MakeSim();
    MakePlayerInput();
    MaybeMakeModel();
    MakeDamage1();
    MakeStuck();
    MakeGyro();
    MakeDriver();
    MakeFeedback();
    MaybeMakeCamera();
    MaybeMakeWheelPtx();

    MakeDamage2();
    MaybeMakeAudio();

    vehEntity* entity = GetEntity();
    vehCarSim* sim = entity->m_Car.m_CarSim;

    if (sim->m_NumWheels == 2) // If bike
    {
        vehAero* aero = sim->m_Aero;
        vehInput* input = entity->m_Car.m_Input;

        aero->sub_4E5450(sim, input);
    }

    vehAutoMgr* mgr = vehAutoMgr::Instance;
    if (mgr) mgr->AddEntry(&entity->m_Car);

    return entity;
}

vehEntity* mcPlayerFactory::GetEntity() const
{
    if (m_Car)
        return (vehEntity*) &m_Car[0xFFFFFFFF]; // m_Car - 0x40
    else
        return nullptr;
}

void mcPlayerFactory::MakePlayerInput()
{
    hook::Thunk<0x46BF20>::Call<void>(this); // Call original
    return;

    // TODO:
    // Rewrite crashes on exit

    vehEntity* entity = GetEntity();

    if (m_PlayerId == -1)
    {
        entity->m_Car.m_Input = nullptr;
        return;
    }

    vehInput* input = new vehInput(0, 1, 0);

    input->Init(entity, m_CarName);

    input->sub_46A3F0(m_PlayerId);
    input->sub_46A410(m_PlayerId);

    entity->m_Car.m_Input = input;
}

//////////////////// vehFactory ////////////////////

void vehFactory::MakeEntity()
{
    hook::Thunk<0x4BF2C0>::Call<void>(this); // Call original
    return;

    // TODO
    vehEntity* entity = new vehEntity();

    if (entity)
        m_Car = &entity->m_Car;
    else
        m_Car = nullptr;
}

void vehFactory::MakeSim()
{
    hook::Thunk<0x4BF300>::Call<void>(this); // Call original
    return;

    // TODO
    vehEntity* entity = GetEntity();

    vehCarSim* sim = new vehCarSim();
    entity->m_Car.m_CarSim = sim;

    sim->MakeCollider(m_CarName, entity);
    sim->MakeAero(m_CarName);
    sim->MakeFluid(m_CarName);
    sim->MakeTransmission(m_CarName);
    sim->MakeEngine(m_CarName);
    sim->MakeWheels (m_CarName);
    sim->MakeDrivetrains(m_CarName);
    sim->MakeAxles (m_CarName);
    sim->MakeSuspensions(m_CarName);

    sim->m_Nitro = new vehNitro();
    sim->m_Nitro->Init(-1, entity, m_CarName);

    sim->m_SSTurbo = new mcCarSSTurbo();
    sim->m_SSTurbo->Init(-1, entity, m_CarName);

    sim->m_AIInfo = new carAIInfo();

    sim->sub_569A80(m_CarName); // Some Load
    sim->sub_575060(&datParser::dword_8600B0); // ?

    sim->field_B4 = 0x15; //
    sim->sub_4D2F60();

    phArchetype* archetype = entity->m_PhysInst.m_Archetype;
    if (archetype)
    {
        archetype->SetTypeFlag(64, 1);
        archetype->SetTypeFlag(1024, 1);
    }

    entity->m_PhysInst.dword_08 |= ((m_Idx * 0x10) + 0x10) << 16 | 8;              //
    entity->m_Car.dword_3c = (entity->m_Car.dword_3c & 0xFFFF00FF) | (m_Idx << 8); //
}

void vehFactory::Build(const char* carName, int idx, void* owner)
{
    hook::Thunk<0x4BF290>::Call<void>(this, carName, idx, owner);
}

vehEntity* vehFactory::Create()
{
    vehAutoMgr* vehMgr = vehAutoMgr::GetInstance();

    MakeEntity();
    MakeSim();
    MakeAIInput();
    MakeModel();
    MakeDamage(); // ?
    MakeAudio();

    vehEntity* entity = GetEntity();

    hook::Thunk<0x4BF850>::Call<void>(this);
    MakeGyro();
    MakeDriver();
    hook::Thunk<0x4BF7F0>::Call<void>(this); // Some MakeDamage

    if (entity) vehMgr->AddEntry(m_Car);
    else vehMgr->AddEntry(nullptr);

    return entity;
}

vehEntity* vehFactory::GetEntity() const
{
    if (m_Car)
        return (vehEntity*)&m_Car[0xFFFFFFFF]; // m_Car - 0x40
    else
        return nullptr;
}
