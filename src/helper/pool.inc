
// ===================================================================== message pool overflow guard (11.1)
// 0x4384F0 = the engine's per-frame message pool allocator (thiscall, pool in ecx; args size, tag; ret 8):
//   pool { int count, max; {ptr, size}* entries; u32* tags; u32 used, cap; u8* base }.
// It returns null when the pool is full and none of its ~70 callers checks: they memcpy into it. Crash seen
// 2026-11-27 at the loading-screen -> intro switch: the vehicle command handler 0x4594B0 (command type 5) filled a
// 32 KB pool with 170 records (0xC0 each). The replacement allocates exactly as the original and, when the pool is
// full, hands out a scratch buffer instead: the record is dropped (not counted, nothing reads it) instead of a crash.
struct MsgPool { int count, max; uint32_t* entries; uint32_t* tags; uint32_t used, cap, base; };
alignas(16) static uint8_t g_poolScratch[0x10000];
static volatile LONG g_poolOverflows;

static void* __fastcall PoolAlloc(MsgPool* p, void*, uint32_t size, uint32_t tag) {
    if (p->count < p->max) {
        uint32_t off = (p->used + 15) & ~15u;
        uint32_t end = off + size;
        if ((int)end <= (int)p->cap) {
            int i = p->count;
            p->count = i + 1;
            uint32_t a = p->base + off;
            p->used = end;
            p->entries[2 * i] = a;
            p->entries[2 * i + 1] = size;
            if (p->tags) p->tags[i] = tag;
            return (void*)a;
        }
    }
    LONG n = InterlockedIncrement(&g_poolOverflows);
    if (n == 1 || n % 1000 == 0)
        Log("message pool %p full (%d of %d records, %u of %u bytes, %u more wanted): record dropped (%ld so far)",
            (void*)p, p->count, p->max, p->used, p->cap, size, (long)n);
    return size <= sizeof g_poolScratch ? g_poolScratch : nullptr;
}

static void InstallPoolGuard() {
    static const uint8_t kOrig[] = {0x56, 0x8B, 0x31, 0x3B, 0x71, 0x04, 0x7C, 0x06};
    uint8_t* f = (uint8_t*)0x4384F0;
    if (memcmp(f, kOrig, sizeof kOrig)) { Log("message pool guard: unexpected bytes at 004384F0 - not installed"); return; }
    DWORD old;
    VirtualProtect(f, 5, PAGE_EXECUTE_READWRITE, &old);
    f[0] = 0xE9;
    *(uint32_t*)(f + 1) = (uint32_t)&PoolAlloc - (uint32_t)(f + 5);
    VirtualProtect(f, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f, 5);
    Log("message pool guard installed (004384F0)");
}
