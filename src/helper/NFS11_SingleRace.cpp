// NFS11_SingleRace.asi - Single Race helper for the original Need for Speed Hot Pursuit (2010), NFS11.exe 1.0.5.0.
// v10 (2026-11-26): names of the 23 Remastered-only routes (RouteNames.h, event set v3) set into empty strings.
// v11 (2026-11-26): opponents written into the event record at StopTraffic (AI finish-message names, port of the
//   Remastered v39); route names always on, for the 10 online-only routes of event set v4 (n 42-51).
// v9 (2026-11-26): intro name tags hidden in Single Race events (tag format string -> empty); the loading screen shows
//   the selected car's class (ChallengeViewModel TierName).
// v8 (2026-11-26): the Race Settings conditions are written into the Single Race event records (list icons, info box,
//   loading screen follow them); SR.Summary for the picker's info box (WALL v3).
// v7 (2026-11-26): main menu label (string 349389 "WALL" -> ini [MenuLabel] Text, default SINGLE RACE); the Race
//   Settings prompt is native (WALL v2 sr_info.lua, to_main_menu = Menu/Start; "SR.OpenSettings" through
//   ResolveStringBinding); the overlay's top-right hint is gone.
// v6 (2026-11-26): the Race Settings menu (SR_Overlay9.cpp, Dear ImGui over Direct3D 9): F5 (ini [Overlay] Key) or
//   the pad's Back / View on the Single Race picker; it reads and writes the ini.
// v5.1 (2026-11-26): the event record is found and written at the launch link (RequestChallengeMode was too late
//   for the AI count); the loading hook only confirms it.
// v5 (2026-11-26): Remastered features from the ini (the Race Settings menu comes later):
//   - AI count ([Race] Racers, [HotPursuit] Racers / Cops, [CopHotPursuit] Racers): written into the current event
//     record (RequestChallengeMode hook) - counts and list lengths, limited to the entries the event holds;
//   - difficulty ([Event] Difficulty=Easy|Medium|Hard): balancing ids written into the event record (SetupRaceBalancing
//     reads them), player toughness / cop respawn through GetChallengeParam, AI toughness through GetOpponentToughness;
//     presets from HP2010 career events (classes 2 / 3 / 5);
//   - conditions ([Conditions] TimeOfDay, Weather, TimeProgression) through SetTimeOfDay;
//   - weapons ([Weapons] Mode=Classic|Custom, the 8 charges, Level): written into the Single Race sets 2300100 / 2300101
//     (WEAPONS overlay; the HP events point at them) before the first SetupWeaponAvailabilityForCompetitor;
//   - cop Hot Pursuit medals fitted to the racer count.
// v4 (2026-11-26): start positions in Single Race Races, natively (as Remastered helper v7): the call of the intro
//   setup 0x5DEB70 (one caller, 0x5DF49F) goes through Hook_IntroSetup. After the original has bound the sequence's
//   actors to the competitors (actor type 0x11 = the local player; mgr + 0x19A0 + 4*competitor = actor,
//   actor + 0x100 = competitor), the actors are sorted front to back and re-bound: AI racers to the front ones,
//   the player to the last one (mgr + 0x7930 = the player's actor index is updated). Runs at every setup, so the
//   second setup at the sequence start gives the same result. v3's Lua slot remap and F9 intro switch are removed
//   (the Hot Pursuit intro crashed with a Race).
// v3 (2026-11-26): Lua slot remap + F9 race intro switch (did not work: the race intro re-placed the cars).
// v2 (2026-11-26): opponent randomizer (from the Remastered helper / NFSHPR Event Randomizer 1.0), Single Race
//   events only: every AI racer and cop gets a random car (of the player's class by default; a racer stays a racer,
//   a cop a cop), AI racers a random paint of that car. StopTraffic (the first step of every event script) starts an
//   event; GetOpponentVehicleId / GetOpponentColourId are answered. ini [Opponents] as in the Remastered mod.
// v1 (2026-11-26):
//   * Launch: a PlayIt link for a Single Race event (res_id 2100000-2499999, route n = id % 100000) is sent as the
//     event of the chosen mode: Race 2100000+n, Hot Pursuit 2200000+n, Cop Hot Pursuit 2400000+n, with IsCop=0/1.
//   * Mode: ini [Event] Mode=Race|HotPursuit|CopHotPursuit. Until the Race Settings menu exists, F6 / F7 / F8 set it
//     (Race / Hot Pursuit / Cop Hot Pursuit) while the game window is active; the choice is saved to the ini.
// Hooks are put in at the natives' registration sites (push <func>; push <name>; call register), chained: if another
// plugin (the probe) already wrapped a native, the helper calls that wrapper.
// Log: NFS11_SingleRace.log next to the .asi ([Log] Enabled=0 turns it off).
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

