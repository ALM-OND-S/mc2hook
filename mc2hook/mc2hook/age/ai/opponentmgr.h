#pragma once
#include <mc2hook\mc2hook.h>

class aiOpponent;

class aiOpponentManager
{
public:
    int m_NumOpponents;
    int m_NumCompetitors;
    aiOpponent* m_Opponents;
    int m_CurrentOpponentIdx;

public:
    bool Init(bool loadResources);

public:
    static hook::Type<aiOpponentManager*> Instance;
    static hook::Type<bool> byte_6C342B;
};
