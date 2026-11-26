
// ===================================================================== conditions in the Single Race event records
// The event list's icons, the info box and the loading screen read an event's time of day and weather from its record
// (NFS11 ChallengeInstance fields, hashes by the exe's field-name hash 0x811C70):
//   TimeOfDay da6ac384 +0x120 f32, FinishTimeOfDay 21a309d6 +0x70 f32, SunDirection d6529487 +0x11C,
//   IsAlternativeWeather 6710cff0 +0x135, IsRainActive cfa2d288 +0x136, IsThunderActive 0361ec56 +0x137,
//   IsWet d4efa547 +0x138, OverrideSunDirection 0b76aef0 +0x13A (u8), WeatherIcons 0194dacf +0x12C (2 x icon id).
// The helper writes the Race Settings conditions into all Single Race records (2100000..2499999) whenever they change,
// so everything shows what the race will have. The finish time keeps each event's own span (progression on).
// Icon ids (from the career events): sun 823036, moon 823039, rain 823038, wet roads 829342, storm 829344.
struct SrRecord { uint8_t* d; float span; };
static SrRecord g_srRecords[400];
static int g_nSrRecords = -1;  // -1 = not scanned yet
static const uint32_t kFTod = 0x120, kFFinish = 0x70, kFAlt = 0x135, kFRain = 0x136, kFThunder = 0x137, kFWet = 0x138,
                      kFOverrideSun = 0x13A, kFIcons = 0x12C;
static const uint32_t kIconSun = 823036, kIconMoon = 823039, kIconRain = 823038, kIconWet = 829342, kIconStorm = 829344;

static void ScanSrRecords() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint8_t* a = (uint8_t*)si.lpMinimumApplicationAddress;
    static uint8_t buf[1 << 20];
    int n = 0;
    DWORD t0 = GetTickCount();
    while (a < (uint8_t*)si.lpMaximumApplicationAddress) {
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
                    if (*(uint32_t*)(buf + i) != kHashEvent) continue;
                    uint32_t id = *(uint32_t*)(buf + i + 4);
                    if (id < kSrFirst || id > kSrLast) continue;
                    uint32_t P = (uint32_t)(p + i) - 0xC;
                    if (P < (uint32_t)p || *(uint32_t*)(P + 8) != P + 0x20 || !Readable((void*)(P + 0x20), 0x140)) continue;
                    if (n < 400) {
                        uint8_t* d = (uint8_t*)(P + 0x20);
                        float span = *(float*)(d + kFFinish) - *(float*)(d + kFTod);
                        if (span < 0) span += 24.0f;
                        g_srRecords[n++] = {d, span};
                    }
                }
                if (len < sizeof buf) break;
            }
        }
        a = next;
    }
    g_nSrRecords = n;
    Log("conditions: %d Single Race event records found (%lu ms)", n, GetTickCount() - t0);
}

static void WriteConditions(const Settings& s, const char* why) {
    if (g_nSrRecords < 0) ScanSrRecords();
    float start = kTimeHours[s.time];
    bool night = start >= 21.0f || start < 4.0f;
    uint32_t tod = night ? kIconMoon : kIconSun;
    uint32_t i1 = tod, i2 = 0;
    if (s.weather == kWet) { i1 = kIconWet; i2 = tod; }
    else if (s.weather == kRain) { i1 = kIconRain; i2 = night ? kIconMoon : 0; }
    else if (s.weather == kStorm) { i1 = kIconStorm; i2 = night ? kIconMoon : 0; }
    int w = 0;
    for (int i = 0; i < g_nSrRecords; ++i) {
        uint8_t* d = g_srRecords[i].d;
        if (!Readable(d, 0x140)) continue;
        float fin = start + (s.timeProgression ? g_srRecords[i].span : 0.001f);
        if (fin >= 24.0f) fin -= 24.0f;
        *(float*)(d + kFTod) = start;
        *(float*)(d + kFFinish) = fin;
        d[kFWet] = s.weather != kDry;
        d[kFRain] = s.weather == kRain || s.weather == kStorm;
        d[kFThunder] = s.weather == kStorm;
        d[kFAlt] = s.weather == kRain || s.weather == kStorm;
        d[kFOverrideSun] = 0;
        *(uint32_t*)(d + kFIcons) = i1;
        *(uint32_t*)(d + kFIcons + 4) = i2;
        ++w;
    }
    Log("conditions (%s): %s, %s, time progression %s written into %d records (icons %u / %u)", why, kTimeNames[s.time],
        kWeatherLabels[s.weather], s.timeProgression ? "on" : "off", w, i1, i2);
}

// ---- the picker's info box text (SR.Summary) and keeping the records in step with the ini
static Settings g_pickerSet;
static int g_pickerMode = -1;
static DWORD g_pickerReadAt;
static int g_writtenSig = -1;
static char g_summary[512];

static void RefreshPickerSettings(bool force) {
    DWORD now = GetTickCount();
    if (!force && g_pickerMode >= 0 && now - g_pickerReadAt < 500) return;
    g_pickerReadAt = now;
    g_pickerSet = ReadEventSettings();
    g_pickerMode = (int)ReadMode();
    const Settings& s = g_pickerSet;
    int sig = s.time * 100 + s.weather * 10 + s.timeProgression;
    if (sig != g_writtenSig) {
        g_writtenSig = sig;
        WriteConditions(s, "picker");
    }
    static const char* kModeLines[] = {"RACE", "HOT PURSUIT (RACER)", "HOT PURSUIT (COP)"};
    static const char* kDiff[] = {"EASY", "MEDIUM", "HARD"};
    std::string t = kModeLines[g_pickerMode];
    t += "\nDIFFICULTY: ";
    t += kDiff[s.difficulty];
    char b[160];
    if (g_pickerMode == kRace) snprintf(b, sizeof b, "\nOPPONENTS: %d", s.raceRacers);
    else if (g_pickerMode == kHotPursuit) snprintf(b, sizeof b, "\nRACERS: %d   COPS: %d", s.hpRacers, s.hpCops);
    else snprintf(b, sizeof b, "\nRACERS: %d", s.copRacers);
    t += b;
    if (g_pickerMode != kRace) {
        const int* c = s.weaponsMode == kWeaponsCustom ? s.charges : kClassicCharges;
        if (g_pickerMode == kHotPursuit)
            snprintf(b, sizeof b, "\nWEAPONS (LEVEL %d): SPIKE STRIP %d, EMP %d, JAMMER %d, TURBO %d", s.weaponLevel, c[0], c[1], c[2], c[3]);
        else
            snprintf(b, sizeof b, "\nWEAPONS (LEVEL %d): SPIKE STRIP %d, EMP %d, ROADBLOCK %d, HELICOPTER %d", s.weaponLevel, c[4], c[5], c[6], c[7]);
        t += b;
    }
    snprintf(g_summary, sizeof g_summary, "%s", t.c_str());
}