static const DWORD kExpectedTimestamp = 0x4DC1385A;
static const char* kVersion = "helper 11";
#include "VehicleTable.h"
#include "PaintTable.h"

// ---- Lua 5.1 API inside NFS11.exe (cdecl)
struct lua_State;
typedef int(__cdecl* lua_CFunction)(lua_State*);
static int(__cdecl* const lua_gettop)(lua_State*) = (int(__cdecl*)(lua_State*))0xB41750;
static void(__cdecl* const lua_replace)(lua_State*, int) = (void(__cdecl*)(lua_State*, int))0xB41A10;
static int(__cdecl* const lua_type)(lua_State*, int) = (int(__cdecl*)(lua_State*, int))0xB41BC0;
static void(__cdecl* const lua_settop)(lua_State*, int) = (void(__cdecl*)(lua_State*, int))0xB41770;
static double(__cdecl* const lua_tonumber)(lua_State*, int) = (double(__cdecl*)(lua_State*, int))0xB41EE0;
static void(__cdecl* const lua_pushnil)(lua_State*) = (void(__cdecl*)(lua_State*))0xB422A0;
static void(__cdecl* const lua_pushboolean)(lua_State*, int) = (void(__cdecl*)(lua_State*, int))0xB425D0;
static void(__cdecl* const lua_pushnumber)(lua_State*, double) = (void(__cdecl*)(lua_State*, double))0xB422D0;
static const char*(__cdecl* const lua_tolstring)(lua_State*, int, size_t*) = (const char*(__cdecl*)(lua_State*, int, size_t*))0xB41FE0;
static void(__cdecl* const lua_pushstring)(lua_State*, const char*) = (void(__cdecl*)(lua_State*, const char*))0xB423C0;
static const uint32_t kLuaApi[] = {0xB41750, 0xB41770, 0xB41A10, 0xB41BC0, 0xB41EE0, 0xB41FE0, 0xB422D0, 0xB423C0, 0xB425D0};
static const int LUA_TNUMBER = 3, LUA_TSTRING = 4;

// ---- natives (name, function, registration site = the "push <func>")
struct NativeReg { const char* name; uint32_t func; uint32_t site; };
static const NativeReg kSendCommandLink = {"SendCommandLink", 0x79C160, 0x7A09CB};
static const NativeReg kStopTraffic = {"StopTraffic", 0x53A8D0, 0x54C31B};
static const NativeReg kGetOpponentVehicleId = {"GetOpponentVehicleId", 0x548F00, 0x54B492};
static const NativeReg kGetOpponentColourId = {"GetOpponentColourId", 0x548F50, 0x54B4A3};
// cutscene start slots: manager = [0xE2E66C] + 0x3CCC0, slot pointers at +0x19A0 (8 x 4 bytes);
// slot + 0x10: 4x4 float transform (row 2 = direction, row 3 = position), as 0x5DD140 copies it
static const uint32_t kGameRootPtr = 0xE2E66C, kOffCutsceneMgr = 0x3CCC0, kOffSlots = 0x19A0;
static const NativeReg kRequestChallengeMode = {"RequestChallengeMode", 0x539D60, 0x54BA30};
static const NativeReg kGetChallengeParam = {"GetChallengeParam", 0x544FE0, 0x54B975};
static const NativeReg kGetOpponentToughness = {"GetOpponentToughness", 0x548FC0, 0x54B4C5};
static const NativeReg kSetTimeOfDay = {"SetTimeOfDay", 0x53CD50, 0x54BD60};
static const NativeReg kWeaponsForCompetitor = {"SetupWeaponAvailabilityForCompetitor", 0x543320, 0x54A2D1};
static lua_CFunction const g_GetSelectedVehicleId = (lua_CFunction)0x53A9F0;   // called directly (not hooked)

// ---- log / ini
static CRITICAL_SECTION g_lock;
static FILE* g_log;
static char g_ini[MAX_PATH];

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

static std::string IniString(const char* sec, const char* key, const char* def = "") {
    char buf[1024];
    GetPrivateProfileStringA(sec, key, def, buf, sizeof buf, g_ini);
    return buf;
}

