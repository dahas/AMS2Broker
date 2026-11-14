#include "pch.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json.h"
#include "utils.h"

#pragma comment(lib, "ws2_32.lib")

const int SERVER_PORT = 58888;

const bool USE_CAR_HOOK = false;
const uintptr_t CAR_HOOK_RVA = 0x3FD3D1;

const uintptr_t CLASS_ROOT_ADDR = 0x1818C05C0;
const uintptr_t CLASS_OFF1 = 0x130;
const uintptr_t CLASS_OFF2 = 0xA8;
const int CLASS_STRIDE = 0x930;
const int CLASS_FLAG_OFFSET = 0x52C;
const int MAX_CLASSES = 89;

const size_t MAX_REQUEST_SIZE = 64 * 1024;

static std::mutex g_hookMutex;
static uintptr_t g_forceCarAddr = 0;

struct IntSetting {
    const std::vector<std::string>* path;
    uintptr_t absoluteAddress;
};

const uintptr_t VEH_CAR_ADDRESS = 0x1818C41F4;
const uintptr_t VEH_LIVERY_ADDRESS = 0x1818C4764;

const uintptr_t TRACK_ADDRESS = 0x1818C1674;

const uintptr_t P_ENABLED_ADDRESS = 0x1817BD934;
const uintptr_t P_MINUTES_ADDRESS = 0x1817BE414;
const uintptr_t P_STARTTIME_ADDRESS = 0x1817C2AC4;
const uintptr_t P_TIME_PROG_ADDRESS = 0x1817C0A24;
const uintptr_t P_WEATHER_SLOTS_ADDRESS = 0x1817C0DC4;
const uintptr_t P_WEATHER_SLOT1_ADDRESS = 0x1817C0F94;
const uintptr_t P_WEATHER_SLOT2_ADDRESS = 0x1817C1504;
const uintptr_t P_WEATHER_SLOT3_ADDRESS = 0x1817C1A74;
const uintptr_t P_WEATHER_SLOT4_ADDRESS = 0x1817C1FE4;
const uintptr_t P_WEATHER_PROG_ADDRESS = 0x1817C0BF4;
const uintptr_t P_LIVETRACK_ADDRESS = 0x1817C35A4;

const uintptr_t Q_ENABLED_ADDRESS = 0x1817CA454;
const uintptr_t Q_MINUTES_ADDRESS = 0x1817CAF34;
const uintptr_t Q_PRIVAT_SESSION_ADDRESS = 0x1817CC4F4;
const uintptr_t Q_STINT_LIMIT_ADDRESS = 0x1817CBA14;
const uintptr_t Q_LAPS_LIMIT_ADDRESS = 0x1817CBBE4;
const uintptr_t Q_STARTTIME_ADDRESS = 0x1817CF5E4;
const uintptr_t Q_TIME_PROG_ADDRESS = 0x1817CD544;
const uintptr_t Q_WEATHER_SLOTS_ADDRESS = 0x1817CD8E4;
const uintptr_t Q_WEATHER_SLOT1_ADDRESS = 0x1817CDAB4;
const uintptr_t Q_WEATHER_SLOT2_ADDRESS = 0x1817CE024;
const uintptr_t Q_WEATHER_SLOT3_ADDRESS = 0x1817CE594;
const uintptr_t Q_WEATHER_SLOT4_ADDRESS = 0x1817CEB04;
const uintptr_t Q_WEATHER_PROG_ADDRESS = 0x1817CD714;
const uintptr_t Q_LIVETRACK_ADDRESS = 0x1817D00C4;

