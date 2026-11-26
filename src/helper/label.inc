
// ===================================================================== main menu label (WALL -> SINGLE RACE)
// The main menu's Wall item takes its title from string 349389 ("WALL"; NFS11.exe 0x79766D: mov [eax+8], 0x554CD).
// The loaded language table (UI\LANGUAGE\00xx.BNDL, 32-bit) is: +0 u32 1, +4 u32 count, +8 ids, +0xC entries
// (on disk offsets 0x10 / 0x10 + 4·count; in memory either fixed up to pointers or kept relative - both handled);
// entry = u32 string (pointer or offset), u32 length in chars including the 0. The entry for 349389 is pointed at
// the helper's own text before the main menu copies its texts. ini [MenuLabel] Text (empty = keep WALL).
static const uint32_t kWallStringId = 349389;
static wchar_t g_labelText[64];
static uint32_t g_labelLen;

#include "RouteNames.h"
// Route names of the Remastered-only Single Race routes: empty HP2010 strings (length 1) pointed at the names.
static bool g_routeNamesOn;
static int PatchRouteNames(uint32_t hdr, const uint32_t* idv, uint32_t count, uint32_t ents, bool rel) {
    int n = 0;
    if (!g_routeNamesOn) return 0;
    for (const RouteName& r : kRouteNames) {
        int lo = 0, hi = (int)count - 1, at = -1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (idv[mid] == r.stringId) { at = mid; break; }
            if (idv[mid] < r.stringId) lo = mid + 1; else hi = mid - 1;
        }
        if (at < 0) continue;
        uint32_t* ent = (uint32_t*)(ents + 8 * at);
        uint32_t want = rel ? (uint32_t)r.text - hdr : (uint32_t)r.text;
        if (ent[0] == want) continue;
        if (ent[1] != 1) continue;  // not empty in this language: left alone
        DWORD old;
        if (!VirtualProtect(ent, 8, PAGE_READWRITE, &old)) continue;
        ent[1] = (uint32_t)wcslen(r.text) + 1;
        InterlockedExchange((LONG*)&ent[0], (LONG)want);
        VirtualProtect(ent, 8, old, &old);
        ++n;
    }
    return n;
}

static bool LabelPatchTable(uint32_t hdr, bool* patched) {
    uint32_t h[4];
    SIZE_T n = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (void*)hdr, h, 16, &n) || n != 16) return false;
    if (h[0] != 1 || h[1] < 1000 || h[1] > 200000) return false;
    bool rel = h[2] == 0x10;
    uint32_t ids = rel ? hdr + 0x10 : h[2], ents = rel ? hdr + h[3] : h[3];
    if (!rel && (ids != hdr + 0x10 || ents != ids + 4 * h[1])) return false;
    if (rel && h[3] != 0x10 + 4 * h[1]) return false;
    if (!Readable((void*)ids, 4 * h[1]) || !Readable((void*)ents, 8 * h[1])) return false;
    // binary search
    const uint32_t* idv = (const uint32_t*)ids;
    int lo = 0, hi = (int)h[1] - 1, at = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (idv[mid] == kWallStringId) { at = mid; break; }
        if (idv[mid] < kWallStringId) lo = mid + 1; else hi = mid - 1;
    }
    int rn = PatchRouteNames(hdr, idv, h[1], ents, rel);
    if (rn) Log("route names: table %08X: %d names set", hdr, rn);
    if (at < 0) return false;
    if (!g_labelLen) { *patched = rn > 0; return true; }  // label off: only the route names
    uint32_t* ent = (uint32_t*)(ents + 8 * at);
    uint32_t want = rel ? (uint32_t)g_labelText - hdr : (uint32_t)g_labelText;
    if (ent[0] == want) { *patched = false; return true; }
    uint32_t sp = rel ? hdr + ent[0] : ent[0];
    if (!Readable((void*)sp, 10) || memcmp((void*)sp, L"WALL", 10) || ent[1] != 5) {
        Log("menu label: table %08X: string %u is not \"WALL\" - left alone", hdr, kWallStringId);
        *patched = false;
        return true;
    }
    DWORD old;
    if (!VirtualProtect(ent, 8, PAGE_READWRITE, &old)) return false;
    ent[1] = g_labelLen;
    InterlockedExchange((LONG*)&ent[0], (LONG)want);
    VirtualProtect(ent, 8, old, &old);
    *patched = true;
    Log("menu label: table %08X (%u strings, %s): string %u -> \"%ls\"", hdr, h[1], rel ? "offsets" : "pointers",
        kWallStringId, g_labelText);
    return true;
}