static const char* kDefaultIni =
    "; NFS11 Single Race helper settings\r\n"
    "[Event]\r\n"
    "; Race, HotPursuit or CopHotPursuit (F6 / F7 / F8 in the game set it too)\r\n"
    "Mode=Race\r\n"
    "\r\n"
    "[Opponents]\r\n"
    "; Single Race events only: AI racers and cops get random cars\r\n"
    "RandomizeRacers=1\r\n"
    "RandomizeCops=1\r\n"
    "; 1 = opponents and cops come from the class of the player's car\r\n"
    "; 0 = each opponent keeps its own class (only the car changes)\r\n"
    "MatchPlayerClass=1\r\n"
    "; 1 = no two opponents get the same car while the class has enough cars\r\n"
    "AvoidDuplicates=1\r\n"
    "; 1 = opponents never get the same model as the player\r\n"
    "AvoidPlayerModel=0\r\n"
    "; 1 = AI racers get a random paint colour of their car (cops keep their liveries)\r\n"
    "RandomizeRacerColours=1\r\n"
    "; vehicle ids never to pick, comma separated (ids are in the log)\r\n"
    "Exclude=\r\n"
    "\r\n"

    "[Log]\r\n"
    "Enabled=1\r\n";

// ---- mode
enum Mode { kRace, kHotPursuit, kCopHotPursuit };
static const char* kModeIni[] = {"Race", "HotPursuit", "CopHotPursuit"};
static const char* kModeName[] = {"Race", "Hot Pursuit", "Cop Hot Pursuit"};
static const uint32_t kModeBase[] = {2100000, 2200000, 2400000};
static const uint32_t kSrFirst = 2100000, kSrLast = 2499999;

static Mode ReadMode() {
    std::string m = IniString("Event", "Mode", "Race");
    for (int i = 0; i < 3; ++i)
        if (!_stricmp(m.c_str(), kModeIni[i])) return (Mode)i;
    return kRace;
}

// ---- hook plumbing
static bool PatchSiteChained(const NativeReg& r, uint32_t wrapper, lua_CFunction* orig) {
    uint8_t* p = (uint8_t*)r.site;
    if (p[0] != 0x68) {
        Log("%s: registration site %08X is not a push - not hooked", r.name, r.site);
        return false;
    }
    uint32_t cur = *(uint32_t*)(p + 1);
    if (cur != r.func) {
        // another plugin's wrapper (outside the exe image) is fine: chain to it
        if (cur >= 0x400000 && cur < 0x1000000) {
            Log("%s: registration site %08X holds %08X, expected %08X - not hooked", r.name, r.site, cur, r.func);
            return false;
        }
        Log("%s: already wrapped by another plugin (%08X), chained", r.name, cur);
    }
    *orig = (lua_CFunction)cur;
    DWORD old;
    VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    *(uint32_t*)(p + 1) = wrapper;
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

// Set (or add) key=value in a PlayIt link.
static void SetLinkValue(std::string& link, const char* key, const std::string& value) {
    std::string k = key;
    size_t pos = std::string::npos;
    for (const char* sep : {"?", "&"}) {
        size_t f = link.find(std::string(sep) + k + "=");
        if (f != std::string::npos) { pos = f + 1; break; }
    }
    if (pos != std::string::npos) {
        size_t vs = pos + k.size() + 1;
        size_t ve = link.find('&', vs);
        link.replace(vs, ve == std::string::npos ? std::string::npos : ve - vs, value);
    } else {
        link += (link.find('?') == std::string::npos ? "?" : "&") + k + "=" + value;
    }
}

static bool GetLinkValue(const std::string& link, const char* key, std::string& value) {
    std::string k = key;
    for (const char* sep : {"?", "&"}) {
        size_t f = link.find(std::string(sep) + k + "=");
        if (f != std::string::npos) {
            size_t vs = f + 1 + k.size() + 1;
            size_t ve = link.find('&', vs);
            value = link.substr(vs, ve == std::string::npos ? std::string::npos : ve - vs);
            return true;
        }
    }
    return false;
}

// ---- SendCommandLink: Single Race launch
static lua_CFunction g_origSendCommandLink;
static volatile LONG g_srMode = -1;   // mode of the last launched event if it was a Single Race event, else -1
static volatile LONG g_srEventId;          // the Single Race event launched last (0 = none)
static volatile LONG g_appliedEventId;     // the event the v5 settings were written into
static void ApplyEventSettings(const char* where);
static void SetIntroTags(bool singleRace);
static void LoadEventSettings();

static int __cdecl Hook_SendCommandLink(lua_State* L) {
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
        const char* s = lua_tolstring(L, 1, nullptr);
        if (s && !strncmp(s, "PlayIt", 6)) {
            std::string link = s, rid;
            if (GetLinkValue(link, "res_id", rid)) {
                uint32_t id = (uint32_t)strtoul(rid.c_str(), nullptr, 10);
                if (id >= kSrFirst && id <= kSrLast) {
                    Mode m = ReadMode();
                    uint32_t n = id % 100000;
                    uint32_t nid = kModeBase[m] + n;
                    SetLinkValue(link, "res_id", std::to_string(nid));
                    SetLinkValue(link, "IsCop", m == kCopHotPursuit ? "1" : "0");
                    Log("event %u -> Single Race %s %u; link \"%s\"", id, kModeName[m], nid, link.c_str());
                    lua_pushstring(L, link.c_str());
                    lua_replace(L, 1);
                    InterlockedExchange(&g_srMode, m);
                    InterlockedExchange(&g_srEventId, (LONG)nid);
                    InterlockedExchange(&g_appliedEventId, 0);
                    ApplyEventSettings("launch");
                    SetIntroTags(true);
                } else {
                    Log("event %u (not a Single Race event) - unchanged", id);
                    InterlockedExchange(&g_srMode, -1);
                    InterlockedExchange(&g_srEventId, 0);
                    SetIntroTags(false);
                }
            }
        }
    }
    return g_origSendCommandLink(L);
}


