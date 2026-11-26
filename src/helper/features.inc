
// ===================================================================== v5: event settings (ini, same keys as Remastered)
enum TimeOfDay { kDawn, kMorning, kNoon, kAfternoon, kSunset, kNight, kTimeCount };
enum Weather { kDry, kWet, kRain, kStorm, kWeatherCount };
enum WeaponsMode { kWeaponsClassic, kWeaponsCustom };
enum Difficulty { kEasy, kMedium, kHard, kDifficultyCount };
static const char* kTimeNames[kTimeCount] = {"Dawn", "Morning", "Noon", "Afternoon", "Sunset", "Night"};
static const float kTimeHours[kTimeCount] = {6.0f, 9.0f, 12.0f, 15.5f, 19.5f, 23.0f};
static const char* kWeatherNames[kWeatherCount] = {"Dry", "Wet", "Rain", "Storm"};
static const char* kWeatherLabels[kWeatherCount] = {"Dry", "Wet roads", "Rain", "Storm"};
static const char* kDifficultyNames[kDifficultyCount] = {"Easy", "Medium", "Hard"};
static const int kWeaponCount = 8, kMaxCharges = 5;
static const char* kWeaponKeys[kWeaponCount] = {"RacerSpikeStrip", "RacerEMP", "Jammer", "Turbo",
                                                "CopSpikeStrip", "CopEMP", "RoadBlock", "Helicopter"};
static const int kClassicCharges[kWeaponCount] = {0, 0, 0, 0, 3, 0, 3, 2};
static const int kDefaultCharges[kWeaponCount] = {2, 2, 1, 1, 2, 2, 2, 1};

struct Settings {
    int difficulty, time, weather, timeProgression;
    int raceRacers, hpRacers, hpCops, copRacers;
    int weaponsMode, charges[kWeaponCount], weaponLevel;
    int copRespawn;  // -1 = the difficulty's own
};
static Settings g_set;

static int NameIndexA(const std::string& s, const char* const* names, int n, int def) {
    for (int i = 0; i < n; ++i)
        if (!_stricmp(s.c_str(), names[i])) return i;
    return def;
}
static void EnsureKey(const char* sec, const char* key, const char* def) {
    if (IniString(sec, key, "\x01") == "\x01") WritePrivateProfileStringA(sec, key, def, g_ini);
}
static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static Settings ReadEventSettings() {
    EnsureKey("Event", "Difficulty", "Medium");
    EnsureKey("Conditions", "TimeOfDay", "Noon");
    EnsureKey("Conditions", "Weather", "Dry");
    EnsureKey("Conditions", "TimeProgression", "On");
    EnsureKey("Race", "Racers", "7");
    EnsureKey("HotPursuit", "Racers", "3");
    EnsureKey("HotPursuit", "Cops", "4");
    EnsureKey("HotPursuit", "CopRespawnTime", "");
    EnsureKey("CopHotPursuit", "Racers", "7");
    EnsureKey("Weapons", "Mode", "Classic");
    EnsureKey("Weapons", "Level", "1");
    for (int w = 0; w < kWeaponCount; ++w) EnsureKey("Weapons", kWeaponKeys[w], std::to_string(kDefaultCharges[w]).c_str());
    Settings s;
    s.difficulty = NameIndexA(IniString("Event", "Difficulty", "Medium"), kDifficultyNames, kDifficultyCount, kMedium);
    s.time = NameIndexA(IniString("Conditions", "TimeOfDay", "Noon"), kTimeNames, kTimeCount, kNoon);
    s.weather = NameIndexA(IniString("Conditions", "Weather", "Dry"), kWeatherNames, kWeatherCount, kDry);
    std::string tp = IniString("Conditions", "TimeProgression", "On");
    s.timeProgression = !(!_stricmp(tp.c_str(), "Off") || tp == "0" || !_stricmp(tp.c_str(), "false"));
    s.raceRacers = Clamp(IniInt("Race", "Racers", 7), 1, 7);
    s.hpRacers = Clamp(IniInt("HotPursuit", "Racers", 3), 1, 6);
    s.hpCops = Clamp(IniInt("HotPursuit", "Cops", 4), 1, 6);
    if (s.hpRacers + s.hpCops > 7) s.hpCops = 7 - s.hpRacers;
    s.copRacers = Clamp(IniInt("CopHotPursuit", "Racers", 7), 1, 7);
    s.weaponsMode = !_stricmp(IniString("Weapons", "Mode", "Classic").c_str(), "Custom") ? kWeaponsCustom : kWeaponsClassic;
    for (int w = 0; w < kWeaponCount; ++w) s.charges[w] = Clamp(IniInt("Weapons", kWeaponKeys[w], kDefaultCharges[w]), 0, kMaxCharges);
    s.weaponLevel = Clamp(IniInt("Weapons", "Level", 1), 1, 3);
    std::string rt = IniString("HotPursuit", "CopRespawnTime", "");
    s.copRespawn = -1;
    {
        const char* q = rt.c_str();
        while (*q == ' ' || *q == '\t') ++q;
        if (*q) {
            const char* d = q;
            long n = 0;
            for (; *d >= '0' && *d <= '9'; ++d) n = n < 100000 ? n * 10 + (*d - '0') : n;
            while (*d == ' ' || *d == '\t') ++d;
            if (d == q || *d) Log("ini [HotPursuit] CopRespawnTime=%s is not a whole number of seconds - the difficulty's own delay is used", q);
            else s.copRespawn = n > 3600 ? 3600 : (int)n;
        }
    }
    return s;
}
static void LoadEventSettings() { g_set = ReadEventSettings(); }