const uintptr_t R_DURATION_TYPE_ADDRESS = 0x1818AE1D4;
const uintptr_t R_MINUTES_ADDRESS = 0x1818AE574;
const uintptr_t R_EXTRA_LAP_ADDRESS = 0x1818AE744;
const uintptr_t R_LAPS_ADDRESS = 0x1818AE3A4;
const uintptr_t R_DATE_TYPE_ADDRESS = 0x1818B26B4;
const uintptr_t R_CUSTOM_DAY_ADDRESS = 0x1818B2FC4;
const uintptr_t R_CUSTOM_MONTH_ADDRESS = 0x1818B3194;
const uintptr_t R_CUSTOM_YEAR_ADDRESS = 0x1818B3364;
const uintptr_t R_STARTTIME_ADDRESS = 0x1818B2C24;
const uintptr_t R_TIME_PROG_ADDRESS = 0x1818B0B84;
const uintptr_t R_WEATHER_SLOTS_ADDRESS = 0x1818B0F24;
const uintptr_t R_WEATHER_SLOT1_ADDRESS = 0x1818B10F4;
const uintptr_t R_WEATHER_SLOT2_ADDRESS = 0x1818B1664;
const uintptr_t R_WEATHER_SLOT3_ADDRESS = 0x1818B1BD4;
const uintptr_t R_WEATHER_SLOT4_ADDRESS = 0x1818B2144;
const uintptr_t R_WEATHER_PROG_ADDRESS = 0x1818B0D54;
const uintptr_t R_START_TYPE_ADDRESS = 0x1818AFD04;
const uintptr_t R_FORMATION_LAP_ADDRESS = 0x1818B07E4;
const uintptr_t R_ALLOW_REFUEL_ADDRESS = 0x1818B0614;
const uintptr_t R_MANDATORY_STOP_ADDRESS = 0x1818AFED4;
const uintptr_t R_MANDATORY_TYRES_ADDRESS = 0x1818B00A4;
const uintptr_t R_SCHEDULED_FCY_ADDRESS = 0x1818B0444;
const uintptr_t R_LIVETRACK_ADDRESS = 0x1818B3704;

const uintptr_t OPP_FIELD_TYPE_ADDRESS = 0x1817BB884;
const uintptr_t OPP_NUMBER_TYPE_ADDRESS = 0x1817BB144;
const uintptr_t OPP_CUSTOM_NUMBER_ADDRESS = 0x1817BAF74;
const uintptr_t OPP_AI_SKILL_ADDRESS = 0x1817BBDF4;
const uintptr_t OPP_AI_RAIN_ADDRESS = 0x1817BC704;
const uintptr_t OPP_AI_TRACTION_ADDRESS = 0x1817BC364;
const uintptr_t OPP_AI_BREAKING_ADDRESS = 0x1817BC534;
const uintptr_t OPP_AI_AGGRESSION_ADDRESS = 0x1817BBFC4;
const uintptr_t OPP_AI_ERRORS_ADDRESS = 0x1817BCAA4;
const uintptr_t OPP_PLR_GRID_POS_ADDRESS = 0x1817BD014;
const uintptr_t OPP_PLR_CUSTOM_POS_ADDRESS = 0x1817BD3B4;

const uintptr_t RUL_ENABLED_ADDRESS = 0x1818BE154;
const uintptr_t RUL_TRACK_LIMITS_ADDRESS = 0x1818BD674;
const uintptr_t RUL_DRIVE_THRU_ADDRESS = 0x1818BE324;
const uintptr_t RUL_PIT_EXIT_ADDRESS = 0x1818BEA64;
const uintptr_t RUL_FULL_YELLOW_ADDRESS = 0x1818BFE54;
const uintptr_t RUL_SPEED_LIMIT_ADDRESS = 0x1818BDF84;
const uintptr_t RUL_LIMITED_SETUP_ADDRESS = 0x1818C01F4;

const uintptr_t CLASS_ROOT_ADDRESS = 0x1818C05C0;

namespace P {
    static const std::vector<std::string> Car =                     { "Vehicle", "Car" };
    static const std::vector<std::string> Livery =                  { "Vehicle", "Livery" };

    static const std::vector<std::string> Track =                   { "Track" };