// ---- opponents and cops (from the Remastered helper / NFSHPR Event Randomizer 1.0)
struct OpponentSettings {
    int racers, cops, matchClass, avoidDuplicates, avoidPlayerModel, racerColours;
    uint32_t exclude[64];
    int excludeCount;
};
static OpponentSettings S;

static int IniInt(const char* sec, const char* key, int def) { return (int)GetPrivateProfileIntA(sec, key, def, g_ini); }

static void LoadSettings() {
    OpponentSettings s;
    memset(&s, 0, sizeof(s));
    // an ini from helper 1 has no [Opponents]: add it with the defaults
    if (IniString("Opponents", "RandomizeRacers", "\x01") == "\x01") {
        const char* keys[][2] = {{"RandomizeRacers", "1"}, {"RandomizeCops", "1"}, {"MatchPlayerClass", "1"},
                                 {"AvoidDuplicates", "1"}, {"AvoidPlayerModel", "0"}, {"RandomizeRacerColours", "1"},
                                 {"Exclude", ""}};
        for (auto& k : keys) WritePrivateProfileStringA("Opponents", k[0], k[1], g_ini);
        Log("ini: [Opponents] added with the defaults");
    }
    s.racers = IniInt("Opponents", "RandomizeRacers", 1);
    s.cops = IniInt("Opponents", "RandomizeCops", 1);
    s.matchClass = IniInt("Opponents", "MatchPlayerClass", 1);
    s.avoidDuplicates = IniInt("Opponents", "AvoidDuplicates", 1);
    s.avoidPlayerModel = IniInt("Opponents", "AvoidPlayerModel", 0);
    s.racerColours = IniInt("Opponents", "RandomizeRacerColours", 1);
    std::string t = IniString("Opponents", "Exclude");
    uint32_t v = 0;
    bool have = false;
    for (const char* p = t.c_str();; ++p) {
        if (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); have = true; }
        else {
            if (have && s.excludeCount < 64) s.exclude[s.excludeCount++] = v;
            v = 0;
            have = false;
            if (!*p) break;
        }
    }
    S = s;
}

static uint64_t g_rng;
static uint64_t Rand() {  // xorshift64*
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545F4914F6CDD1DULL;
}
static unsigned RandBelow(unsigned n) { return n ? (unsigned)(Rand() % n) : 0; }

static const char* kRacerClass[] = {"?", "Sports", "Performance", "Super", "Exotic", "Hyper"};
static const char* kCopClass[] = {"?", "Traffic Police", "Highway Patrol", "Rapid Deployment", "Speed Enforcement", "Special Response"};

static const Vehicle* FindVehicle(uint32_t id) {
    for (int i = 0; i < kVehicleCount; ++i)
        if (kVehicles[i].id == id) return &kVehicles[i];
    return nullptr;
}
static const char* ClassName(const Vehicle* v) {
    if (!v || v->tier < 1 || v->tier > 5) return "?";
    return v->side == 3 ? kCopClass[v->tier] : kRacerClass[v->tier];
}
static const char* VName(const Vehicle* v) { return v ? v->name : "unknown"; }