// ---- difficulty presets (HP2010 career events; balancing ids from BALANCINGDATA, padded to 7 entries where needed)
struct RacePreset { const char* source; uint32_t race, profile, shortcuts, cops; float playerScale; };
static const RacePreset kRacePresets[kDifficultyCount] = {
    {"career race 572222 Encore Performance (class 2)", 695386, 745009, 695374, 0, 2.25f},
    {"career race 572224 Spoilt for Choice (class 3)", 695617, 695615, 745472, 0, 2.75f},
    {"career race 695482 Faster than Light (class 5)", 695621, 695624, 745472, 0, 3.0f},
};
struct HpPreset { const char* source; uint32_t race, profile, shortcuts, cops; float playerScale, respawn, racerTough, copTough[6]; };
static const HpPreset kHpPresets[kDifficultyCount] = {
    {"career Hot Pursuit 572362 Escape Lane (class 2)", 674399, 688501, 688502, 688454, 2.25f, 60.f, 0.17f, {0.25f, 0.25f, 0.35f, 0.35f, 0.25f, 0.25f}},
    {"career Hot Pursuit 572392 Hotting Up (class 3)", 674844, 688556, 688557, 688470, 2.75f, 60.f, 0.17f, {0.28f, 0.28f, 0.35f, 0.45f, 0.28f, 0.28f}},
    {"career Hot Pursuit 572398 Calm before the Storm (class 5)", 674848, 688587, 688588, 688472, 3.0f, 90.f, 0.17f, {0.33f, 0.33f, 0.33f, 0.33f, 0.33f, 0.33f}},
};
struct CopPreset { const char* source; uint32_t shortcuts, hpCops; float playerCopScale, racerTough[7]; };
static const CopPreset kCopPresets[kDifficultyCount] = {
    {"career cop event 572318 Point of Impact (cop class 2)", 688600, 744065, 2.25f, {0.25f, 0.25f, 0.22f, 0.2f, 0.2f, 0.2f, 0.2f}},
    {"career cop event 572338 Run to the Hills (cop class 3)", 744211, 744067, 2.5f, {0.35f, 0.32f, 0.3f, 0.27f, 0.24f, 0.22f, 0.2f}},
    {"career cop event 572348 Hard Target (cop class 5)", 688606, 744079, 3.0f, {0.35f, 0.28f, 0.28f, 0.35f, 0.28f, 0.35f, 0.28f}},
};

// ---- the current event record: [[root+0x13478]+0x28] + [root+0x1347C] (as SetupChallengeWeaponAvailability reads it)
static const uint32_t kOffChallengeRes = 0x13478, kOffChallengeOff = 0x1347C, kHashEvent = 0x73226C8B;
// field offsets in the event's data (HP2010 ChallengeInstance schema)
static const uint32_t kFRacerCount = 0xAC, kFRacers = 0xB0, kFCopCount = 0x40, kFCops = 0x44, kFMedals = 0x9C;
static const uint32_t kFRaceBal = 0xD4, kFProfile = 0xD8, kFShortcuts = 0x118, kFCopBal = 0x34, kFHpCopBal = 0x78;

static uint8_t* CurrentEventData(uint32_t* idOut) {
    uint32_t root = 0;
    if (!Readable((void*)kGameRootPtr, 4) || !(root = *(uint32_t*)kGameRootPtr)) return nullptr;
    if (!Readable((void*)(root + kOffChallengeRes), 8)) return nullptr;
    uint32_t res = *(uint32_t*)(root + kOffChallengeRes), off = *(uint32_t*)(root + kOffChallengeOff);
    if (!res || !Readable((void*)(res + 0x28), 4)) return nullptr;
    uint32_t base = *(uint32_t*)(res + 0x28);
    uint8_t* P = (uint8_t*)(base + off);
    if (!base || !Readable(P, 0x20)) return nullptr;
    uint32_t dp = *(uint32_t*)(P + 8), h = *(uint32_t*)(P + 0xC), id = *(uint32_t*)(P + 0x10);
    if (h != kHashEvent || dp != (uint32_t)(P + 0x20) || !Readable((void*)dp, 0x140)) return nullptr;
    if (idOut) *idOut = id;
    return (uint8_t*)dp;
}