    static const std::vector<std::string> PracticeEnabled =         { "Practice", "Enabled" };
    static const std::vector<std::string> PracticeMinutes =         { "Practice", "Minutes" };
    static const std::vector<std::string> PracticeStartTime =       { "Practice", "StartTime" };
    static const std::vector<std::string> PracticeTimeProg =        { "Practice", "TimeProgression" };
    static const std::vector<std::string> PracticeWeatherSlots =    { "Practice", "WeatherSlots" };
    static const std::vector<std::string> PracticeWeatherSlot1 =    { "Practice", "WeatherSlot1" };
    static const std::vector<std::string> PracticeWeatherSlot2 =    { "Practice", "WeatherSlot2" };
    static const std::vector<std::string> PracticeWeatherSlot3 =    { "Practice", "WeatherSlot3" };
    static const std::vector<std::string> PracticeWeatherSlot4 =    { "Practice", "WeatherSlot4" };
    static const std::vector<std::string> PracticeWeatherProg =     { "Practice", "WeatherProgression" };
    static const std::vector<std::string> PracticeLivetrack =       { "Practice", "Livetrack" };

    static const std::vector<std::string> QualiEnabled =            { "Qualifying", "Enabled" };
    static const std::vector<std::string> QualiMinutes =            { "Qualifying", "Minutes" };
    static const std::vector<std::string> QualiPrivateSession =     { "Qualifying", "PrivateSession" };
    static const std::vector<std::string> QualiStintLimit =         { "Qualifying", "StintLimit" };
    static const std::vector<std::string> QualiLapsLimit =          { "Qualifying", "LapsLimit" };
    static const std::vector<std::string> QualiStartTime =          { "Qualifying", "StartTime" };
    static const std::vector<std::string> QualiTimeProg =           { "Qualifying", "TimeProgression" };
    static const std::vector<std::string> QualiWeatherSlots =       { "Qualifying", "WeatherSlots" };
    static const std::vector<std::string> QualiWeatherSlot1 =       { "Qualifying", "WeatherSlot1" };
    static const std::vector<std::string> QualiWeatherSlot2 =       { "Qualifying", "WeatherSlot2" };
    static const std::vector<std::string> QualiWeatherSlot3 =       { "Qualifying", "WeatherSlot3" };
    static const std::vector<std::string> QualiWeatherSlot4 =       { "Qualifying", "WeatherSlot4" };
    static const std::vector<std::string> QualiWeatherProg =        { "Qualifying", "WeatherProgression" };
    static const std::vector<std::string> QualiLivetrack =          { "Qualifying", "Livetrack" };

    static const std::vector<std::string> RaceDurType =             { "Race", "DurationType" };
    static const std::vector<std::string> RaceMinutes =             { "Race", "Minutes" };
    static const std::vector<std::string> RaceExtraLap =            { "Race", "+1Lap" };
    static const std::vector<std::string> RaceLaps =                { "Race", "Laps" };
    static const std::vector<std::string> RaceDateType =            { "Race", "DateType" };
    static const std::vector<std::string> RaceCustomDay =           { "Race", "CustomDay" };
    static const std::vector<std::string> RaceCustomMonth =         { "Race", "CustomMonth" };
    static const std::vector<std::string> RaceCustomYear =          { "Race", "CustomYear" };
    static const std::vector<std::string> RaceStartTime =           { "Race", "StartTime" };
    static const std::vector<std::string> RaceTimeProg =            { "Race", "TimeProgression" };
    static const std::vector<std::string> RaceWeatherSlots =        { "Race", "WeatherSlots" };
    static const std::vector<std::string> RaceWeatherSlot1 =        { "Race", "WeatherSlot1" };
    static const std::vector<std::string> RaceWeatherSlot2 =        { "Race", "WeatherSlot2" };
    static const std::vector<std::string> RaceWeatherSlot3 =        { "Race", "WeatherSlot3" };
    static const std::vector<std::string> RaceWeatherSlot4 =        { "Race", "WeatherSlot4" };
    static const std::vector<std::string> RaceWeatherProg =         { "Race", "WeatherProgression" };
    static const std::vector<std::string> RaceStartType =           { "Race", "StartType" };
    static const std::vector<std::string> RaceFormationLap =        { "Race", "FormationLap" };
    static const std::vector<std::string> RaceAllowRefueling =      { "Race", "AllowRefueling" };
    static const std::vector<std::string> RaceMandatoryPitStop =    { "Race", "MandatoryPitStop" };
    static const std::vector<std::string> RaceMandatoryTyres =      { "Race", "MandatoryTyres" };
    static const std::vector<std::string> RaceScheduledFCY =        { "Race", "ScheduledFCY" };
    static const std::vector<std::string> RaceLivetrack =           { "Race", "Livetrack" };