static bool Excluded(uint32_t id) {
    for (int i = 0; i < S.excludeCount; ++i)
        if (S.exclude[i] == id) return true;
    return false;
}

struct Slot { int competitor; uint32_t original, chosen, colour; };  // colour 0 = not chosen yet
struct EventState {
    int number;
    bool singleRace;
    Slot slots[64];
    int slotCount;
    uint32_t playerId;
    bool playerKnown;
};
static EventState E;

// The player's car: GetSelectedVehicleId called on the current Lua state (clears the stack).
static void LearnPlayerCar(lua_State* L) {
    if (E.playerKnown) return;
    lua_settop(L, 0);
    int n = g_GetSelectedVehicleId(L);
    if (n >= 1 && lua_type(L, -1) == LUA_TNUMBER) {
        E.playerId = (uint32_t)lua_tonumber(L, -1);
        E.playerKnown = true;
        const Vehicle* v = FindVehicle(E.playerId);
        Log("player car: %u %s [%s %s]", E.playerId, VName(v), v ? (v->side == 3 ? "cop" : "racer") : "?", ClassName(v));
    }
    lua_settop(L, 0);
}

static bool UsedThisEvent(uint32_t id) {
    for (int i = 0; i < E.slotCount; ++i)
        if (E.slots[i].chosen == id) return true;
    return false;
}

static uint32_t PickReplacement(uint32_t original) {
    const Vehicle* o = FindVehicle(original);
    if (!o) return original;  // traffic / unknown vehicle: leave it
    if (o->side == 2 && !S.racers) return original;
    if (o->side == 3 && !S.cops) return original;
    const Vehicle* p = E.playerKnown ? FindVehicle(E.playerId) : nullptr;
    int tier = (S.matchClass && p) ? p->tier : o->tier;
    const Vehicle* pool[64];
    int n = 0;
    for (int pass = 0; pass < 2 && n == 0; ++pass) {  // pass 1 allows duplicates if needed
        for (int i = 0; i < kVehicleCount && n < 64; ++i) {
            const Vehicle* v = &kVehicles[i];
            if (v->side != o->side || v->tier != tier || Excluded(v->id)) continue;
            if (S.avoidPlayerModel && p && lstrcmpA(v->name, p->name) == 0) continue;
            if (pass == 0 && S.avoidDuplicates && UsedThisEvent(v->id)) continue;
            pool[n++] = v;
        }
    }
    if (!n) return original;
    return pool[RandBelow((unsigned)n)]->id;
}

static lua_CFunction g_origStopTraffic, g_origGetOpponentVehicleId, g_origGetOpponentColourId;
static void PickOpponentsAtStart(lua_State* L);
static bool g_prePicked;

static int __cdecl Hook_StopTraffic(lua_State* L) {
    ++E.number;
    E.slotCount = 0;
    E.playerKnown = false;
    E.playerId = 0;
    LONG m = g_srMode;
    E.singleRace = m >= 0;
    if (E.singleRace) {
        LoadSettings();
        Log("---- event %d loading (Single Race %s): RandomizeRacers=%d RandomizeCops=%d MatchPlayerClass=%d "
            "AvoidDuplicates=%d RandomizeRacerColours=%d", E.number, kModeName[m], S.racers, S.cops, S.matchClass,
            S.avoidDuplicates, S.racerColours);
    }
    g_prePicked = false;
    if (E.singleRace) {
        ApplyEventSettings("StopTraffic");
        PickOpponentsAtStart(L);
    }
    return g_origStopTraffic(L);
}

// GetOpponentVehicleId(competitor_index) -> vehicle id
static int __cdecl Hook_GetOpponentVehicleId(lua_State* L) {
    int competitor = (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TNUMBER) ? (int)lua_tonumber(L, 1) : -1;
    int n = g_origGetOpponentVehicleId(L);
    if (!E.singleRace || n < 1 || lua_type(L, -1) != LUA_TNUMBER) return n;
    uint32_t original = (uint32_t)lua_tonumber(L, -1);
    if (g_prePicked) {  // the record already has this event's pick: just remember the competitor's car (colours)
        for (int i = 0; i < E.slotCount; ++i)
            if (E.slots[i].competitor == competitor && E.slots[i].chosen == original) return n;
        if (E.slotCount < 64) E.slots[E.slotCount++] = Slot{competitor, original, original, 0};
        const Vehicle* c = FindVehicle(original);
        Log("competitor %d: %u %s [%s] (picked at the event start)", competitor, original, VName(c), ClassName(c));
        return n;
    }
    // same competitor and same original car in this event -> same answer (spawn, cutscene, redeploy)
    for (int i = 0; i < E.slotCount; ++i) {
        Slot& sl = E.slots[i];
        if (sl.competitor == competitor && sl.original == original) {
            if (sl.chosen == original) return n;
            lua_settop(L, 0);
            lua_pushnumber(L, (double)sl.chosen);
            return 1;
        }
    }
    LearnPlayerCar(L);  // clears the stack
    uint32_t chosen = PickReplacement(original);
    if (E.slotCount < 64) E.slots[E.slotCount++] = Slot{competitor, original, chosen, 0};
    const Vehicle* o = FindVehicle(original);
    const Vehicle* c = FindVehicle(chosen);
    Log("competitor %d: %u %s [%s] -> %u %s [%s]", competitor, original, VName(o), ClassName(o), chosen, VName(c),
        ClassName(c));
    lua_settop(L, 0);
    lua_pushnumber(L, (double)chosen);
    return 1;
}