// The event's record in the loaded CHALLENGEINSTANCES data, found by its header (data = P+0x20, hash, id) before
// the game starts loading (the counts must be in place by then; RequestChallengeMode is too late).
static uint32_t g_evRecCache[2];  // id, address
static uint8_t* FindEventRecord(uint32_t id) {
    auto ok = [&](uint32_t P) {
        uint32_t h[3];
        SIZE_T n = 0;
        return ReadProcessMemory(GetCurrentProcess(), (void*)(P + 8), h, 12, &n) && n == 12 && h[0] == P + 0x20 &&
               h[1] == kHashEvent && h[2] == id && Readable((void*)(P + 0x20), 0x140);
    };
    if (g_evRecCache[0] == id && ok(g_evRecCache[1])) return (uint8_t*)(g_evRecCache[1] + 0x20);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint8_t* a = (uint8_t*)si.lpMinimumApplicationAddress;
    static uint8_t buf[1 << 20];
    DWORD t0 = GetTickCount();
    uint32_t found = 0;
    while (!found && a < (uint8_t*)si.lpMaximumApplicationAddress) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(a, &mi, sizeof mi)) break;
        uint8_t* next = (uint8_t*)mi.BaseAddress + mi.RegionSize;
        if (mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) && !(mi.Protect & PAGE_GUARD) &&
            mi.Type == MEM_PRIVATE) {
            for (uint8_t* p = (uint8_t*)mi.BaseAddress; p < next && !found; p += sizeof buf - 16) {
                size_t len = (size_t)(next - p) < sizeof buf ? (size_t)(next - p) : sizeof buf;
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), p, buf, len, &got) || got < 12) continue;
                for (size_t i = 0; i + 8 <= got; i += 4)
                    if (*(uint32_t*)(buf + i) == kHashEvent && *(uint32_t*)(buf + i + 4) == id) {
                        uint32_t P = (uint32_t)(p + i) - 0xC;
                        if (ok(P)) { found = P; break; }
                    }
                if (len < sizeof buf) break;
            }
        }
        a = next;
    }
    Log("event %u record %s (%08X) in %lu ms", id, found ? "found" : "NOT found", found, GetTickCount() - t0);
    if (!found) return nullptr;
    g_evRecCache[0] = id;
    g_evRecCache[1] = found;
    return (uint8_t*)(found + 0x20);
}

struct ListCap { uint32_t id, racers, cops; };
static ListCap g_caps[512];
static int g_nCaps;
static int g_eventRacers, g_eventCops;     // AI counts of the running Single Race event

static int ValidOpps(uint8_t* d, uint32_t f, uint32_t cap, uint32_t hash) {
    uint32_t ap = *(uint32_t*)(d + f);
    int n = 0;
    for (uint32_t k = 0; k < cap && k < 8; ++k) {
        uint32_t p = 0;
        if (!Readable((void*)(ap + 4 * k), 4) || !(p = *(uint32_t*)(ap + 4 * k)) || !Readable((void*)p, 0x3C)) break;
        if (*(uint32_t*)(p + 0xC) != hash || *(uint32_t*)(p + 8) != p + 0x20) break;
        ++n;
    }
    return n;
}