    static const std::vector<std::string> OppFieldType =            { "Opponents", "FieldType" };
    static const std::vector<std::string> OppNumberType =           { "Opponents", "NumberType" };
    static const std::vector<std::string> OppCustomNumber =         { "Opponents", "CustomNumber" };
    static const std::vector<std::string> OppAiSkill =              { "Opponents", "AiSkill" };
    static const std::vector<std::string> OppAiRainAbility =        { "Opponents", "AiRainAbility" };
    static const std::vector<std::string> OppAiTraction =           { "Opponents", "AiTraction" };
    static const std::vector<std::string> OppAiBreaking =           { "Opponents", "AiBreaking" };
    static const std::vector<std::string> OppAiAggressiveness =     { "Opponents", "AiAggressiveness" };
    static const std::vector<std::string> OppAiErrorMultiplier =    { "Opponents", "AiErrorMultiplier" };
    static const std::vector<std::string> OppPlayerGridPosition =   { "Opponents", "PlayerGridPosition" };
    static const std::vector<std::string> OppCustomGridPosition =   { "Opponents", "CustomGridPosition" };

    static const std::vector<std::string> RulEnabled =              { "Rules&Penalties", "Enabled" };
    static const std::vector<std::string> RulWarningTrackLimits =   { "Rules&Penalties", "WarningTrackLimits" };
    static const std::vector<std::string> RulDriveThroughPenaltiy = { "Rules&Penalties", "DriveThroughPenaltiy" };
    static const std::vector<std::string> RulPitExitPenaltiy =      { "Rules&Penalties", "PitExitPenaltiy" };
    static const std::vector<std::string> RulFullCourseYellow =     { "Rules&Penalties", "FullCourseYellow" };
    static const std::vector<std::string> RulPitSpeedLimit =        { "Rules&Penalties", "PitSpeedLimit" };
    static const std::vector<std::string> RulLimitedSetup =         { "Rules&Penalties", "LimitedSetup" };
}

