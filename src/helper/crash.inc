
// ===================================================================== crash logger (10.2)
// First-chance access violations / illegal instructions / stack overflows in the game: the address, registers, the
// module and the top of the stack go to the log, and the first one writes NFS11_SingleRace_crash.dmp (dbghelp
// MiniDumpWriteDump, MiniDumpWithIndirectlyReferencedMemory) next to the log.
static char g_dumpPath[MAX_PATH];
static volatile LONG g_crashLogged;

static void ModuleOf(uint32_t a, char* out, size_t n) {
    HMODULE m = nullptr;
    out[0] = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        char p[MAX_PATH];
        GetModuleFileNameA(m, p, MAX_PATH);
        const char* s = strrchr(p, '\\');
        snprintf(out, n, "%s+%X", s ? s + 1 : p, a - (uint32_t)m);
    }
}

static LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    LONG n = InterlockedIncrement(&g_crashLogged);
    if (n > 3) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    char mod[160];
    ModuleOf(c->Eip, mod, sizeof mod);
    Log("CRASH %d: code %08lX at %08lX (%s), %s %08lX", (int)n, (unsigned long)code, (unsigned long)c->Eip, mod,
        ep->ExceptionRecord->NumberParameters >= 2 ? (ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read") : "",
        ep->ExceptionRecord->NumberParameters >= 2 ? (unsigned long)ep->ExceptionRecord->ExceptionInformation[1] : 0ul);
    Log("  eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX", c->Eax, c->Ebx, c->Ecx, c->Edx,
        c->Esi, c->Edi, c->Ebp, c->Esp);
    const uint32_t* sp = (const uint32_t*)c->Esp;
    char line[600];
    int len = 0;
    for (int i = 0; i < 48 && Readable(sp + i, 4); ++i) {
        uint32_t v = sp[i];
        if (v >= 0x401000 && v < 0xC40000) len += snprintf(line + len, sizeof line - len, " %08X", v);  // return addresses
        if (len > 520) break;
    }
    Log("  stack (exe addresses): %s", line);
    if (n == 1 && g_dumpPath[0]) {
        typedef BOOL(WINAPI * Dump_t)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
        HMODULE dh = LoadLibraryA("dbghelp.dll");
        Dump_t dump = dh ? (Dump_t)GetProcAddress(dh, "MiniDumpWriteDump") : nullptr;
        HANDLE f = dump ? CreateFileA(g_dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr) : INVALID_HANDLE_VALUE;
        if (f != INVALID_HANDLE_VALUE) {
            struct { DWORD tid; EXCEPTION_POINTERS* ep; BOOL client; } mei = {GetCurrentThreadId(), ep, FALSE};
            BOOL ok = dump(GetCurrentProcess(), GetCurrentProcessId(), f, 0x40 /*IndirectlyReferencedMemory*/, &mei, nullptr, nullptr);
            CloseHandle(f);
            Log("  dump %s: %s", ok ? "written" : "FAILED", g_dumpPath);
        }
    }
    if (g_log) fflush(g_log);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void InstallCrashLogger(const char* dir) {
    snprintf(g_dumpPath, sizeof g_dumpPath, "%sNFS11_SingleRace_crash.dmp", dir);
    AddVectoredExceptionHandler(1, CrashHandler);
    Log("crash logger installed");
}