static void WriteConditions(const Settings& s, const char* why);
static void ApplyEventSettings(const char* where) {
    LONG want = g_srEventId;
    if (!want || g_appliedEventId == want) return;
    uint32_t id = 0;
    uint8_t* d = nullptr;
    bool atLaunch = !strcmp(where, "launch");
    if (atLaunch) {
        d = FindEventRecord((uint32_t)want);
        id = (uint32_t)want;
        if (!d) { Log("event settings (launch): event %ld not found in memory - trying again at loading", (long)want); return; }
    } else {
        d = CurrentEventData(&id);
        if (!d) { Log("event settings (%s): no current event record yet", where); return; }
        if (id != (uint32_t)want) { Log("event settings (%s): current event is %u, not %ld - not yet", where, id, (long)want); return; }
        Log("event settings (%s): written at loading only - the AI count may come too late", where);
    }
    LoadEventSettings();
    const Settings& s = g_set;
    LONG m = g_srMode;
    if (atLaunch) WriteConditions(s, "launch");
    // list capacities: as the event record had them first (the counts below change the list lengths)
    ListCap* c = nullptr;
    for (int i = 0; i < g_nCaps; ++i)
        if (g_caps[i].id == id) c = &g_caps[i];
    if (!c && g_nCaps < 512) {
        c = &g_caps[g_nCaps++];
        c->id = id;
        c->racers = *(uint32_t*)(d + kFRacers + 4);
        c->cops = *(uint32_t*)(d + kFCops + 4);
    }
    const uint32_t kHashOpp = 0x1987C43D;
    int roomR = c ? ValidOpps(d, kFRacers, c->racers, kHashOpp) : 0;
    int roomC = c ? ValidOpps(d, kFCops, c->cops, kHashOpp) : 0;
    int racers = m == kRace ? s.raceRacers : m == kHotPursuit ? s.hpRacers : s.copRacers;
    int cops = m == kHotPursuit ? s.hpCops : 0;
    if (racers > roomR) { Log("AI count: event %u has room for %d racers only (limited)", id, roomR); racers = roomR; }
    if (cops > roomC) { Log("AI count: event %u has room for %d cops only (limited)", id, roomC); cops = roomC; }
    if (racers < 1) racers = roomR < 1 ? 0 : 1;
    *(uint32_t*)(d + kFRacerCount) = (uint32_t)racers;
    *(uint32_t*)(d + kFRacers + 4) = (uint32_t)racers;
    if (m == kHotPursuit) {
        *(uint32_t*)(d + kFCopCount) = (uint32_t)cops;
        *(uint32_t*)(d + kFCops + 4) = (uint32_t)cops;
    }
    g_eventRacers = racers;
    g_eventCops = cops;
    Log("AI count: event %u: %d racers%s (room %d / %d)", id, racers,
        m == kHotPursuit ? (" + " + std::to_string(cops) + " cops").c_str() : "", roomR, roomC);
    // difficulty: balancing ids (read by SetupRaceBalancing at "Post Load")
    int df = s.difficulty;
    uint32_t bal[5];
    const char* src;
    if (m == kCopHotPursuit) {
        const CopPreset& p = kCopPresets[df];
        uint32_t b[5] = {0, 0, p.shortcuts, 0, p.hpCops};
        memcpy(bal, b, sizeof b);
        src = p.source;
    } else if (m == kHotPursuit) {
        const HpPreset& p = kHpPresets[df];
        uint32_t b[5] = {p.race, p.profile, p.shortcuts, p.cops, 0};
        memcpy(bal, b, sizeof b);
        src = p.source;
    } else {
        const RacePreset& p = kRacePresets[df];
        uint32_t b[5] = {p.race, p.profile, p.shortcuts, p.cops, 0};
        memcpy(bal, b, sizeof b);
        src = p.source;
    }
    const uint32_t fo[5] = {kFRaceBal, kFProfile, kFShortcuts, kFCopBal, kFHpCopBal};
    uint32_t was[5];
    for (int i = 0; i < 5; ++i) { was[i] = *(uint32_t*)(d + fo[i]); *(uint32_t*)(d + fo[i]) = bal[i]; }
    Log("difficulty %s (%s): balancing %u / profile %u / shortcuts %u / cops %u / hp cops %u (event had %u / %u / %u / %u / %u)",
        kDifficultyNames[df], src, bal[0], bal[1], bal[2], bal[3], bal[4], was[0], was[1], was[2], was[3], was[4]);
    // cop Hot Pursuit medals: busts needed = racers / racers-1 / racers-2 (at least 1)
    if (m == kCopHotPursuit) {
        uint32_t ap = *(uint32_t*)(d + kFMedals), cnt = *(uint32_t*)(d + kFMedals + 4);
        int done = 0;
        for (uint32_t k = 0; k < cnt && k < 3; ++k) {
            uint32_t p = Readable((void*)(ap + 4 * k), 4) ? *(uint32_t*)(ap + 4 * k) : 0;
            if (!p || !Readable((void*)p, 0x38) || *(uint32_t*)(p + 0xC) != 0x63D64900) continue;
            int busts = racers - (int)k;
            *(uint32_t*)(*(uint32_t*)(p + 8) + 4) = (uint32_t)(busts < 1 ? 1 : busts);
            ++done;
        }
        Log("medals: event %u: busts needed %d / %d / %d (gold / silver / bronze, %d racers)%s", id, racers,
            racers - 1 < 1 ? 1 : racers - 1, racers - 2 < 1 ? 1 : racers - 2, racers, done ? "" : " - no medal list");
    }
    InterlockedExchange(&g_appliedEventId, want);
}

static lua_CFunction g_origRequestChallengeMode, g_origGetChallengeParam, g_origGetOpponentToughness,
    g_origSetTimeOfDay, g_origWeaponsForCompetitor;

static int __cdecl Hook_RequestChallengeMode(lua_State* L) {
    ApplyEventSettings("RequestChallengeMode");
    if (g_srEventId && g_appliedEventId == g_srEventId) {
        uint32_t id = 0;
        uint8_t* d = CurrentEventData(&id);
        if (d && id == (uint32_t)g_srEventId)
            Log("loading event %u: record %08X, racers %u (list %u), cops %u (list %u)", id, (uint32_t)(d - 0x20),
                *(uint32_t*)(d + kFRacerCount), *(uint32_t*)(d + kFRacers + 4), *(uint32_t*)(d + kFCopCount),
                *(uint32_t*)(d + kFCops + 4));
    }
    return g_origRequestChallengeMode(L);
}

