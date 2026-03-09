#ifndef SAVE_H
#define SAVE_H

#include <cstdint>

struct PlayerData
{
    uint32_t level;
    uint32_t xp;
    uint32_t xpTotal;
    uint32_t songsPlayed;
    uint32_t songsClear;
    uint32_t perfectCount;
    uint32_t excellentCount;
    uint32_t greatCount;
    uint32_t standardCount;
    uint32_t dropoutCount;
};

void saveInit();
void saveToDisk();
PlayerData &getPlayerData();
uint32_t getXpForLevel(uint32_t level);
uint32_t addXp(uint32_t amount);

#endif