static const PaintList* FindPaints(uint32_t vehicle) {
    for (int i = 0; i < kPaintCount; ++i)
        if (kPaints[i].vehicle == vehicle) return &kPaints[i];
    return nullptr;
}

// A random colour of the car, preferring one no other AI with the same car has in this event.
static uint32_t PickColour(const PaintList* pl, int self) {
    uint32_t pool[16];
    int n = 0;
    for (int pass = 0; pass < 2 && n == 0; ++pass)
        for (int k = 0; k < pl->count; ++k) {
            bool taken = false;
            for (int i = 0; pass == 0 && i < E.slotCount; ++i)
                if (i != self && E.slots[i].chosen == pl->vehicle && E.slots[i].colour == pl->colours[k]) taken = true;
            if (!taken) pool[n++] = pl->colours[k];
        }
    return n ? pool[RandBelow((unsigned)n)] : 0;
}

// GetOpponentColourId(competitor_index) -> paint colour id (called right after GetOpponentVehicleId)
static int __cdecl Hook_GetOpponentColourId(lua_State* L) {
    int competitor = (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TNUMBER) ? (int)lua_tonumber(L, 1) : -1;
    int n = g_origGetOpponentColourId(L);
    if (!E.singleRace || !S.racerColours || n < 1 || lua_type(L, -1) != LUA_TNUMBER || competitor < 0) return n;
    int self = -1;
    for (int i = E.slotCount - 1; i >= 0; --i)  // the latest car of this competitor
        if (E.slots[i].competitor == competitor) { self = i; break; }
    if (self < 0) return n;
    Slot& sl = E.slots[self];
    const Vehicle* v = FindVehicle(sl.chosen);
    if (!v || v->side != 2) return n;  // cops keep their liveries
    if (!sl.colour) {
        const PaintList* pl = FindPaints(sl.chosen);
        if (!pl || pl->count == 0) return n;
        uint32_t original = (uint32_t)lua_tonumber(L, -1);
        sl.colour = PickColour(pl, self);
        if (!sl.colour) return n;
        Log("competitor %d: colour %u -> %u (%d colours for %s)", competitor, original, sl.colour, pl->count, VName(v));
    }
    lua_settop(L, 0);
    lua_pushnumber(L, (double)sl.colour);
    return 1;
}

// ---- start positions (Races): native intro setup, AI racers in the front slots, the player behind them
static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (const uint8_t*)p + n <= (const uint8_t*)mi.BaseAddress + mi.RegionSize;
}

static const uint32_t kIntroSetup = 0x5DEB70, kIntroSetupCall = 0x5DF49F;
static const uint32_t kOffActorBase = 0x100, kActorStride = 0x110, kOffActorCount = 0x980, kOffActorComp = 0x100;
static const uint32_t kOffPlayerActor = 0x7930, kOffCompMgr = 0x7B70, kOffLocalComp = 0x5F04;
typedef void(__attribute__((thiscall)) * IntroSetupFn)(void* mgr);