// GetChallengeParam(name): player toughness and cop respawn from the difficulty preset
static int __cdecl Hook_GetChallengeParam(lua_State* L) {
    char key[48] = {};
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
        size_t len = 0;
        const char* k = lua_tolstring(L, 1, &len);
        if (k) memcpy(key, k, len < sizeof key - 1 ? len : sizeof key - 1);
    }
    int n = g_origGetChallengeParam(L);
    if (!E.singleRace || n != 1 || !key[0]) return n;
    LONG m = g_srMode;
    int df = g_set.difficulty, t = lua_type(L, -1);
    double was = t == LUA_TNUMBER ? lua_tonumber(L, -1) : 0;
    auto put = [&](double v) { lua_settop(L, -2); lua_pushnumber(L, v); };
    if (m == kCopHotPursuit && !strcmp(key, "LocalPlayerCopToughnessScale") && t == LUA_TNUMBER) {
        put(kCopPresets[df].playerCopScale);
        Log("difficulty %s: LocalPlayerCopToughnessScale %.2f (event %.2f)", kDifficultyNames[df], kCopPresets[df].playerCopScale, was);
    } else if (m != kCopHotPursuit && !strcmp(key, "LocalPlayerToughnessScale") && t == LUA_TNUMBER) {
        float v = m == kHotPursuit ? kHpPresets[df].playerScale : kRacePresets[df].playerScale;
        put(v);
        Log("difficulty %s: LocalPlayerToughnessScale %.2f (event %.2f)", kDifficultyNames[df], v, was);
    } else if (m == kHotPursuit && !strcmp(key, "AICopRespawnAfterWreckTime") && t == LUA_TNUMBER) {
        double secs = g_set.copRespawn >= 0 ? g_set.copRespawn : kHpPresets[df].respawn;
        put(secs);
        Log("difficulty %s: AICopRespawnAfterWreckTime %.0f s (%s; event %.0f)", kDifficultyNames[df], secs,
            g_set.copRespawn >= 0 ? "ini CopRespawnTime" : "difficulty default", was);
    } else if (m == kHotPursuit && !strcmp(key, "UseDelayCopRespawn") && (t == 1 || t == LUA_TNUMBER)) {
        lua_settop(L, -2);
        if (t == 1) lua_pushboolean(L, 1);
        else lua_pushnumber(L, 1.0);
        Log("difficulty %s: UseDelayCopRespawn true", kDifficultyNames[df]);
    }
    return n;
}

// GetOpponentToughness(competitor): Hot Pursuit AI racers / cops and cop-side AI racers from the preset
static int __cdecl Hook_GetOpponentToughness(lua_State* L) {
    int comp = (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TNUMBER) ? (int)lua_tonumber(L, 1) : -1;
    int n = g_origGetOpponentToughness(L);
    LONG m = g_srMode;
    if (!E.singleRace || n != 1 || lua_type(L, -1) != LUA_TNUMBER || comp < 1 || comp > 7 || m == kRace) return n;
    int df = g_set.difficulty;
    float v;
    const char* what;
    if (m == kCopHotPursuit) {
        if (comp > g_eventRacers) return n;
        v = kCopPresets[df].racerTough[comp - 1];
        what = "AI racer";
    } else {
        if (comp > g_eventRacers + g_eventCops) return n;
        int cop = comp - g_eventRacers - 1;
        v = cop < 0 ? kHpPresets[df].racerTough : kHpPresets[df].copTough[cop < 6 ? cop : 5];
        what = cop < 0 ? "AI racer" : "cop";
    }
    double was = lua_tonumber(L, -1);
    lua_settop(L, -2);
    lua_pushnumber(L, v);
    Log("difficulty %s: competitor %d (%s) toughness %.2f (event %.2f)", kDifficultyNames[df], comp, what, v, was);
    return n;
}

