#include <cstdio>
#include <cstring>
#include <nds.h>
#include <cmath>

#include "save.h"

static PlayerData player = {};
static const char *savePath = "/project-ds/save.dat";
static const uint32_t saveVersion = 1;

void saveInit()
{
    // Try to load existing save
    FILE *f = fopen(savePath, "rb");
    if (f)
    {
        uint32_t version = 0;
        fread(&version, sizeof(uint32_t), 1, f);

        if (version == saveVersion)
        {
            fread(&player, sizeof(PlayerData), 1, f);
            printf("Save loaded! Lv.%lu\n", player.level);
        }
        else
        {
            printf("Save version mismatch, resetting\n");
            memset(&player, 0, sizeof(PlayerData));
            player.level = 1;
        }
        fclose(f);
    }
    else
    {
        // First time: initialize fresh
        memset(&player, 0, sizeof(PlayerData));
        player.level = 1;
        printf("New save created\n");
    }
}

void saveToDisk()
{
    FILE *f = fopen(savePath, "wb");
    if (f)
    {
        fwrite(&saveVersion, sizeof(uint32_t), 1, f);
        fwrite(&player, sizeof(PlayerData), 1, f);
        fclose(f);
    }
}

PlayerData &getPlayerData()
{
    return player;
}

uint32_t getXpForLevel(uint32_t level)
{
    return 230 + level * 10 + (uint32_t)(sqrt((double)level) * 10);
}

uint32_t addXp(uint32_t amount)
{
    uint32_t levelsGained = 0;

    player.xp += amount;
    player.xpTotal += amount;

    // Check for level ups
    while (player.xp >= getXpForLevel(player.level))
    {
        player.xp -= getXpForLevel(player.level);
        player.level++;
        levelsGained++;
    }

    // Auto-save after gaining XP
    saveToDisk();

    return levelsGained;
}