static void PlacePlayerLast(uint8_t* mgr) {
    uint32_t* arr = (uint32_t*)(mgr + kOffSlots);
    uint32_t cm = 0;
    if (!Readable(arr, 32) || !Readable(mgr + kOffCompMgr, 4) || !(cm = *(uint32_t*)(mgr + kOffCompMgr)) ||
        !Readable((void*)(cm + kOffLocalComp), 4)) {
        Log("intro setup: manager not readable - not changed");
        return;
    }
    int player = *(int*)(cm + kOffLocalComp);
    uint32_t actors0 = (uint32_t)(mgr + kOffActorBase);
    int nActors = *(int*)(mgr + kOffActorCount);
    if (player < 0 || player > 7 || !arr[player]) { Log("intro setup: no actor for the player (%d) - not changed", player); return; }
    int comps[8], n = 0;
    uint32_t act[8];
    for (int c = 0; c < 8; ++c) {
        uint32_t a = arr[c];
        if (!a) continue;
        if (a < actors0 || (a - actors0) % kActorStride || (int)((a - actors0) / kActorStride) >= nActors ||
            !Readable((void*)a, kActorStride)) {
            Log("intro setup: competitor %d has an unexpected actor %08X - not changed", c, a);
            return;
        }
        comps[n] = c;
        act[n++] = a;
    }
    if (n < 2) return;
    const float* pd = (const float*)(arr[player] + 0x10);   // the player's actor transform: row 2 = direction
    float dir[3] = {pd[8], pd[9], pd[10]};
    float key[8];
    for (int i = 0; i < n; ++i) {
        const float* m = (const float*)(act[i] + 0x10);
        key[i] = m[12] * dir[0] + m[13] * dir[1] + m[14] * dir[2];
    }
    int ord[8];
    for (int i = 0; i < n; ++i) ord[i] = i;
    for (int a = 0; a < n; ++a)  // front (largest key) first
        for (int b = a + 1; b < n; ++b)
            if (key[ord[b]] > key[ord[a]]) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
    // AI competitors in index order take the actors front to back; the player the last one
    uint32_t na[8] = {};
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (comps[i] != player) na[comps[i]] = act[ord[k++]];
    na[player] = act[ord[n - 1]];
    char buf[160];
    int l = 0;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        int c = comps[i];
        if (arr[c] != na[c]) changed = true;
        l += snprintf(buf + l, sizeof buf - l, "%s%d:%d", i ? " " : "", c, (int)((na[c] - actors0) / kActorStride));
    }
    if (!changed) { Log("intro setup: player already behind the AI racers"); return; }
    for (int i = 0; i < n; ++i) {
        int c = comps[i];
        arr[c] = na[c];
        *(uint32_t*)(na[c] + kOffActorComp) = (uint32_t)c;
    }
    int oldPA = *(int*)(mgr + kOffPlayerActor);
    *(int*)(mgr + kOffPlayerActor) = (int)((na[player] - actors0) / kActorStride);
    Log("intro setup (Race): competitor:actor %s; player actor %d -> %d", buf, oldPA, *(int*)(mgr + kOffPlayerActor));
}

static void __fastcall Hook_IntroSetup(void* mgr) {
    ((IntroSetupFn)kIntroSetup)(mgr);
    if (E.singleRace && g_srMode == kRace) PlacePlayerLast((uint8_t*)mgr);
}

static bool InstallIntroHook() {
    uint8_t* p = (uint8_t*)kIntroSetupCall;
    if (p[0] != 0xE8 || (uint32_t)(kIntroSetupCall + 5 + *(int32_t*)(p + 1)) != kIntroSetup) {
        Log("intro setup: call site %08X does not match - start positions not hooked", kIntroSetupCall);
        return false;
    }
    const uint8_t* f = (const uint8_t*)kIntroSetup;
    if (f[0] != 0x55 || f[1] != 0x8B || f[2] != 0xEC) { Log("intro setup: function does not match - not hooked"); return false; }
    DWORD old;
    VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t*)(p + 1) = (int32_t)((uint32_t)&Hook_IntroSetup - (kIntroSetupCall + 5));
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

#include "SR_Shared9.h"
#include <stdarg.h>
#include "features.inc"
#include "label.inc"
#include "intro_loading.inc"
#include "crash.inc"
#include "opponents.inc"

// ---- keys
static DWORD WINAPI KeyThread(LPVOID) {
    const int keys[] = {VK_F6, VK_F7, VK_F8};
    bool down[3] = {};
    for (;;) {
        Sleep(30);
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) continue;
        for (int k = 0; k < 3; ++k) {
            bool d = (GetAsyncKeyState(keys[k]) & 0x8000) != 0;
            if (d && !down[k]) {
                WritePrivateProfileStringA("Event", "Mode", kModeIni[k], g_ini);
                Log("mode: %s (key F%d)", kModeName[k], 6 + k);
                MessageBeep(k == 0 ? MB_OK : (k == 1 ? MB_ICONASTERISK : MB_ICONEXCLAMATION));
            }
            down[k] = d;
        }
    }
}