// ---- time of day and weather (Remastered / NFSHPR Event Randomizer logic)
static uint8_t* ArgSlot(lua_State* L, int i) { return (uint8_t*)(*(uint32_t*)((uint8_t*)L + 0xC)) + (size_t)(i - 1) * 16; }
static int SlotType(uint8_t* t) { return *(int*)(t + 8); }
static void SetFlagArg(lua_State* L, int i, bool v) {
    if (lua_gettop(L) < i) return;
    uint8_t* t = ArgSlot(L, i);
    switch (SlotType(t)) {
        case LUA_TNUMBER: *(double*)t = v ? 1.0 : 0.0; break;
        case LUA_TSTRING: {
            const char* str = lua_tolstring(L, i, nullptr);
            const char* w = v ? "1" : "0";
            if (str && (str[0] == 't' || str[0] == 'f')) w = v ? "true" : "false";
            lua_pushstring(L, w);
            lua_replace(L, i);
            break;
        }
        default:
            *(int*)t = v ? 1 : 0;
            *(int*)(t + 8) = 1;  // LUA_TBOOLEAN
            break;
    }
}
static double WrapHour(double h) {
    while (h >= 24.0) h -= 24.0;
    while (h < 0.0) h += 24.0;
    return h;
}
// SetTimeOfDay(start, finish, cycle, isWet, isRain, isThunder, isAlternativeWeather, overrideSun, sunDir)
static int __cdecl Hook_SetTimeOfDay(lua_State* L) {
    if (!E.singleRace) return g_origSetTimeOfDay(L);
    const Settings& s = g_set;
    int top = lua_gettop(L);
    double oldStart = 0, oldFinish = 0;
    if (top >= 2 && SlotType(ArgSlot(L, 1)) == LUA_TNUMBER) {
        double start = oldStart = *(double*)ArgSlot(L, 1);
        bool fin = SlotType(ArgSlot(L, 2)) == LUA_TNUMBER;
        if (fin) oldFinish = *(double*)ArgSlot(L, 2);
        double span = fin ? oldFinish - start : 0.0;
        double ns = WrapHour(kTimeHours[s.time]);
        *(double*)ArgSlot(L, 1) = ns;
        if (!s.timeProgression) span = 0.001;
        if (fin) *(double*)ArgSlot(L, 2) = WrapHour(ns + span);
        if (top >= 8) SetFlagArg(L, 8, false);  // drop the event's fixed sun direction
    }
    if (top >= 6) {
        SetFlagArg(L, 4, s.weather != kDry);
        SetFlagArg(L, 5, s.weather == kRain || s.weather == kStorm);
        SetFlagArg(L, 6, s.weather == kStorm);
    }
    Log("conditions: %s, %s, time progression %s (event %.2f -> %.2f h)", kTimeNames[s.time], kWeatherLabels[s.weather],
        s.timeProgression ? "on" : "off", oldStart, oldFinish);
    return g_origSetTimeOfDay(L);
}

// ---- weapons: the Single Race sets 2300100 (AI) / 2300101 (player) get the charges and the level
static const uint32_t kHashWeaponSet = 0x82D52FAF, kHashWeapon = 0x5D264E2B, kSetAI = 2300100, kSetPlayer = 2300101;
// HP2010 weapon set data: 8 Weapon pointers, CopWeaponLevel (ea93e8f7) +0x08, RacerWeaponLevel (47273606) +0x20
static const uint32_t kWeaponFieldOffset[kWeaponCount] = {0x1C /*RacerSpikeStrip*/, 0x18 /*RacerEMP*/, 0x10 /*Jammer*/,
    0x14 /*Overdrive*/, 0x04 /*CopSpikeStrip*/, 0x00 /*CopEMP*/, 0x24 /*RoadBlock*/, 0x0C /*Helicopter*/};
static const uint32_t kOffCopWeaponLevel = 0x08, kOffRacerWeaponLevel = 0x20;
static uint32_t g_setAddr[2];   // instance addresses found by the scan (AI, player)

static bool IsSet(uint32_t P, uint32_t id) {
    uint32_t h[3];
    SIZE_T n = 0;
    return ReadProcessMemory(GetCurrentProcess(), (void*)(P + 8), h, 12, &n) && n == 12 && h[0] == P + 0x20 &&
           h[1] == kHashWeaponSet && h[2] == id;
}

// Finds both set instances in committed read-write memory (signature: hash, id at P+0xC).
static void ScanSets() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint8_t* a = (uint8_t*)si.lpMinimumApplicationAddress;
    static uint8_t buf[1 << 20];
    int found = 0;
    for (int k = 0; k < 2; ++k) if (g_setAddr[k] && IsSet(g_setAddr[k], k ? kSetPlayer : kSetAI)) ++found; else g_setAddr[k] = 0;
    DWORD t0 = GetTickCount();
    while (found < 2 && a < (uint8_t*)si.lpMaximumApplicationAddress) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(a, &mi, sizeof mi)) break;
        uint8_t* next = (uint8_t*)mi.BaseAddress + mi.RegionSize;
        if (mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) && !(mi.Protect & PAGE_GUARD) &&
            mi.Type == MEM_PRIVATE) {
            for (uint8_t* p = (uint8_t*)mi.BaseAddress; p < next; p += sizeof buf - 16) {
                size_t len = (size_t)(next - p) < sizeof buf ? (size_t)(next - p) : sizeof buf;
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), p, buf, len, &got) || got < 12) continue;
                for (size_t i = 0; i + 8 <= got; i += 4) {
                    if (*(uint32_t*)(buf + i) != kHashWeaponSet) continue;
                    uint32_t id = *(uint32_t*)(buf + i + 4);
                    int k = id == kSetAI ? 0 : id == kSetPlayer ? 1 : -1;
                    if (k < 0 || g_setAddr[k]) continue;
                    uint32_t P = (uint32_t)(p + i) - 0xC;
                    if (IsSet(P, id)) { g_setAddr[k] = P; ++found; }
                }
                if (len < sizeof buf) break;
            }
        }
        a = next;
    }
    Log("weapons: sets %s (AI %08X, player %08X) in %lu ms", found == 2 ? "found" : "NOT all found", g_setAddr[0],
        g_setAddr[1], GetTickCount() - t0);
}

