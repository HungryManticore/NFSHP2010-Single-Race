
// ===================================================================== opponents picked at the event start (11)
// Port of the Remastered helper v39. SetAIRacerName (the "{racer} FINISHED 1ST" / busted / escaped name) and the
// other natives read an opponent's car from the event record's OpponentData (instance 1987c43d, data +0x14 = vehicle
// id, +0 = paint), not from GetOpponentVehicleId's Lua result. So the picks are written into the record at
// StopTraffic (after car select and loading, before the cars are placed and named); GetOpponentVehicleId then
// answers with the record's car and only the colour randomizer keys off it. The records stay in memory between
// launches, so each entry's bundle car is remembered (g_originalCars) and is the base of every pick (and is written
// back when randomizing is off).
struct OrigCar { uint32_t inst, vehicle; };
static OrigCar g_originalCars[1024];
static int g_nOriginalCars;
// g_prePicked: declared before Hook_StopTraffic

static uint32_t* OppVehicleSlot(uint32_t inst) {
    if (!Readable((void*)inst, 0x40) || *(uint32_t*)(inst + 0xC) != 0x1987C43D || *(uint32_t*)(inst + 8) != inst + 0x20)
        return nullptr;
    return (uint32_t*)(inst + 0x20 + 0x14);
}

static uint32_t OriginalCarOf(uint32_t inst, uint32_t current) {
    for (int i = 0; i < g_nOriginalCars; ++i)
        if (g_originalCars[i].inst == inst) return g_originalCars[i].vehicle;
    if (g_nOriginalCars < 1024) g_originalCars[g_nOriginalCars++] = {inst, current};
    return current;
}

// The selected car from the game's state (no Lua): M = [root]+0x264C8; id = [M+0x14D0 + 4*[[M+0x1514]+0x5F04]].
static void LearnPlayerCarNoLua() {
    if (E.playerKnown) return;
    uint32_t root = Readable((void*)kGameRootPtr, 4) ? *(uint32_t*)kGameRootPtr : 0;
    if (!root) return;
    uint32_t M = root + 0x264C8;
    if (!Readable((void*)(M + 0x1514), 4)) return;
    uint32_t q = *(uint32_t*)(M + 0x1514);
    if (!q || !Readable((void*)(q + 0x5F04), 4)) return;
    uint32_t k = *(uint32_t*)(q + 0x5F04);
    if (k > 255 || !Readable((void*)(M + 0x14D0 + 4 * k), 4)) return;
    uint32_t id = *(uint32_t*)(M + 0x14D0 + 4 * k);
    const Vehicle* v = FindVehicle(id);
    if (!v) return;
    E.playerId = id;
    E.playerKnown = true;
    Log("player car: %u %s [%s %s]", id, VName(v), v->side == 3 ? "cop" : "racer", ClassName(v));
}

static void PickOpponentsAtStart(lua_State* L) {
    g_prePicked = false;
    uint32_t id = 0;
    uint8_t* d = CurrentEventData(&id);
    if (!d || id != (uint32_t)g_srEventId) {
        uint8_t* f = g_srEventId ? FindEventRecord((uint32_t)g_srEventId) : nullptr;
        if (f) { d = f; id = (uint32_t)g_srEventId; }
    }
    if (!d) { Log("opponents: event record not found - cars picked at spawn (names stay the bundle's)"); return; }
    LearnPlayerCarNoLua();
    if (!E.playerKnown && L) {  // Lua fallback, the stack restored
        int top = lua_gettop(L);
        int n = g_GetSelectedVehicleId(L);
        if (n >= 1 && lua_type(L, -1) == LUA_TNUMBER) {
            E.playerId = (uint32_t)lua_tonumber(L, -1);
            E.playerKnown = true;
            const Vehicle* v = FindVehicle(E.playerId);
            Log("player car: %u %s (Lua)", E.playerId, VName(v));
        }
        lua_settop(L, top);
    }
    static const char* kList[] = {"racer", "cop"};
    const uint32_t lists[2] = {kFRacers, kFCops};
    int written = 0;
    for (int li = 0; li < 2; ++li) {
        uint32_t ap = *(uint32_t*)(d + lists[li]), cnt = *(uint32_t*)(d + lists[li] + 4);
        for (uint32_t k = 0; k < cnt && k < 16; ++k) {
            if (!Readable((void*)(ap + 4 * k), 4)) break;
            uint32_t inst = *(uint32_t*)(ap + 4 * k);
            uint32_t* slot = OppVehicleSlot(inst);
            if (!slot) break;
            uint32_t orig = OriginalCarOf(inst, *slot);
            uint32_t chosen = PickReplacement(orig);
            if (E.playerKnown && chosen == E.playerId && chosen != orig) {
                for (int t = 0; t < 8 && chosen == E.playerId; ++t) chosen = PickReplacement(orig);
                if (chosen == E.playerId) chosen = orig;
            }
            *slot = chosen;
            if (E.slotCount < 64) E.slots[E.slotCount++] = Slot{-100 - (int)written, orig, chosen, 0};  // for AvoidDuplicates
            ++written;
            const Vehicle* o = FindVehicle(orig);
            const Vehicle* c = FindVehicle(chosen);
            Log("opponents: %s %u: %u %s [%s] -> %u %s [%s]", kList[li], k, orig, VName(o), ClassName(o), chosen, VName(c),
                ClassName(c));
        }
    }
    g_prePicked = written > 0;
    Log("opponents: %d cars written into event %u's record", written, id);
}