static bool CheckGame() {
    uint8_t* base = (uint8_t*)GetModuleHandleA(nullptr);
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    if ((uint32_t)base != 0x400000 || nt->FileHeader.TimeDateStamp != kExpectedTimestamp) {
        Log("not NFS11.exe 1.0.5.0 (base %08X, timestamp %08X) - helper disabled", (uint32_t)base,
            (uint32_t)nt->FileHeader.TimeDateStamp);
        return false;
    }
    for (uint32_t a : kLuaApi) {
        const uint8_t* p = (const uint8_t*)a;
        if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
            Log("Lua API check failed at %08X - helper disabled", a);
            return false;
        }
    }
    return true;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    InitializeCriticalSection(&g_lock);
    char dir[MAX_PATH];
    GetModuleFileNameA(inst, dir, MAX_PATH);
    if (char* s = strrchr(dir, '\\')) s[1] = 0;
    snprintf(g_ini, sizeof g_ini, "%sNFS11_SingleRace.ini", dir);
    bool fresh = GetFileAttributesA(g_ini) == INVALID_FILE_ATTRIBUTES;
    if (fresh) {
        if (FILE* f = fopen(g_ini, "wb")) { fputs(kDefaultIni, f); fclose(f); }
    }
    if (GetPrivateProfileIntA("Log", "Enabled", 1, g_ini)) {
        char logPath[MAX_PATH];
        snprintf(logPath, sizeof logPath, "%sNFS11_SingleRace.log", dir);
        g_log = fopen(logPath, "a");
    }
    Log("---- NFS11 Single Race %s loaded%s", kVersion, fresh ? " (default ini written)" : "");
    InstallCrashLogger(dir);
    if (!CheckGame()) return TRUE;
    if (PatchSiteChained(kSendCommandLink, (uint32_t)&Hook_SendCommandLink, &g_origSendCommandLink))
        Log("SendCommandLink hooked");
    if (PatchSiteChained(kStopTraffic, (uint32_t)&Hook_StopTraffic, &g_origStopTraffic) &&
        PatchSiteChained(kGetOpponentVehicleId, (uint32_t)&Hook_GetOpponentVehicleId, &g_origGetOpponentVehicleId))
        Log("opponent randomizer hooked (%d cars)", kVehicleCount);
    if (PatchSiteChained(kGetOpponentColourId, (uint32_t)&Hook_GetOpponentColourId, &g_origGetOpponentColourId))
        Log("racer colours hooked (%d cars with paints)", kPaintCount);
    {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        g_rng = (uint64_t)q.QuadPart ^ ((uint64_t)GetTickCount() << 32) ^ 0x9E3779B97F4A7C15ULL;
        if (!g_rng) g_rng = 1;
    }
    {
        struct { const NativeReg* r; lua_CFunction hook; lua_CFunction* orig; } hs[] = {
            {&kRequestChallengeMode, &Hook_RequestChallengeMode, &g_origRequestChallengeMode},
            {&kGetChallengeParam, &Hook_GetChallengeParam, &g_origGetChallengeParam},
            {&kGetOpponentToughness, &Hook_GetOpponentToughness, &g_origGetOpponentToughness},
            {&kSetTimeOfDay, &Hook_SetTimeOfDay, &g_origSetTimeOfDay},
            {&kWeaponsForCompetitor, &Hook_WeaponsForCompetitor, &g_origWeaponsForCompetitor},
        };
        int ok = 0;
        for (auto& h : hs) ok += PatchSiteChained(*h.r, (uint32_t)h.hook, h.orig);
        Log("event settings hooked (%d of 5: RequestChallengeMode, GetChallengeParam, GetOpponentToughness, SetTimeOfDay, "
            "SetupWeaponAvailabilityForCompetitor)", ok);
        LoadEventSettings();
    }
    if (InstallIntroHook()) Log("start positions hooked (intro setup %08X via %08X)", kIntroSetup, kIntroSetupCall);
    // ini of helper 3: [Race] Intro is no longer used
    if (IniString("Race", "Intro", "\x01") != "\x01") WritePrivateProfileStringA("Race", "Intro", nullptr, g_ini);
    LoadSettings();
    Log("mode: %s (ini)", kModeName[ReadMode()]);
    CreateThread(nullptr, 0, KeyThread, nullptr, 0, nullptr);
    InstallMenu();
    StartMenuLabel();
    if (InstallTagPatch()) Log("intro tags hooked (tag string load %08X)", kTagLoad);
    if (InstallTierHook()) Log("loading screen class hooked (%08X)", kTierSite);
    return TRUE;
}