static bool WriteCharges(uint32_t set, const int* charges, int level) {
    uint32_t data = *(uint32_t*)(set + 8);
    uint32_t subs[kWeaponCount];
    for (int w = 0; w < kWeaponCount; ++w) {
        subs[w] = *(uint32_t*)(data + kWeaponFieldOffset[w]);
        if (!subs[w] || !Readable((void*)subs[w], 0x30) || *(uint32_t*)(subs[w] + 0xC) != kHashWeapon) return false;
    }
    for (int w = 0; w < kWeaponCount; ++w) *(uint32_t*)(*(uint32_t*)(subs[w] + 8)) = (uint32_t)charges[w];  // Count
    *(uint32_t*)(data + kOffCopWeaponLevel) = (uint32_t)level;
    *(uint32_t*)(data + kOffRacerWeaponLevel) = (uint32_t)level;
    return true;
}

static volatile LONG g_weaponsWrittenFor;
static int __cdecl Hook_WeaponsForCompetitor(lua_State* L) {
    LONG m = g_srMode, ev = g_srEventId;
    if (m == kHotPursuit || m == kCopHotPursuit) {
        if (InterlockedExchange(&g_weaponsWrittenFor, ev) != ev) {
            LoadEventSettings();
            const Settings& s = g_set;
            const int* charges = s.weaponsMode == kWeaponsCustom ? s.charges : kClassicCharges;
            bool copSide = m == kCopHotPursuit;
            int own[kWeaponCount];
            for (int w = 0; w < kWeaponCount; ++w) own[w] = (w >= 4) == copSide ? charges[w] : 0;
            ScanSets();
            bool okA = g_setAddr[0] && WriteCharges(g_setAddr[0], charges, s.weaponLevel);
            bool okP = g_setAddr[1] && WriteCharges(g_setAddr[1], own, s.weaponLevel);
            Log("weapons (%s, level %d): racers spike %d emp %d jammer %d turbo %d | cops spike %d emp %d roadblock %d heli %d"
                " | player (%s side) gets its own side only%s%s",
                s.weaponsMode == kWeaponsCustom ? "Custom" : "Classic", s.weaponLevel, charges[0], charges[1], charges[2],
                charges[3], charges[4], charges[5], charges[6], charges[7], copSide ? "cop" : "racer",
                okA ? "" : " - AI SET NOT WRITTEN", okP ? "" : " - PLAYER SET NOT WRITTEN");
        }
    }
    return g_origWeaponsForCompetitor(L);
}

// ===================================================================== Race Settings menu (SR_Overlay9.cpp)
static volatile LONG g_layoutSet;          // last SwitchLayoutSet
static volatile LONG g_offlinePageIsWall;  // last OverrideDefaultPageForContext("offline_menu", page) was "wall"
static const LONG kLayoutLocations = 646187, kLayoutEvents = 646188;
static lua_CFunction g_origSwitchLayoutSet, g_origOverrideDefaultPage;
static const NativeReg kSwitchLayoutSet = {"SwitchLayoutSet", 0x79C360, 0x7A09FE};
static const NativeReg kOverrideDefaultPage = {"OverrideDefaultPageForContext", 0x79BA90, 0x7A057A};

static void LogPickerChange() {
    static int last = -1;
    int v = SR_PickerVisible();
    if (v != last) { last = v; Log("picker %s (layout %ld, wall %ld)", v ? "shown" : "hidden", (long)g_layoutSet, (long)g_offlinePageIsWall); }
}
static int __cdecl Hook_SwitchLayoutSet(lua_State* L) {
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TNUMBER) {
        InterlockedExchange(&g_layoutSet, (LONG)lua_tonumber(L, 1));
        LogPickerChange();
    }
    return g_origSwitchLayoutSet(L);
}
static int __cdecl Hook_OverrideDefaultPage(lua_State* L) {
    if (lua_gettop(L) >= 2 && lua_type(L, 1) == LUA_TSTRING && lua_type(L, 2) == LUA_TSTRING) {
        const char* ctx = lua_tolstring(L, 1, nullptr);
        const char* page = lua_tolstring(L, 2, nullptr);
        if (ctx && page && !strcmp(ctx, "offline_menu")) {
            InterlockedExchange(&g_offlinePageIsWall, !strcmp(page, "wall"));
            LogPickerChange();
        }
    }
    return g_origOverrideDefaultPage(L);
}

