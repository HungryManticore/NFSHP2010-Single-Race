
static uint32_t VmEventId(uint8_t* vm, uint32_t handleOff, uint8_t** data);

// ===================================================================== intro name tags and the loading screen's class
// Intro tags: the competitor info VM formats "{name} ({rank})" (string 260775, kept at [0xE36CD4]) into its FullRank
// (vm+0x100 -> vm+0xA8) when the rank changes: NFS11.exe 0x583D62 `mov ecx, [0xE36CD4]`. That load is pointed at
// g_tagStringId: an empty string (148253) during Single Race events, 260775 otherwise.
static const uint32_t kTagLoad = 0x583D62, kTagStringVar = 0xE36CD4, kTagString = 260775, kEmptyString = 148253;
static volatile LONG g_tagStringId = (LONG)kTagString;
static bool g_tagPatched;

static bool InstallTagPatch() {
    uint8_t* p = (uint8_t*)kTagLoad;
    if (p[0] != 0x8B || p[1] != 0x0D || *(uint32_t*)(p + 2) != kTagStringVar) { Log("intro tags: code does not match - not hooked"); return false; }
    DWORD old;
    VirtualProtect(p + 2, 4, PAGE_EXECUTE_READWRITE, &old);
    *(uint32_t*)(p + 2) = (uint32_t)&g_tagStringId;
    VirtualProtect(p + 2, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
    g_tagPatched = true;
    return true;
}
static void SetIntroTags(bool singleRace) {
    if (!g_tagPatched) return;
    LONG want = singleRace ? (LONG)kEmptyString : (LONG)kTagString;
    if (InterlockedExchange(&g_tagStringId, want) != want)
        Log("intro tags: %s", singleRace ? "hidden (Single Race event)" : "shown again");
}

// Loading screen: "{ModeName} - {TierName}" of the ChallengeViewModel. SetChallenge sets TierName (vm+0x68, string id)
// from the career tier that lists the event (0x5666F9); Single Race events are in none. A hook right after it
// (0x5666FC `mov eax, [esi+0x724]`, 6 bytes) writes the class of the player's selected car for Single Race events.
static const uint32_t kTierSite = 0x5666FC;
static const uint32_t kRacerTierStrings[5] = {559305, 559307, 559309, 559311, 559313};  // SPORTS .. HYPER
static const uint32_t kCopTierStrings[5] = {559286, 559288, 559297, 559299, 559302};    // TRAFFIC POLICE .. SPECIAL RESPONSE
extern "C" uint32_t g_tierRet;
uint32_t g_tierRet = kTierSite + 6;

extern "C" void __cdecl OnTierSet(uint8_t* vm) {
    static int calls;
    if (calls < 5) { ++calls; Log("loading screen: SetChallenge tier write (vm %08X, event %u)", (uint32_t)vm, VmEventId(vm, 0x50, nullptr)); }
    if (!Readable(vm + 0x50, 8)) return;
    uint32_t res = *(uint32_t*)(vm + 0x50), off = *(uint32_t*)(vm + 0x54);
    if (!res || !Readable((void*)(res + 0x28), 4)) return;
    uint8_t* P = (uint8_t*)(*(uint32_t*)(res + 0x28) + off);
    if (!Readable(P, 0x14) || *(uint32_t*)(P + 0xC) != kHashEvent) return;
    uint32_t id = *(uint32_t*)(P + 0x10);
    if (id < kSrFirst || id > kSrLast || g_srMode < 0) return;
    uint32_t root = Readable((void*)kGameRootPtr, 4) ? *(uint32_t*)kGameRootPtr : 0;
    if (!root) return;
    uint8_t* M = (uint8_t*)(root + 0x264C8);
    if (!Readable(M + 0x1514, 4)) return;
    uint32_t pl = *(uint32_t*)(M + 0x1514);
    if (!pl || !Readable((void*)(pl + 0x5F04), 4)) return;
    uint32_t idx = *(uint32_t*)(pl + 0x5F04);
    if (idx > 15 || !Readable(M + 0x14D0 + 4 * idx, 4)) return;
    uint32_t vid = *(uint32_t*)(M + 0x14D0 + 4 * idx);
    const Vehicle* v = FindVehicle(vid);
    if (!v || v->tier < 1 || v->tier > 5) { Log("loading screen: event %u, car %u not in the table - class left blank", id, vid); return; }
    uint32_t sid = (v->side == 3 ? kCopTierStrings : kRacerTierStrings)[v->tier - 1];
    *(uint32_t*)(vm + 0x68) = sid;
    Log("loading screen: event %u, car %u %s -> class string %u", id, vid, v->name, sid);
}


// helper 9.1: the hook above is not reached for Single Race events, so the view model is also found in memory
// (constructed at gm-side object + 0x13C18 by 0x567860: [vm+0x714] = vm+0x508, [vm+0x508] = vm+0x510, [vm+0x724] = gm)
// and a thread writes TierName while a Single Race event is current.
static uint8_t* g_cvm;

static uint8_t* FindChallengeVM(uint32_t gm) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint8_t* a = (uint8_t*)si.lpMinimumApplicationAddress;
    static uint8_t buf[1 << 20];
    while (a < (uint8_t*)si.lpMaximumApplicationAddress) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(a, &mi, sizeof mi)) break;
        uint8_t* next = (uint8_t*)mi.BaseAddress + mi.RegionSize;
        if (mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) && !(mi.Protect & PAGE_GUARD) &&
            mi.Type == MEM_PRIVATE) {
            for (uint8_t* p = (uint8_t*)mi.BaseAddress; p < next; p += sizeof buf - 0x400) {
                size_t len = (size_t)(next - p) < sizeof buf ? (size_t)(next - p) : sizeof buf;
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), p, buf, len, &got) || got < 0x400) continue;
                for (size_t i = 0; i + 0x220 <= got; i += 4) {
                    uint32_t A = (uint32_t)(p + i);  // candidate vm+0x508
                    if (*(uint32_t*)(buf + i) != A + 8) continue;
                    if (*(uint32_t*)(buf + i + 0x20C) != A || *(uint32_t*)(buf + i + 0x21C) != gm) continue;
                    return (uint8_t*)(A - 0x508);
                }
                if (len < sizeof buf) break;
            }
        }
        a = next;
    }
    return nullptr;
}