static const IntSetting SETTINGS[] = {
    { &P::Car,                  VEH_CAR_ADDRESS },
    { &P::Livery,               VEH_LIVERY_ADDRESS },

    { &P::Track,                TRACK_ADDRESS },

    { &P::PracticeEnabled,      P_ENABLED_ADDRESS },
    { &P::PracticeMinutes,      P_MINUTES_ADDRESS },
    { &P::PracticeStartTime,    P_STARTTIME_ADDRESS },
    { &P::PracticeTimeProg,     P_TIME_PROG_ADDRESS },
    { &P::PracticeWeatherSlots, P_WEATHER_SLOTS_ADDRESS },
    { &P::PracticeWeatherSlot1, P_WEATHER_SLOT1_ADDRESS },
    { &P::PracticeWeatherSlot2, P_WEATHER_SLOT2_ADDRESS },
    { &P::PracticeWeatherSlot3, P_WEATHER_SLOT3_ADDRESS },
    { &P::PracticeWeatherSlot4, P_WEATHER_SLOT4_ADDRESS },
    { &P::PracticeWeatherProg,  P_WEATHER_PROG_ADDRESS },
    { &P::PracticeLivetrack,    P_LIVETRACK_ADDRESS },

    { &P::QualiEnabled,         Q_ENABLED_ADDRESS },
    { &P::QualiMinutes,         Q_MINUTES_ADDRESS },
    { &P::QualiPrivateSession,  Q_PRIVAT_SESSION_ADDRESS },
    { &P::QualiStintLimit,      Q_STINT_LIMIT_ADDRESS },
    { &P::QualiLapsLimit,       Q_LAPS_LIMIT_ADDRESS },
    { &P::QualiStartTime,       Q_STARTTIME_ADDRESS },
    { &P::QualiTimeProg,        Q_TIME_PROG_ADDRESS },
    { &P::QualiWeatherSlots,    Q_WEATHER_SLOTS_ADDRESS },
    { &P::QualiWeatherSlot1,    Q_WEATHER_SLOT1_ADDRESS },
    { &P::QualiWeatherSlot2,    Q_WEATHER_SLOT2_ADDRESS },
    { &P::QualiWeatherSlot3,    Q_WEATHER_SLOT3_ADDRESS },
    { &P::QualiWeatherSlot4,    Q_WEATHER_SLOT4_ADDRESS },
    { &P::QualiWeatherProg,     Q_WEATHER_PROG_ADDRESS },
    { &P::QualiLivetrack,       Q_LIVETRACK_ADDRESS },

    { &P::RaceDurType,          R_DURATION_TYPE_ADDRESS },
    { &P::RaceMinutes,          R_MINUTES_ADDRESS },
    { &P::RaceExtraLap,         R_EXTRA_LAP_ADDRESS },
    { &P::RaceLaps,             R_LAPS_ADDRESS },
    { &P::RaceDateType,         R_DATE_TYPE_ADDRESS },
    { &P::RaceCustomDay,        R_CUSTOM_DAY_ADDRESS },
    { &P::RaceCustomMonth,      R_CUSTOM_MONTH_ADDRESS },
    { &P::RaceCustomYear,       R_CUSTOM_YEAR_ADDRESS },
    { &P::RaceStartTime,        R_STARTTIME_ADDRESS },
    { &P::RaceTimeProg,         R_TIME_PROG_ADDRESS },
    { &P::RaceWeatherSlots,     R_WEATHER_SLOTS_ADDRESS },
    { &P::RaceWeatherSlot1,     R_WEATHER_SLOT1_ADDRESS },
    { &P::RaceWeatherSlot2,     R_WEATHER_SLOT2_ADDRESS },
    { &P::RaceWeatherSlot3,     R_WEATHER_SLOT3_ADDRESS },
    { &P::RaceWeatherSlot4,     R_WEATHER_SLOT4_ADDRESS },
    { &P::RaceWeatherProg,      R_WEATHER_PROG_ADDRESS },
    { &P::RaceStartType,        R_START_TYPE_ADDRESS },
    { &P::RaceFormationLap,     R_FORMATION_LAP_ADDRESS },
    { &P::RaceAllowRefueling,   R_ALLOW_REFUEL_ADDRESS },
    { &P::RaceMandatoryPitStop, R_MANDATORY_STOP_ADDRESS },
    { &P::RaceMandatoryTyres,   R_MANDATORY_TYRES_ADDRESS },
    { &P::RaceScheduledFCY,     R_SCHEDULED_FCY_ADDRESS },
    { &P::RaceLivetrack,        R_LIVETRACK_ADDRESS },

    { &P::OppFieldType,             OPP_FIELD_TYPE_ADDRESS },
    { &P::OppNumberType,            OPP_NUMBER_TYPE_ADDRESS },
    { &P::OppCustomNumber,          OPP_CUSTOM_NUMBER_ADDRESS },
    { &P::OppAiSkill,               OPP_AI_SKILL_ADDRESS },
    { &P::OppAiRainAbility,         OPP_AI_RAIN_ADDRESS },
    { &P::OppAiTraction,            OPP_AI_TRACTION_ADDRESS },
    { &P::OppAiBreaking,            OPP_AI_BREAKING_ADDRESS },
    { &P::OppAiAggressiveness,      OPP_AI_AGGRESSION_ADDRESS },
    { &P::OppAiErrorMultiplier,     OPP_AI_ERRORS_ADDRESS },
    { &P::OppPlayerGridPosition,    OPP_PLR_GRID_POS_ADDRESS },
    { &P::OppCustomGridPosition,    OPP_PLR_CUSTOM_POS_ADDRESS },

    { &P::RulEnabled,               RUL_ENABLED_ADDRESS },
    { &P::RulWarningTrackLimits,    RUL_TRACK_LIMITS_ADDRESS },
    { &P::RulDriveThroughPenaltiy,  RUL_DRIVE_THRU_ADDRESS },
    { &P::RulPitExitPenaltiy,       RUL_PIT_EXIT_ADDRESS },
    { &P::RulFullCourseYellow,      RUL_FULL_YELLOW_ADDRESS },
    { &P::RulPitSpeedLimit,         RUL_SPEED_LIMIT_ADDRESS },
    { &P::RulLimitedSetup,          RUL_LIMITED_SETUP_ADDRESS },
};