// Scans committed private RW memory for language tables (there can be more than one loaded); returns how many.
static int LabelScan(uint32_t* found, int max) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint8_t* a = (uint8_t*)si.lpMinimumApplicationAddress;
    static uint8_t buf[1 << 20];
    int nf = 0;
    while (a < (uint8_t*)si.lpMaximumApplicationAddress && nf < max) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(a, &mi, sizeof mi)) break;
        uint8_t* next = (uint8_t*)mi.BaseAddress + mi.RegionSize;
        if (mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) && !(mi.Protect & PAGE_GUARD) &&
            mi.Type == MEM_PRIVATE) {
            for (uint8_t* p = (uint8_t*)mi.BaseAddress; p < next; p += sizeof buf - 16) {
                size_t len = (size_t)(next - p) < sizeof buf ? (size_t)(next - p) : sizeof buf;
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), p, buf, len, &got) || got < 16) continue;
                for (size_t i = 0; i + 16 <= got; i += 4) {
                    const uint32_t* q = (const uint32_t*)(buf + i);
                    if (q[0] != 1 || q[1] < 1000 || q[1] > 200000) continue;
                    uint32_t P = (uint32_t)(p + i);
                    bool ptrs = q[2] == P + 0x10 && q[3] == q[2] + 4 * q[1];
                    bool rel = q[2] == 0x10 && q[3] == 0x10 + 4 * q[1];
                    if ((ptrs || rel) && nf < max) found[nf++] = P;
                }
                if (len < sizeof buf) break;
            }
        }
        a = next;
    }
    return nf;
}

static DWORD WINAPI LabelThread(LPVOID) {
    uint32_t tables[8];
    int n = 0, patchedAny = 0;
    DWORD t0 = GetTickCount();
    bool logged = false;
    for (;;) {
        // re-check the known tables; rescan when none is valid (a language change loads a new table)
        int valid = 0;
        for (int i = 0; i < n; ++i) {
            bool p = false;
            if (LabelPatchTable(tables[i], &p)) { ++valid; patchedAny += p; }
        }
        if (!valid) {
            n = LabelScan(tables, 8);
            for (int i = 0; i < n; ++i) {
                bool p = false;
                if (LabelPatchTable(tables[i], &p)) patchedAny += p;
            }
            if (!n && !logged && GetTickCount() - t0 > 180000) {
                Log("menu label: no language table found after 3 minutes - still looking");
                logged = true;
            }
        }
        Sleep(n ? 1000 : (GetTickCount() - t0 < 180000 ? 100 : 2000));
    }
    return 0;
}

static void StartMenuLabel() {
    // 10.1: the route names go into empty strings next to the in-race radio messages; off until that is ruled out
    // as the cause of the race-load crash (ini [RouteNames] Enabled=1 turns them on)
    g_routeNamesOn = true;  // 11: always (cleared as the crash cause; the old [RouteNames] key is ignored)
    Log("route names: %s ([RouteNames] Enabled)", g_routeNamesOn ? "on" : "off");
    if (IniString("MenuLabel", "Text", "\x01") == "\x01") WritePrivateProfileStringA("MenuLabel", "Text", "SINGLE RACE", g_ini);
    std::string t = IniString("MenuLabel", "Text", "SINGLE RACE");
    if (t.empty()) Log("menu label: [MenuLabel] Text is empty - WALL kept");
    int w = t.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, g_labelText, 63);
    if (w <= 1) w = MultiByteToWideChar(CP_ACP, 0, t.c_str(), -1, g_labelText, 63);
    for (wchar_t* c = g_labelText; *c; ++c)  // "\n" in the ini = a line break
        if (c[0] == L'\\' && c[1] == L'n') { c[0] = L'\n'; memmove(c + 1, c + 2, (wcslen(c + 2) + 1) * sizeof(wchar_t)); }
    g_labelLen = t.empty() ? 0 : (uint32_t)wcslen(g_labelText) + 1;
    CreateThread(nullptr, 0, LabelThread, nullptr, 0, nullptr);
}
