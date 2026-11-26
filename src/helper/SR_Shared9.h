// Shared between the HP2010 Single Race helper core (NFS11_SingleRace.cpp) and its Race Settings menu (SR_Overlay9.cpp).
// The settings live in the ini (the core re-reads it at every launch), so the menu reads and writes the ini through
// SR_GetConditions / SR_SetConditions.
#pragma once
#include <windows.h>
#include <stdint.h>

namespace sr {
enum EventMode { kModeRace, kModeHotPursuit, kModeCopPursuit, kEventModeCount };
enum TimeOfDay { kDawn, kMorning, kNoon, kAfternoon, kSunset, kNight, kTimeCount };
enum Weather { kDry, kWet, kRain, kStorm, kWeatherCount };
enum WeaponsMode { kWeaponsClassic, kWeaponsCustom, kWeaponsModeCount };
enum Difficulty { kEasy, kMedium, kHard, kDifficultyCount };
enum Weapon { kRacerSpikeStrip, kRacerEMP, kJammer, kTurbo, kCopSpikeStrip, kCopEMP, kRoadBlock, kHelicopter, kWeaponCount };
const int kMaxCharges = 5;
const int kWeaponLevelMin = 1, kWeaponLevelMax = 3;
const int kMaxAI = 7, kRaceRacersMin = 1, kRaceRacersMax = 7, kHpMin = 1, kHpRacersMax = 6, kHpCopsMax = 6;
const int kCopRacersMin = 1, kCopRacersMax = 7;

struct Conditions {
    int eventMode, time, weather, timeProgression, difficulty;
    int raceRacers, hpRacers, hpCops, copRacers;
    int weaponsMode, charges[kWeaponCount], weaponLevel;
    int menuKey;   // ini [Overlay] Key (default F5)
    int fontSize;  // ini [Overlay] FontSize, 0 = automatic
};
}  // namespace sr

sr::Conditions SR_GetConditions();
void SR_SetConditions(const sr::Conditions& c);  // writes the ini
bool SR_PickerVisible();                          // the Single Race event picker is on screen
void SR_Log(const char* fmt, ...);
void SR_RequestMenu();  // the picker's footer prompt input (sr_info.lua); opened on the next frame
void Overlay_Start();