bool InstallCarHook() {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    if (g_forceCarAddr) return true;

    uintptr_t base = ModuleBase();
    uintptr_t site = base + CAR_HOOK_RVA;

    static const uint8_t orig[7] = { 0x41, 0x8B, 0xD4, 0x48, 0x89, 0x45, 0x40 };
    uint8_t cur[7] = {};
    if (!SafeRead(site, cur, sizeof(cur)) || memcmp(cur, orig, sizeof(orig)) != 0) {
        Log("[CAR] Originalbytes an 0x%llX stimmen nicht, Hook nicht installiert.\n", (unsigned long long)site);
        return false;
    }

    uintptr_t lo = base > 0x70000000ULL ? base - 0x70000000ULL : 0x10000;
    uint8_t* cave = nullptr;
    for (uintptr_t a = base - 0x10000; a > lo; a -= 0x10000) {
        cave = (uint8_t*)VirtualAlloc((void*)a, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (cave) break;
    }
    if (!cave) {
        Log("[CAR] Kein Speicher in Reichweite des Moduls.\n");
        return false;
    }
    memset(cave, 0xCC, 0x1000);

    uint8_t* p = cave;
    *p++ = 0x83; *p++ = 0x3D;
    *(int32_t*)p = 0x100 - 7; p += 4;
    *p++ = 0x00;
    *p++ = 0x74; *p++ = 0x07;
    *p++ = 0x44; *p++ = 0x8B; *p++ = 0x25;
    *(int32_t*)p = 0x100 - 16; p += 4;
    *p++ = 0x44; *p++ = 0x89; *p++ = 0xE2;
    *p++ = 0x48; *p++ = 0x89; *p++ = 0x45; *p++ = 0x40;
    *p++ = 0xE9;
    *(int32_t*)p = (int32_t)((intptr_t)(site + 7) - (intptr_t)(cave + 28)); p += 4;
    *(int32_t*)(cave + 0x100) = 0;

    FlushInstructionCache(GetCurrentProcess(), cave, 0x1000);

    uint8_t patch[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    *(int32_t*)(patch + 1) = (int32_t)((intptr_t)cave - (intptr_t)(site + 5));

    DWORD oldProtect;
    if (!VirtualProtect((void*)site, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("[CAR] VirtualProtect fehlgeschlagen.\n");
        return false;
    }
    memcpy((void*)site, patch, sizeof(patch));
    VirtualProtect((void*)site, sizeof(patch), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (void*)site, sizeof(patch));

    g_forceCarAddr = (uintptr_t)(cave + 0x100);
    Log("[CAR] Hook installiert: site=0x%llX cave=0x%llX\n",
        (unsigned long long)site, (unsigned long long)cave);
    return true;
}

bool ApplySettings(const std::string& json) {
    bool allOk = true;
    for (const auto& s : SETTINGS) {
        if (USE_CAR_HOOK && s.absoluteAddress == VEH_CAR_ADDRESS) continue;

        long long v = 0;
        if (!GetIntAtPath(json, *s.path, v)) continue;

        int target = (int)v;
        if (s.absoluteAddress == VEH_LIVERY_ADDRESS) target += 1;

        int before = -1, after = -1;

        uintptr_t rva = RvaFromAbsolute(s.absoluteAddress);
        bool ok = PatchInt(At(rva), target, &before, &after);

        Log("[SET] %s json=%lld ziel=%d ok=%d vorher=%d nachher=%d\n",
            s.path->back().c_str(), v, target, ok ? 1 : 0, before, after);
        if (!ok) allOk = false;
    }
    return allOk;
}

bool ApplyCarHook(const std::string& json) {
    if (!USE_CAR_HOOK) return true;
    long long car = 0;
    if (!GetIntAtPath(json, P::Car, car)) return true;
    if (!InstallCarHook()) return false;
    bool ok = SafeWrite32(g_forceCarAddr, (int)car);
    Log("[CAR] forceCar=%lld ok=%d\n", car, ok ? 1 : 0);
    return ok;
}

bool ResolveClassBase(uintptr_t& base) {
    uintptr_t p1 = 0, p2 = 0, p3 = 0;
    uintptr_t rva = RvaFromAbsolute(CLASS_ROOT_ADDR);
    if (!SafeRead(At(rva) + CLASS_OFF1, &p1, sizeof(p1)) || !p1) return false;
    if (!SafeRead(p1 + CLASS_OFF2, &p2, sizeof(p2)) || !p2) return false;
    if (!SafeRead(p2, &p3, sizeof(p3)) || !p3) return false;
    base = p3;
    return true;
}

uintptr_t ClassFlagAddress(uintptr_t base, int classNumber) {
    return base + CLASS_FLAG_OFFSET + (uintptr_t)(classNumber - 1) * CLASS_STRIDE;
}

bool EntryIsClass(uintptr_t base, int classNumber) {
    uintptr_t entry = base + (uintptr_t)(classNumber - 1) * CLASS_STRIDE;
    uint64_t head0 = 0, ref0 = 0;
    int flag = -1;
    if (!SafeRead(entry, &head0, sizeof(head0))) return false;
    if (!SafeRead(base, &ref0, sizeof(ref0))) return false;
    if (!SafeRead(entry + CLASS_FLAG_OFFSET, &flag, sizeof(flag))) return false;
    return head0 == ref0 && (flag == 0 || flag == 1);
}

bool ApplyClassesNow(const std::map<int, int>& parsed) {
    uintptr_t base = 0;
    if (!ResolveClassBase(base) || !EntryIsClass(base, 1)) {
        Log("[CLASS] Liste nicht bereit.\n");
        return false;
    }

    bool allOk = true;
    for (const auto& kv : parsed) {
        if (!EntryIsClass(base, kv.first)) {
            Log("[CLASS] Klasse %d nicht in der Liste.\n", kv.first);
            allOk = false;
            continue;
        }
        uintptr_t addr = ClassFlagAddress(base, kv.first);
        int before = -1, after = -1;
        SafeRead(addr, &before, sizeof(before));
        bool ok = SafeWrite32(addr, kv.second);
        SafeRead(addr, &after, sizeof(after));
        Log("[CLASS] Klasse %d ziel=%d ok=%d vorher=%d nachher=%d\n",
            kv.first, kv.second, ok ? 1 : 0, before, after);
        if (!ok || after != kv.second) allOk = false;
    }
    return allOk;
}

bool ApplyClassStates(const std::string& json) {
    std::map<int, int> parsed;
    if (!ParseClassStates(json, parsed)) return true;
    if (parsed.empty()) return true;
    return ApplyClassesNow(parsed);
}

bool ReadHttpRequest(SOCKET s, std::string& headers, std::string& body) {
    std::string data;
    char buf[4096];
    size_t headerEnd = std::string::npos;

    while (headerEnd == std::string::npos) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        data.append(buf, n);
        if (data.size() > MAX_REQUEST_SIZE) return false;
        headerEnd = data.find("\r\n\r\n");
    }

    headers = data.substr(0, headerEnd);
    size_t bodyStart = headerEnd + 4;

    std::string lower = headers;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);

    size_t contentLength = 0;
    size_t clPos = lower.find("content-length:");
    if (clPos != std::string::npos) {
        try {
            contentLength = (size_t)std::stoul(lower.substr(clPos + 15));
        }
        catch (...) {
            return false;
        }
    }
    if (contentLength > MAX_REQUEST_SIZE) return false;

    while (data.size() - bodyStart < contentLength) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        data.append(buf, n);
        if (data.size() > MAX_REQUEST_SIZE) return false;
    }

    body = data.substr(bodyStart, contentLength);
    return true;
}