bool SR_PickerVisible() {
    LONG l = g_layoutSet;
    return g_offlinePageIsWall && (l == kLayoutLocations || l == kLayoutEvents);
}

void SR_Log(const char* fmt, ...) {
    char buf[900];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    Log("%s", buf);
}

static int MenuKeyFromIni() {
    std::string t = IniString("Overlay", "Key", "F5");
    if ((t[0] == 'F' || t[0] == 'f') && t.size() > 1) {
        int n = atoi(t.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    int v = atoi(t.c_str());
    return v > 0 && v < 256 ? v : VK_F5;
}

sr::Conditions SR_GetConditions() {
    Settings s = ReadEventSettings();
    sr::Conditions c = {};
    c.eventMode = (int)ReadMode();
    c.time = s.time;
    c.weather = s.weather;
    c.timeProgression = s.timeProgression;
    c.difficulty = s.difficulty;
    c.raceRacers = s.raceRacers;
    c.hpRacers = s.hpRacers;
    c.hpCops = s.hpCops;
    c.copRacers = s.copRacers;
    c.weaponsMode = s.weaponsMode;
    for (int w = 0; w < kWeaponCount; ++w) c.charges[w] = s.charges[w];
    c.weaponLevel = s.weaponLevel;
    c.menuKey = MenuKeyFromIni();
    c.fontSize = IniInt("Overlay", "FontSize", 0);
    return c;
}

void SR_SetConditions(const sr::Conditions& c) {
    auto put = [](const char* sec, const char* key, const char* v) { WritePrivateProfileStringA(sec, key, v, g_ini); };
    auto num = [&](const char* sec, const char* key, int v) { put(sec, key, std::to_string(v).c_str()); };
    put("Event", "Mode", kModeIni[Clamp(c.eventMode, 0, 2)]);
    put("Event", "Difficulty", kDifficultyNames[Clamp(c.difficulty, 0, kDifficultyCount - 1)]);
    put("Conditions", "TimeOfDay", kTimeNames[Clamp(c.time, 0, kTimeCount - 1)]);
    put("Conditions", "Weather", kWeatherNames[Clamp(c.weather, 0, kWeatherCount - 1)]);
    put("Conditions", "TimeProgression", c.timeProgression ? "On" : "Off");
    num("Race", "Racers", c.raceRacers);
    num("HotPursuit", "Racers", c.hpRacers);
    num("HotPursuit", "Cops", c.hpCops);
    num("CopHotPursuit", "Racers", c.copRacers);
    put("Weapons", "Mode", c.weaponsMode == kWeaponsCustom ? "Custom" : "Classic");
    num("Weapons", "Level", c.weaponLevel);
    for (int w = 0; w < kWeaponCount; ++w) num("Weapons", kWeaponKeys[w], c.charges[w]);
    // the game thread re-reads the ini at the next launch
}

#include "conditions.inc"

// ResolveStringBinding("SR.<name>"): the picker's Lua (sr_info.lua) asks the helper; anything else goes to the game.
static lua_CFunction g_origResolveString;
static const NativeReg kResolveString = {"ResolveStringBinding", 0x79BF60, 0x7A06F0};
static int __cdecl Hook_ResolveString(lua_State* L) {
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
        size_t len = 0;
        const char* k = lua_tolstring(L, 1, &len);
        if (k && len > 3 && !memcmp(k, "SR.", 3)) {
            if (!strcmp(k, "SR.Summary")) {
                RefreshPickerSettings(false);
                lua_settop(L, 0);
                lua_pushstring(L, g_summary);
                return 1;
            }
            if (!strcmp(k, "SR.OpenSettings")) {
                SR_RequestMenu();
                lua_pushstring(L, "1");
                return 1;
            }
            lua_settop(L, 0);
            lua_pushnil(L);
            return 1;
        }
    }
    return g_origResolveString(L);
}

static void InstallMenu() {
    EnsureKey("Overlay", "Enabled", "1");
    EnsureKey("Overlay", "Key", "F5");
    EnsureKey("Overlay", "FontSize", "0");
    if (PatchSiteChained(kResolveString, (uint32_t)&Hook_ResolveString, &g_origResolveString))
        Log("ResolveStringBinding hooked (SR.* for the picker's Lua)");
    bool a = PatchSiteChained(kSwitchLayoutSet, (uint32_t)&Hook_SwitchLayoutSet, &g_origSwitchLayoutSet);
    bool b = PatchSiteChained(kOverrideDefaultPage, (uint32_t)&Hook_OverrideDefaultPage, &g_origOverrideDefaultPage);
    if (!a || !b) { Log("Race Settings menu: picker detection not hooked - menu disabled"); return; }
    if (!IniInt("Overlay", "Enabled", 1)) { Log("Race Settings menu: disabled in the ini ([Overlay] Enabled=0)"); return; }
    Overlay_Start();
}