static uint32_t VmEventId(uint8_t* vm, uint32_t handleOff, uint8_t** data) {
    if (!Readable(vm + handleOff, 8)) return 0;
    uint32_t res = *(uint32_t*)(vm + handleOff), off = *(uint32_t*)(vm + handleOff + 4);
    if (!res || !Readable((void*)(res + 0x28), 4)) return 0;
    uint8_t* P = (uint8_t*)(*(uint32_t*)(res + 0x28) + off);
    if (!Readable(P, 0x14) || *(uint32_t*)(P + 0xC) != kHashEvent) return 0;
    if (data) *data = (uint8_t*)*(uint32_t*)(P + 8);
    return *(uint32_t*)(P + 0x10);
}

static uint32_t SelectedVehicle() {
    uint32_t root = Readable((void*)kGameRootPtr, 4) ? *(uint32_t*)kGameRootPtr : 0;
    if (!root) return 0;
    uint8_t* M = (uint8_t*)(root + 0x264C8);
    if (!Readable(M + 0x1514, 4)) return 0;
    uint32_t pl = *(uint32_t*)(M + 0x1514);
    if (!pl || !Readable((void*)(pl + 0x5F04), 4)) return 0;
    uint32_t idx = *(uint32_t*)(pl + 0x5F04);
    if (idx > 15 || !Readable(M + 0x14D0 + 4 * idx, 4)) return 0;
    return *(uint32_t*)(M + 0x14D0 + 4 * idx);
}

static DWORD WINAPI TierThread(LPVOID) {
    uint32_t lastGm = 0;
    uint32_t lastKey[2] = {0, 0}, lastSeen[3] = {0, 0, 0};
    for (;;) {
        Sleep(50);
        uint32_t gm = Readable((void*)kGameRootPtr, 4) ? *(uint32_t*)kGameRootPtr : 0;
        if (!gm) continue;
        if (!g_cvm || gm != lastGm) {
            static DWORD nextTry;
            if (GetTickCount() < nextTry) continue;
            nextTry = GetTickCount() + 5000;
            DWORD t0 = GetTickCount();
            g_cvm = FindChallengeVM(gm);
            lastGm = gm;
            Log("loading screen: challenge view model %s (%08X, %lu ms)", g_cvm ? "found" : "not found yet", (uint32_t)g_cvm,
                GetTickCount() - t0);
            if (!g_cvm) continue;
        }
        if (!Readable(g_cvm, 0x730) || *(uint32_t*)(g_cvm + 0x724) != gm) { g_cvm = nullptr; continue; }
        uint8_t* data = nullptr;
        // the loading screen's event is the handle at +0x50 ("info"); +0x58 is unset for Single Race launches
        uint32_t id58 = VmEventId(g_cvm, 0x58, nullptr), id = VmEventId(g_cvm, 0x50, &data), id50 = id58;
        uint32_t tier = *(uint32_t*)(g_cvm + 0x68);
        if (id != lastSeen[0] || id50 != lastSeen[1] || tier != lastSeen[2]) {
            lastSeen[0] = id; lastSeen[1] = id50; lastSeen[2] = tier;
            float tod = data && Readable(data + 0x130, 4) ? *(float*)(data + 0x120) : -1.0f;
            Log("loading screen: view model event %u (+0x58: %u), mode string %u, class string %u, record time %.2f, icons %u / %u",
                id, id50, *(uint32_t*)(g_cvm + 0x64), tier, tod, data ? *(uint32_t*)(data + 0x12C) : 0, data ? *(uint32_t*)(data + 0x130) : 0);
        }
        if (id < kSrFirst || id > kSrLast || g_srMode < 0) continue;
        uint32_t vid = SelectedVehicle();
        const Vehicle* v = FindVehicle(vid);
        if (!v || v->tier < 1 || v->tier > 5) continue;
        uint32_t sid = (v->side == 3 ? kCopTierStrings : kRacerTierStrings)[v->tier - 1];
        if (tier != sid) {
            *(uint32_t*)(g_cvm + 0x68) = sid;
            if (lastKey[0] != id || lastKey[1] != vid) {
                lastKey[0] = id; lastKey[1] = vid;
                Log("loading screen: event %u, car %u %s -> class string %u", id, vid, v->name, sid);
            }
        }
    }
    return 0;
}

extern "C" void TierStub();
asm(".text\n"
    ".globl _TierStub\n"
    "_TierStub:\n"
    "  pushal\n"
    "  pushl %esi\n"
    "  call _OnTierSet\n"
    "  addl $4, %esp\n"
    "  popal\n"
    "  movl 0x724(%esi), %eax\n"
    "  jmp *_g_tierRet\n");

static bool InstallTierHook() {
    CreateThread(nullptr, 0, TierThread, nullptr, 0, nullptr);
    uint8_t* p = (uint8_t*)kTierSite;
    const uint8_t want[6] = {0x8B, 0x86, 0x24, 0x07, 0x00, 0x00};
    if (memcmp(p, want, 6)) { Log("loading screen class: code does not match - not hooked"); return false; }
    DWORD old;
    VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(int32_t*)(p + 1) = (int32_t)((uint32_t)&TierStub - (kTierSite + 5));
    p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
    return true;
}