void SendResponse(SOCKET s, const char* status, const char* text) {
    std::string resp = std::string("HTTP/1.1 ") + status +
        "\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: " +
        std::to_string(strlen(text)) + "\r\n\r\n" + text;
    send(s, resp.c_str(), (int)resp.size(), 0);
}

void HandleClient(SOCKET clientSocket) {
    DWORD timeoutMs = 3000;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    std::string headers, body;
    if (!ReadHttpRequest(clientSocket, headers, body)) {
        Log("[HTTP] Request unvollstaendig oder zu gross.\n");
        SendResponse(clientSocket, "400 Bad Request", "BAD REQUEST");
        closesocket(clientSocket);
        return;
    }

    if (headers.find("POST /patch") != std::string::npos) {
        if (body.empty()) {
            Log("[HTTP] Leerer Body.\n");
            SendResponse(clientSocket, "400 Bad Request", "EMPTY BODY");
        }
        else {
            std::string processedBody = ApplyPlaceholders(body, headers);
            std::string json = StripComments(processedBody);

            bool setOk = ApplySettings(json);
            bool carOk = ApplyCarHook(json);
            bool classesOk = ApplyClassStates(json);

            if (!carOk) SendResponse(clientSocket, "500 Internal Server Error", "CAR HOOK FAILED");
            else if (!setOk) SendResponse(clientSocket, "500 Internal Server Error", "SETTING FAILED");
            else if (!classesOk) SendResponse(clientSocket, "503 Service Unavailable", "CLASSES NOT READY");
            else SendResponse(clientSocket, "200 OK", "OK");
        }
    }
    else {
        SendResponse(clientSocket, "200 OK", "PONG");
    }

    closesocket(clientSocket);
}

void StartHttpServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) return;

    BOOL reuse = TRUE;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    serverAddr.sin_port = htons(SERVER_PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(serverSocket);
        return;
    }

    listen(serverSocket, SOMAXCONN);

    InitLog();

    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket != INVALID_SOCKET) {
            std::thread(HandleClient, clientSocket).detach();
        }
    }
}

bool ValidateAddresses() {
    bool ok = true;

    for (const auto& s : SETTINGS) {
        int probe = 0;
        uintptr_t rva = RvaFromAbsolute(s.absoluteAddress);
        if (!SafeRead(At(rva), &probe, sizeof(probe))) {
            Log("[INIT] Adresse 0x%llX (RVA 0x%llX) ungueltig - Update oder ASLR-Problem?\n",
                (unsigned long long)s.absoluteAddress, (unsigned long long)rva);
            ok = false;
        }
    }

    int classProbe = 0;
    uintptr_t classRva = RvaFromAbsolute(CLASS_ROOT_ADDR);
    if (!SafeRead(At(classRva), &classProbe, sizeof(classProbe))) {
        Log("[INIT] CLASS_ROOT_ADDR (RVA 0x%llX) ungueltig - Update oder ASLR-Problem?\n",
            (unsigned long long)classRva);
        ok = false;
    }

    return ok;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    Sleep(2000);
    ValidateAddresses();
    StartHttpServer();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
        break;
    }
    return TRUE;
}