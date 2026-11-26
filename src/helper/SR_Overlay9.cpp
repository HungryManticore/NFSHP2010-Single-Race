// Single Race "Race Settings" menu for NFS Hot Pursuit (2010), drawn with Dear ImGui 1.91.5 over the game's frames.
// Port of the Remastered helper's SR_Overlay.cpp (v37): the menu, its layout, fonts and prompts are the same; the
// platform parts are new:
// * Direct3D 9: the game's Direct3DCreate9 import and IDirect3D9::CreateDevice are hooked, then the game's own device's
//   Present / Reset and its swap chain's Present (vtables); the menu is drawn into the back buffer just before Present.
//   (v6 hooked a throw-away device's vtable: the game's device has another one, so nothing was drawn.)
// * Keyboard: the game window is subclassed, DirectInput 8 GetDeviceState / GetDeviceData are hooked (vtable) and the
//   game's GetAsyncKeyState import is redirected, so the game reads no keys while the menu is open.
// * Controller: the game's XInputGetState import (xinput1_3 ordinal 2) is redirected: the menu sees the real pad, the
//   game an empty one while the menu is open. Back / View opens the menu on the picker (and is kept from the game there).
// * Only on the Single Race picker (SR_PickerVisible). Settings are read from / saved to the ini.

#include "SR_Shared9.h"
#include <d3d9.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "SR_Assets.h"

using namespace sr;

namespace {

const GUID kIID_IDirectInput8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
const GUID kIID_IDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
const GUID kGUID_SysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// menu texts (as in Remastered)
const char* const kEventModeNames[kEventModeCount] = {"Race", "HotPursuit", "CopHotPursuit"};
const char* const kEventModeLabels[kEventModeCount] = {"Race", "Hot Pursuit (Racer)", "Hot Pursuit (Cop)"};
const char* const kTimeNames[kTimeCount] = {"Dawn", "Morning", "Noon", "Afternoon", "Sunset", "Night"};
const char* const kWeatherNames[kWeatherCount] = {"Dry", "Wet", "Rain", "Storm"};
const char* const kWeatherLabels[kWeatherCount] = {"Dry", "Wet roads", "Rain", "Storm"};
const char* const kWeaponsModeNames[kWeaponsModeCount] = {"Classic", "Custom"};
const char* const kWeaponLabels[kWeaponCount] = {"Spike Strip", "EMP", "Jammer", "Turbo", "Spike Strip", "EMP", "Roadblock", "Helicopter"};
const int kClassicCharges[kWeaponCount] = {0, 0, 0, 0, 3, 0, 3, 2};
const char* const kDifficultyNames[kDifficultyCount] = {"Easy", "Medium", "Hard"};

// ====================================================================== state

CRITICAL_SECTION g_imguiLock;
bool g_imguiReady;
HWND g_hwnd;
WNDPROC g_origWndProc;
bool g_wndUnicode;
IDirect3DDevice9* g_device;  // the device ImGui's DX9 backend was initialised with (not owned)
float g_fontPx = 20.0f;
ImFont* g_fontReg;
ImFont* g_fontIt;
float g_regCap, g_itCap;
struct IconRect { float w, h; ImVec2 uv0, uv1; };
IconRect g_icons[kIconCount];
bool g_iconsReady;
float g_valueX, g_rowW;

enum InputDevice { kDevKeyboard, kDevPad };
volatile int g_inputDev = kDevKeyboard;
volatile LONG g_padActivity;

volatile bool g_menuOpen;
volatile LONG g_menuRequest;  // set by the game thread (SR.OpenSettings), taken by HandleInput
int g_menuKey = VK_F5;  // read once at start-up (ini [Overlay] Key)
ULONGLONG g_blockUntil;
volatile bool g_waitPadRelease;
bool g_waitKeyRelease;

Conditions M;
int g_row;
enum Tab { kTabSetup, kTabWeapons, kTabCount };
const char* const kTabNames[kTabCount] = {"Race setup", "Weapons"};
int g_tab;
enum { kRowTabs, kRowMode, kRowTime, kRowTimeProgression, kRowWeather, kRowDifficulty, kRowRacers, kRowCops, kRowWeaponLevel, kRowWeapons, kRowFirstCharge };
int VisibleRows(int* ids) {
    int n = 0;
    ids[n++] = kRowTabs;
    if (g_tab == kTabSetup) {
        ids[n++] = kRowMode;
        ids[n++] = kRowTime;
        ids[n++] = kRowTimeProgression;
        ids[n++] = kRowWeather;
        ids[n++] = kRowDifficulty;
        ids[n++] = kRowRacers;
        if (M.eventMode == kModeHotPursuit) ids[n++] = kRowCops;
    } else {
        ids[n++] = kRowWeaponLevel;
        ids[n++] = kRowWeapons;
        if (M.weaponsMode == kWeaponsCustom)
            for (int w = 0; w < kWeaponCount; ++w) ids[n++] = kRowFirstCharge + w;
    }
    return n;
}
int RowCount() {
    int ids[kRowFirstCharge + kWeaponCount];
    return VisibleRows(ids);
}
int RowId(int index) {
    int ids[kRowFirstCharge + kWeaponCount];
    int n = VisibleRows(ids);
    return index >= 0 && index < n ? ids[index] : -1;
}

ULONGLONG Now() { return GetTickCount64(); }
bool InputBlocked() { return g_menuOpen || Now() < g_blockUntil; }

// ====================================================================== import redirection (the game's IAT)

void** FindImport(const char* dll, const char* name, int ordinal) {
    uint8_t* base = (uint8_t*)GetModuleHandleA(nullptr);
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd.VirtualAddress) return nullptr;
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dd.VirtualAddress); d->Name; ++d) {
        if (_stricmp((const char*)(base + d->Name), dll)) continue;
        IMAGE_THUNK_DATA32* orig = (IMAGE_THUNK_DATA32*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA32* iat = (IMAGE_THUNK_DATA32*)(base + d->FirstThunk);
        for (; orig->u1.AddressOfData; ++orig, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL32(orig->u1.Ordinal)) {
                if (ordinal && (int)IMAGE_ORDINAL32(orig->u1.Ordinal) == ordinal) return (void**)&iat->u1.Function;
            } else if (name) {
                IMAGE_IMPORT_BY_NAME* n = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
                if (!strcmp((const char*)n->Name, name)) return (void**)&iat->u1.Function;
            }
        }
    }
    return nullptr;
}
void* PatchPointer(void** slot, void* fn) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return nullptr;
    void* prev = *slot;
    *slot = fn;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return prev;
}
void* PatchVtable(void** vt, int index, void* fn) { return PatchPointer(&vt[index], fn); }

// ====================================================================== XInput

struct XGamepad { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XState { DWORD packet; XGamepad pad; };
typedef DWORD(WINAPI* XGetState_t)(DWORD, XState*);
const WORD kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008, kPadStart = 0x0010, kPadBack = 0x0020, kPadLB = 0x0100,
           kPadRB = 0x0200, kPadA = 0x1000, kPadB = 0x2000;

XGetState_t g_realGetState;
volatile LONG g_padButtons[4];

bool PadActive(const XGamepad& p) {
    const int kStick = 12000;
    return p.buttons || p.lt > 40 || p.rt > 40 || p.lx > kStick || p.lx < -kStick || p.ly > kStick || p.ly < -kStick ||
           p.rx > kStick || p.rx < -kStick || p.ry > kStick || p.ry < -kStick;
}

DWORD WINAPI Hook_XInputGetState(DWORD idx, XState* s) {
    DWORD r = g_realGetState(idx, s);
    if (r != 0 || !s || idx > 3) return r;
    g_padButtons[idx] = s->pad.buttons;
    static bool wasActive[4];
    bool active = PadActive(s->pad);
    if (active && !wasActive[idx]) InterlockedIncrement(&g_padActivity);
    wasActive[idx] = active;
    if (InputBlocked() || g_waitPadRelease) memset(&s->pad, 0, sizeof(s->pad));
    else if (SR_PickerVisible()) s->pad.buttons &= (WORD)~kPadBack;  // Back / View opens the menu on the picker
    return r;
}

void HookXInput() {
    void** slot = FindImport("XINPUT1_3.dll", "XInputGetState", 2);
    if (!slot || !*slot) { SR_Log("overlay: XInputGetState import not found - controller not available for the menu"); return; }
    g_realGetState = (XGetState_t)*slot;
    PatchPointer(slot, (void*)Hook_XInputGetState);
    SR_Log("overlay: XInput hooked (import of xinput1_3 ordinal 2)");
}

// GetAsyncKeyState as the game imports it: nothing is down while the menu is open (the helper's own calls are not
// affected - they use the helper's import).
typedef SHORT(WINAPI* GetAsyncKeyState_t)(int);
GetAsyncKeyState_t g_realGetAsyncKeyState;
SHORT WINAPI Hook_GetAsyncKeyState(int vk) { return InputBlocked() ? 0 : g_realGetAsyncKeyState(vk); }
void HookGetAsyncKeyState() {
    void** slot = FindImport("USER32.dll", "GetAsyncKeyState", 0);
    if (!slot || !*slot) return;
    g_realGetAsyncKeyState = (GetAsyncKeyState_t)*slot;
    PatchPointer(slot, (void*)Hook_GetAsyncKeyState);
}

// ====================================================================== the menu

void OpenMenu() {
    M = SR_GetConditions();
    g_tab = kTabSetup;
    g_row = 1;  // the first setting (Mode); up reaches the tab bar
    g_menuOpen = true;
    SR_Log("conditions menu opened (%s, %s, %s, %s, weapons %s, AI: race %d racers, hot pursuit %d racers + %d cops, "
           "cop hot pursuit %d racers)",
           kEventModeNames[M.eventMode], kDifficultyNames[M.difficulty], kTimeNames[M.time], kWeatherNames[M.weather],
           kWeaponsModeNames[M.weaponsMode], M.raceRacers, M.hpRacers, M.hpCops, M.copRacers);
}

void CloseMenu(bool save) {
    if (save) SR_SetConditions(M);
    g_menuOpen = false;
    g_blockUntil = Now() + 400;
    g_waitPadRelease = true;
    g_waitKeyRelease = true;
    Conditions c = SR_GetConditions();
    SR_Log("conditions menu %s: %s, %s, %s (time progression %s), %s, weapons %s level %d (racers %d %d %d %d, cops %d %d %d %d), AI: race %d racers, "
           "hot pursuit %d racers + %d cops, cop hot pursuit %d racers", save ? "saved" : "cancelled",
           kEventModeNames[c.eventMode], kDifficultyNames[c.difficulty], kTimeNames[c.time], c.timeProgression ? "on" : "off", kWeatherNames[c.weather], kWeaponsModeNames[c.weaponsMode], c.weaponLevel, c.charges[0], c.charges[1],
           c.charges[2], c.charges[3], c.charges[4], c.charges[5], c.charges[6], c.charges[7], c.raceRacers, c.hpRacers, c.hpCops,
           c.copRacers);
}

// LB / RB, or left / right on the tab bar. The cursor stays on the tab bar, or goes to the tab's first setting.
void SwitchTab(int dir) {
    g_tab = (g_tab + dir + kTabCount) % kTabCount;
    if (g_row != 0) g_row = 1;
}

// AI count rows. Race: 1-7 racers. Hot Pursuit: 1-6 racers and 1-6 cops, 7 in all at most: a step up that would
// go past 7 does nothing. Cop Hot Pursuit: 1-7 racers (the player is the only cop).
void StepCount(int id, int dir) {
    bool hp = M.eventMode == kModeHotPursuit;
    if (M.eventMode == kModeCopPursuit) {
        int n = M.copRacers + dir;
        if (n >= kCopRacersMin && n <= kCopRacersMax) M.copRacers = n;
        return;
    }
    if (!hp) {
        int n = M.raceRacers + dir;
        if (n >= kRaceRacersMin && n <= kRaceRacersMax) M.raceRacers = n;
        return;
    }
    int& v = id == kRowCops ? M.hpCops : M.hpRacers;
    int n = v + dir, mx = id == kRowCops ? kHpCopsMax : kHpRacersMax;
    if (n < kHpMin || n > mx) return;
    if (dir > 0 && M.hpRacers + M.hpCops + 1 > kMaxAI) return;
    v = n;
}

void Step(int dir) {
    int id = RowId(g_row);
    if (id == kRowTabs) SwitchTab(dir);
    else if (id == kRowMode) M.eventMode = (M.eventMode + dir + kEventModeCount) % kEventModeCount;
    else if (id == kRowDifficulty) M.difficulty = (M.difficulty + dir + kDifficultyCount) % kDifficultyCount;
    else if (id == kRowTime) M.time = (M.time + dir + kTimeCount) % kTimeCount;
    else if (id == kRowTimeProgression) M.timeProgression = !M.timeProgression;
    else if (id == kRowWeather) M.weather = (M.weather + dir + kWeatherCount) % kWeatherCount;
    else if (id == kRowRacers || id == kRowCops) StepCount(id, dir);
    else if (id == kRowWeaponLevel)
        M.weaponLevel = (M.weaponLevel - kWeaponLevelMin + dir + kWeaponLevelMax - kWeaponLevelMin + 1) %
                            (kWeaponLevelMax - kWeaponLevelMin + 1) + kWeaponLevelMin;
    else if (id == kRowWeapons) M.weaponsMode = (M.weaponsMode + dir + kWeaponsModeCount) % kWeaponsModeCount;
    else if (id >= kRowFirstCharge) {
        int& n = M.charges[id - kRowFirstCharge];
        n += dir;
        if (n < 0) n = 0;
        if (n > kMaxCharges) n = kMaxCharges;
    }
}

void MoveRow(int dir) {
    g_row += dir;
    if (g_row < 0) g_row = 0;
    if (g_row >= RowCount()) g_row = RowCount() - 1;
}

const char* KeyName(int vk, char* out) {
    if (vk >= VK_F1 && vk <= VK_F24) sprintf(out, "F%d", vk - VK_F1 + 1);
    else sprintf(out, "key %d", vk);
    return out;
}

// ---------------------------------------------------------------------- text and prompt drawing

const char* Upper(const char* in, char* out, int size) {
    int i = 0;
    for (; in[i] && i < size - 1; ++i) out[i] = (in[i] >= 'a' && in[i] <= 'z') ? (char)(in[i] - 32) : in[i];
    out[i] = 0;
    return out;
}
float TextW(ImFont* f, const char* t, float size = 0) {
    return f->CalcTextSizeA(size > 0 ? size : f->FontSize, FLT_MAX, 0, t).x;
}
// Draws t with its baseline at y (so the italic and regular fonts line up).
void DrawAt(ImDrawList* dl, ImFont* f, float x, float baseline, ImU32 col, const char* t, float size = 0) {
    float sz = size > 0 ? size : f->FontSize;
    dl->AddText(f, sz, ImVec2(x, baseline - f->Ascent * sz / f->FontSize), col, t);
}

void DrawIcon(ImDrawList* dl, int id, float x, float cy, float w = 0) {
    const IconRect& r = g_icons[id];
    float ww = w > 0 ? w : r.w;
    dl->AddImage(ImGui::GetIO().Fonts->TexID, ImVec2(x, cy - r.h * 0.5f), ImVec2(x + ww, cy + r.h * 0.5f), r.uv0, r.uv1);
}

// A keyboard key with a name on it, built like the game builds it: keycap left half + stretched middle + right half,
// with the key name written on the cap face.
float KeycapTextSize() { return g_icons[IconKeycapLeft].h * 0.36f / kFontRegularCapEm; }
float KeycapWidth(const char* text) {
    const IconRect &l = g_icons[IconKeycapLeft], &r = g_icons[IconKeycapRight];
    float inner = TextW(g_fontReg, text, KeycapTextSize()) + l.w * 0.84f - l.w - r.w;
    return l.w + (inner > 0 ? inner : 0) + r.w;
}
void DrawKeycap(ImDrawList* dl, const char* text, float x, float cy) {
    const IconRect &l = g_icons[IconKeycapLeft], &r = g_icons[IconKeycapRight];
    float w = KeycapWidth(text), inner = w - l.w - r.w, size = KeycapTextSize();
    DrawIcon(dl, IconKeycapLeft, x, cy);
    if (inner > 0) DrawIcon(dl, IconKeycapMiddle, x + l.w, cy, inner);
    DrawIcon(dl, IconKeycapRight, x + l.w + inner, cy);
    float top = cy - l.h * 0.5f, cap = size * kFontRegularCapEm;
    DrawAt(dl, g_fontReg, x + (w - TextW(g_fontReg, text, size)) * 0.5f, top + l.h * 0.47f + cap * 0.5f, IM_COL32(255, 255, 255, 255), text, size);
}

// One prompt: up to two icons (or a named keycap) and its label.
struct Prompt { int icon1, icon2; const char* keycap; const char* label; };
float PromptIconsW(const Prompt& p, float gap) {
    float w = 0;
    if (p.keycap) w += KeycapWidth(p.keycap);
    if (p.icon1 >= 0) w += g_icons[p.icon1].w;
    if (p.icon2 >= 0) w += gap + g_icons[p.icon2].w;
    return w;
}
float PromptH(const Prompt& p) {
    float h = p.keycap ? g_icons[IconKeycapLeft].h : 0;
    if (p.icon1 >= 0 && g_icons[p.icon1].h > h) h = g_icons[p.icon1].h;
    if (p.icon2 >= 0 && g_icons[p.icon2].h > h) h = g_icons[p.icon2].h;
    return h;
}
// A row of prompts, like the game's footer; reserves its space in the window.
void PromptBar(const Prompt* ps, int n, ImU32 col) {
    const float em = g_fontPx, gapIcons = em * 0.12f, gapText = em * 0.35f, gapItems = em * 1.1f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = g_regCap * 1.3f, w = 0;
    char up[64];
    for (int i = 0; i < n; ++i) {
        if (PromptH(ps[i]) > h) h = PromptH(ps[i]);
        w += (i ? gapItems : 0) + PromptIconsW(ps[i], gapIcons) + gapText + TextW(g_fontReg, Upper(ps[i].label, up, 64));
    }
    float cy = p.y + h * 0.5f, x = p.x;
    for (int i = 0; i < n; ++i) {
        const Prompt& q = ps[i];
        if (i) x += gapItems;
        if (q.keycap) { DrawKeycap(dl, q.keycap, x, cy); x += KeycapWidth(q.keycap); }
        if (q.icon1 >= 0) { DrawIcon(dl, q.icon1, x, cy); x += g_icons[q.icon1].w; }
        if (q.icon2 >= 0) { x += gapIcons; DrawIcon(dl, q.icon2, x, cy); x += g_icons[q.icon2].w; }
        x += gapText;
        Upper(q.label, up, 64);
        DrawAt(dl, g_fontReg, x, cy + g_regCap * 0.5f, col, up);
        x += TextW(g_fontReg, up);
    }
    ImGui::Dummy(ImVec2(w, h));
}

// ---------------------------------------------------------------------- the menu

void MeasureLayout() {
    const float em = g_fontPx;
    char up[64], v[64];
    const char* labels[] = {"Mode", "Time of day", "Time progression", "Weather", "Difficulty", "Racers", "Cops", "Weapon level", "Weapons"};
    float lw = 0, vw = 0;
    for (const char* l : labels) { float t = TextW(g_fontIt, Upper(l, up, 64)); if (t > lw) lw = t; }
    for (int w = 0; w < kWeaponCount; ++w) {
        char l[48];
        sprintf(l, "    %s", kWeaponLabels[w]);
        float t = TextW(g_fontIt, Upper(l, up, 64));
        if (t > lw) lw = t;
    }
    auto value = [&](const char* s) { sprintf(v, "<   %s   >", s); float t = TextW(g_fontReg, v); if (t > vw) vw = t; };
    for (int i = 0; i < kEventModeCount; ++i) value(kEventModeLabels[i]);
    for (int i = 0; i < kTimeCount; ++i) value(kTimeNames[i]);
    for (int i = 0; i < kWeatherCount; ++i) value(kWeatherLabels[i]);
    for (int i = 0; i < kDifficultyCount; ++i) value(kDifficultyNames[i]);
    value("Classic Hot Pursuit");
    value("Custom");
    value("10");
    g_valueX = em * 0.6f + lw + em * 1.5f;
    g_rowW = g_valueX + vw + em * 0.6f;
    float tabs = em * 0.6f + em * 3.6f;  // the tab bar: names, gaps and the "<  >" marker
    for (int t = 0; t < kTabCount; ++t) tabs += TextW(g_fontIt, Upper(kTabNames[t], up, 64)) + em * 1.6f;
    if (tabs > g_rowW) g_rowW = tabs;
}

// The vertical centre of a row's text is shared by both fonts: same baseline, caps centred on the regular font.
float RowBaseline(float top) { return top + g_fontPx * 0.9f + g_regCap * 0.5f; }

void Row(int id, const char* label, const char* value) {
    const float em = g_fontPx;
    bool sel = RowId(g_row) == id;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (sel) dl->AddRectFilled(p, ImVec2(p.x + g_rowW, p.y + em * 1.8f), IM_COL32(255, 158, 31, 60), em * 0.25f);
    float by = RowBaseline(p.y);
    ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    char up[64], v[64];
    DrawAt(dl, g_fontIt, p.x + em * 0.6f, by, col, Upper(label, up, 64));
    sprintf(v, sel ? "<   %s   >" : "    %s", value);
    DrawAt(dl, g_fontReg, p.x + g_valueX, by, sel ? IM_COL32(255, 158, 31, 255) : col, v);
    ImGui::Dummy(ImVec2(g_rowW, em * 1.8f));
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + em * 2.0f));
}

// The tab bar: both tab names, the active one filled; highlighted like a row when the cursor is on it.
void TabBar() {
    const float em = g_fontPx;
    bool sel = g_row == 0;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = g_rowW;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (sel) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + em * 1.8f), IM_COL32(255, 158, 31, 60), em * 0.25f);
    float x = p.x + em * 0.6f, by = RowBaseline(p.y);
    char up[64];
    for (int t = 0; t < kTabCount; ++t) {
        const char* name = Upper(kTabNames[t], up, 64);
        float tw = TextW(g_fontIt, name);
        ImVec2 a(x - em * 0.5f, p.y + em * 0.15f), b(x + tw + em * 0.5f, p.y + em * 1.65f);
        if (t == g_tab) {
            dl->AddRectFilled(a, b, IM_COL32(255, 158, 31, sel ? 200 : 140), em * 0.2f);
            DrawAt(dl, g_fontIt, x, by, IM_COL32(20, 20, 20, 255), name);
        } else {
            dl->AddRect(a, b, IM_COL32(160, 160, 160, 160), em * 0.2f);
            DrawAt(dl, g_fontIt, x, by, IM_COL32(200, 200, 200, 255), name);
        }
        x += tw + em * 1.6f;
    }
    if (sel) DrawAt(dl, g_fontReg, p.x + w - em * 3.2f, by, IM_COL32(255, 158, 31, 255), "<  >");
    ImGui::Dummy(ImVec2(w, em * 1.8f));
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + em * 2.0f));
}

// A small heading inside the Weapons tab ("Racers" / "Cops").
void SubHeading(const char* text) {
    const float em = g_fontPx;
    ImVec2 p = ImGui::GetCursorScreenPos();
    char up[64];
    DrawAt(ImGui::GetWindowDrawList(), g_fontIt, p.x + em * 0.6f, p.y + em * 0.6f + g_itCap * 0.5f,
           ImGui::GetColorU32(ImGuiCol_TextDisabled), Upper(text, up, 64));
    ImGui::Dummy(ImVec2(g_rowW, em * 1.2f));
}

void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    const float em = g_fontPx;
    MeasureLayout();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushFont(g_fontIt);  // the window title
    ImGui::Begin("SINGLE RACE###sr", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(g_rowW, em * 0.3f));
    TabBar();
    ImGui::Dummy(ImVec2(0, em * 0.3f));
    if (g_tab == kTabSetup) {
        Row(kRowMode, "Mode", kEventModeLabels[M.eventMode]);
        Row(kRowTime, "Time of day", kTimeNames[M.time]);
        Row(kRowTimeProgression, "Time progression", M.timeProgression ? "On" : "Off");
        Row(kRowWeather, "Weather", kWeatherLabels[M.weather]);
        Row(kRowDifficulty, "Difficulty", kDifficultyNames[M.difficulty]);
        char r[8], c[8];
        if (M.eventMode == kModeHotPursuit) {
            sprintf(r, "%d", M.hpRacers);
            sprintf(c, "%d", M.hpCops);
            Row(kRowRacers, "Racers", r);
            Row(kRowCops, "Cops", c);
        } else {
            sprintf(r, "%d", M.eventMode == kModeCopPursuit ? M.copRacers : M.raceRacers);
            Row(kRowRacers, "Racers", r);
        }
    } else {
        char lv[16];
        sprintf(lv, "Level %d", M.weaponLevel);
        Row(kRowWeaponLevel, "Weapon level", lv);
        Row(kRowWeapons, "Weapons", M.weaponsMode == kWeaponsCustom ? "Custom" : "Classic Hot Pursuit");
        // Custom: the ini charges, adjustable. Classic: the Classic Hot Pursuit preset, greyed out and not
        // selectable (the Custom values are kept and come back when Custom is chosen again).
        bool custom = M.weaponsMode == kWeaponsCustom;
        if (!custom) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        for (int w = 0; w < kWeaponCount; ++w) {
            if (w == kRacerSpikeStrip || w == kCopSpikeStrip) {
                ImGui::Dummy(ImVec2(0, em * 0.2f));
                SubHeading(w == kRacerSpikeStrip ? "Racers" : "Cops");
            }
            char v[8], label[40];
            sprintf(v, "%d", custom ? M.charges[w] : kClassicCharges[w]);
            sprintf(label, "    %s", kWeaponLabels[w]);
            Row(kRowFirstCharge + w, label, v);
        }
        if (!custom) ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, em * 0.4f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, em * 0.2f));
    ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    if (!g_iconsReady) {
        ImGui::TextDisabled("D-pad / arrows: choose     LB / RB: tabs     A / Enter: save     B / Esc: cancel");
    } else if (g_inputDev == kDevPad) {
        const Prompt ps[] = {{IconDpadUpDown, -1, nullptr, "Choose"}, {IconDpadLeftRight, -1, nullptr, "Change"},
                             {IconPadLB, IconPadRB, nullptr, "Tabs"}, {IconPadA, -1, nullptr, "Save"},
                             {IconPadB, -1, nullptr, "Cancel"}};
        PromptBar(ps, 5, col);
    } else {
        // Keyboard: tabs are switched with left / right on the tab bar.
        const Prompt ps[] = {{IconKeyUp, IconKeyDown, nullptr, "Choose"}, {IconKeyLeft, IconKeyRight, nullptr, "Change"},
                             {IconKeyEnter, -1, nullptr, "Save"}, {-1, -1, "ESC", "Cancel"}};
        PromptBar(ps, 4, col);
    }
    ImGui::End();
}

// v34: the top-right "Race Settings" hint (v26-v33) is removed: the picker's footer prompt bar shows it
// natively (WALL v12, sr_info.lua).


// ====================================================================== window procedure

bool IsInputMessage(UINT m) {
    return (m >= WM_KEYFIRST && m <= WM_KEYLAST) || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || m == WM_INPUT;
}

LRESULT CALLBACK OverlayWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (InputBlocked() && IsInputMessage(m)) return 0;
    return g_wndUnicode ? CallWindowProcW(g_origWndProc, h, m, w, l) : CallWindowProcA(g_origWndProc, h, m, w, l);
}

// ====================================================================== DirectInput (keyboard / mouse blocking)

typedef HRESULT(WINAPI* GetDeviceState_t)(void* self, DWORD cb, void* data);
typedef HRESULT(WINAPI* GetDeviceData_t)(void* self, DWORD cbObj, void* rgdod, DWORD* inOut, DWORD flags);

struct DiVtable { void** vt; GetDeviceState_t state; GetDeviceData_t data; };
DiVtable g_diVt[2];
int g_diVtCount;

const DiVtable* FindDiVt(void* self) {
    void** vt = *(void***)self;
    for (int i = 0; i < g_diVtCount; ++i)
        if (g_diVt[i].vt == vt) return &g_diVt[i];
    return nullptr;
}

HRESULT WINAPI Hook_GetDeviceState(void* self, DWORD cb, void* data) {
    const DiVtable* v = FindDiVt(self);
    if (!v) return E_FAIL;
    HRESULT hr = v->state(self, cb, data);
    if (SUCCEEDED(hr) && data && InputBlocked()) memset(data, 0, cb);
    return hr;
}

HRESULT WINAPI Hook_GetDeviceData(void* self, DWORD cbObj, void* rgdod, DWORD* inOut, DWORD flags) {
    const DiVtable* v = FindDiVt(self);
    if (!v) return E_FAIL;
    HRESULT hr = v->data(self, cbObj, rgdod, inOut, flags);
    if (SUCCEEDED(hr) && inOut && rgdod && InputBlocked()) *inOut = 0;
    return hr;
}

void HookDirectInput() {
    typedef HRESULT(WINAPI * Create_t)(HINSTANCE, DWORD, REFIID, void**, void*);
    // the game's import (dinput8.dll here is the ASI loader, which forwards to the system DLL)
    void** slot = FindImport("DINPUT8.dll", "DirectInput8Create", 0);
    Create_t create = slot ? (Create_t)*slot : nullptr;
    if (!create) { SR_Log("overlay: DirectInput8Create not found - keyboard is not blocked"); return; }
    const GUID* iids[2] = {&kIID_IDirectInput8A, &kIID_IDirectInput8W};
    for (int k = 0; k < 2; ++k) {
        void* dinput = nullptr;
        if (FAILED(create(GetModuleHandleW(nullptr), 0x0800, *iids[k], &dinput, nullptr)) || !dinput) continue;
        void** divt = *(void***)dinput;
        typedef HRESULT(WINAPI * CreateDevice_t)(void*, REFGUID, void**, void*);
        void* dev = nullptr;
        if (SUCCEEDED(((CreateDevice_t)divt[3])(dinput, kGUID_SysKeyboard, &dev, nullptr)) && dev) {
            void** vt = *(void***)dev;
            bool known = false;
            for (int i = 0; i < g_diVtCount; ++i) known |= g_diVt[i].vt == vt;
            if (!known && g_diVtCount < 2) {
                DiVtable& d = g_diVt[g_diVtCount];
                d.vt = vt;
                d.state = (GetDeviceState_t)vt[9];
                d.data = (GetDeviceData_t)vt[10];
                ++g_diVtCount;
                PatchVtable(vt, 9, (void*)Hook_GetDeviceState);
                PatchVtable(vt, 10, (void*)Hook_GetDeviceData);
            }
            ((ULONG(WINAPI*)(void*))(*(void***)dev)[2])(dev);
        }
        ((ULONG(WINAPI*)(void*))divt[2])(dinput);
    }
    SR_Log("overlay: DirectInput hooked (%d device interface%s)", g_diVtCount, g_diVtCount == 1 ? "" : "s");
}

// ====================================================================== Direct3D 9

typedef HRESULT(WINAPI* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(WINAPI* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT(WINAPI* ScPresent_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
// originals per vtable (devices of different kinds have different vtables)
struct DevVt { void** vt; Present_t present; Reset_t reset; };
struct ScVt { void** vt; ScPresent_t present; };
DevVt g_devVt[8];
ScVt g_scVt[8];
volatile LONG g_nDevVt, g_nScVt;
CRITICAL_SECTION g_vtLock;
const DevVt* FindDevVt(void* self) {
    void** vt = *(void***)self;
    for (int i = 0; i < g_nDevVt; ++i) if (g_devVt[i].vt == vt) return &g_devVt[i];
    return nullptr;
}
const ScVt* FindScVt(void* self) {
    void** vt = *(void***)self;
    for (int i = 0; i < g_nScVt; ++i) if (g_scVt[i].vt == vt) return &g_scVt[i];
    return nullptr;
}
volatile LONG g_inDevicePresent;

// Area-average resample of an embedded icon (straight alpha, averaged premultiplied) into the atlas.
void ResampleIcon(const IconData& src, unsigned char* dst, int pitch, int dw, int dh) {
    float sx = (float)src.w / dw, sy = (float)src.h / dh;
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x) {
            float x0 = x * sx, x1 = x0 + (sx > 1 ? sx : 1), y0 = y * sy, y1 = y0 + (sy > 1 ? sy : 1);
            if (sx < 1) { x0 = (x + 0.5f) * sx - 0.5f; if (x0 < 0) x0 = 0; x1 = x0 + 1; }
            if (sy < 1) { y0 = (y + 0.5f) * sy - 0.5f; if (y0 < 0) y0 = 0; y1 = y0 + 1; }
            float acc[4] = {0, 0, 0, 0}, wsum = 0;
            for (int yy = (int)y0; yy < src.h && yy < y1; ++yy) {
                float wy = (yy + 1 < y1 ? yy + 1 : y1) - (yy > y0 ? yy : y0);
                for (int xx = (int)x0; xx < src.w && xx < x1; ++xx) {
                    float wx = (xx + 1 < x1 ? xx + 1 : x1) - (xx > x0 ? xx : x0);
                    const uint8_t* q = src.rgba + (yy * src.w + xx) * 4;
                    float wgt = wx * wy, a = q[3] * wgt;
                    acc[0] += q[0] * a; acc[1] += q[1] * a; acc[2] += q[2] * a; acc[3] += a;
                    wsum += wgt;
                }
            }
            unsigned char* o = dst + y * pitch + x * 4;
            if (acc[3] <= 0 || wsum <= 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            for (int c = 0; c < 3; ++c) { float v = acc[c] / acc[3]; o[c] = (unsigned char)(v > 255 ? 255 : v + 0.5f); }
            float al = acc[3] / wsum;
            o[3] = (unsigned char)(al > 255 ? 255 : al + 0.5f);
        }
}

// The game's fonts (embedded TTFs) and its button-prompt glyphs (embedded RGBA, put into ImGui's font atlas so they
// draw with the same texture). em = the layout unit: regular caps are 0.70 em (as tall as Segoe UI's at em),
// italic caps 0.60 em (the game's option names are a little smaller than its values), the Xbox A button 1.3x the
// regular caps (as in the game's footer).
void LoadFonts(float em) {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar ranges[] = {0x0020, 0x017F, 0};
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // static data
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    float regPx = (float)(int)(em * 0.70f / kFontRegularCapEm + 0.5f), itPx = (float)(int)(em * 0.60f / kFontItalicCapEm + 0.5f);
    g_fontReg = io.Fonts->AddFontFromMemoryTTF((void*)kFontRegular, (int)sizeof(kFontRegular), regPx, &cfg, ranges);
    g_fontIt = io.Fonts->AddFontFromMemoryTTF((void*)kFontItalic, (int)sizeof(kFontItalic), itPx, &cfg, ranges);
    if (!g_fontReg || !g_fontIt) {
        SR_Log("overlay: the game fonts could not be loaded - using ImGui's default font");
        io.Fonts->Clear();
        ImFontConfig d;
        d.SizePixels = em;
        g_fontReg = g_fontIt = io.Fonts->AddFontDefault(&d);
        g_regCap = g_itCap = em * 0.7f;
        return;
    }
    g_regCap = regPx * kFontRegularCapEm;
    g_itCap = itPx * kFontItalicCapEm;
    float unit = g_regCap * 1.3f;  // the Xbox A button's height
    int ids[kIconCount];
    for (int i = 0; i < kIconCount; ++i) {
        const IconData& d = kIcons[i];
        // Keyboard keycaps are drawn a third larger than the game's ratio, so the key names on them stay readable
        // at the overlay's text sizes.
        bool key = i >= IconKeyUp && i <= IconKeycapRight;
        int h = (int)(unit * d.rel * (key ? 1.35f : 1.0f) + 0.5f);
        if (h < 1) h = 1;
        int w = i == IconKeycapMiddle ? d.w : (int)((float)d.w * h / d.h + 0.5f);
        if (w < 1) w = 1;
        ids[i] = io.Fonts->AddCustomRectRegular(w, h);
        g_icons[i].w = (float)w;
        g_icons[i].h = (float)h;
    }
    if (!io.Fonts->Build()) { SR_Log("overlay: font atlas build failed"); return; }
    unsigned char* px = nullptr;
    int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);  // cached by the atlas; the DX11 backend uploads this buffer
    if (!px) return;
    for (int i = 0; i < kIconCount; ++i) {
        ImFontAtlasCustomRect* r = io.Fonts->GetCustomRectByIndex(ids[i]);
        if (!r || !r->IsPacked()) return;
        ResampleIcon(kIcons[i], px + (r->Y * tw + r->X) * 4, tw * 4, r->Width, r->Height);
        io.Fonts->CalcCustomRectUV(r, &g_icons[i].uv0, &g_icons[i].uv1);
        if (i == IconKeycapMiddle) {  // stretched: sample the middle column only, so the edges don't bleed in
            float du = (g_icons[i].uv1.x - g_icons[i].uv0.x) * 0.5f;
            g_icons[i].uv0.x += du * 0.9f;
            g_icons[i].uv1.x -= du * 0.9f;
        }
    }
    g_iconsReady = true;
}

// Edge-triggered input: keyboard (polled, only while the game window has focus) and the pad buttons.
struct Keys { bool menuKey, up, down, left, right, enter, esc, back; };
Keys g_prevKeys;
WORD g_prevPad;
bool g_keyWasDown[256];
LONG g_seenPadActivity;

// Which device the prompts show: a keyboard key going down (game window focused) switches to the keyboard, new pad
// input (counted in the XInput hook) to the controller. Mouse buttons are not counted.
void TrackInputDevice(bool focused) {
    bool keyPressed = false;
    for (int vk = 0x08; vk < 0xFF; ++vk) {
        bool d = focused && (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (d && !g_keyWasDown[vk]) keyPressed = true;
        g_keyWasDown[vk] = d;
    }
    LONG pad = g_padActivity;
    bool padPressed = pad != g_seenPadActivity;
    g_seenPadActivity = pad;
    int dev = g_inputDev;
    if (padPressed) dev = kDevPad;
    if (keyPressed) dev = kDevKeyboard;
    if (dev != g_inputDev) {
        g_inputDev = dev;
        if (g_menuOpen || SR_PickerVisible()) SR_Log("overlay: prompts switched to %s", dev == kDevPad ? "controller" : "keyboard");
    }
}

void HandleInput() {
    bool focused = GetForegroundWindow() == g_hwnd;
    TrackInputDevice(focused);
    auto down = [&](int vk) { return focused && (GetAsyncKeyState(vk) & 0x8000) != 0; };
    Keys k = {down(g_menuKey), down(VK_UP), down(VK_DOWN), down(VK_LEFT), down(VK_RIGHT),
              down(VK_RETURN), down(VK_ESCAPE), down(VK_BACK)};
    WORD pad = 0;
    for (int i = 0; i < 4; ++i) pad |= (WORD)g_padButtons[i];
    auto key = [&](bool Keys::*m) { return k.*m && !(g_prevKeys.*m); };
    auto btn = [&](WORD b) { return (pad & b) && !(g_prevPad & b); };

    if (!pad) g_waitPadRelease = false;
    bool picker = SR_PickerVisible();
    if (!g_menuOpen) {
        // HP2010: the menu key or the pad's Back / View button (kept from the game while the picker is shown)
        bool requested = InterlockedExchange(&g_menuRequest, 0) != 0;  // the footer prompt (sr_info.lua)
        if (picker && !InputBlocked() && (key(&Keys::menuKey) || btn(kPadBack) || requested)) OpenMenu();
    } else if (!picker) {
        CloseMenu(false);
    } else if (key(&Keys::enter) || btn(kPadA)) {
        CloseMenu(true);
    } else if (key(&Keys::esc) || key(&Keys::back) || key(&Keys::menuKey) || btn(kPadB) || btn(kPadStart) || btn(kPadBack)) {
        CloseMenu(false);
    } else {
        if (key(&Keys::up) || btn(kPadUp)) MoveRow(-1);
        if (key(&Keys::down) || btn(kPadDown)) MoveRow(1);
        if (key(&Keys::left) || btn(kPadLeft)) Step(-1);
        if (key(&Keys::right) || btn(kPadRight)) Step(1);
        if (btn(kPadLB)) SwitchTab(-1);
        if (btn(kPadRB)) SwitchTab(1);
        if (g_row >= RowCount()) g_row = RowCount() - 1;  // Custom -> Classic hides the charge rows
    }
    if (g_waitKeyRelease) {
        if (k.menuKey || k.esc || k.enter || k.back) g_blockUntil = Now() + 150;  // until released
        else g_waitKeyRelease = false;
    }
    g_prevKeys = k;
    g_prevPad = pad;
}

bool InitImGui(IDirect3DDevice9* dev) {
    HWND hwnd = nullptr;
    UINT bbw = 0, bbh = 0;
    IDirect3DSwapChain9* sc = nullptr;
    if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc) {
        D3DPRESENT_PARAMETERS pp;
        if (SUCCEEDED(sc->GetPresentParameters(&pp))) { hwnd = pp.hDeviceWindow; bbw = pp.BackBufferWidth; bbh = pp.BackBufferHeight; }
        sc->Release();
    }
    if (!hwnd) {
        D3DDEVICE_CREATION_PARAMETERS cp;
        if (SUCCEEDED(dev->GetCreationParameters(&cp))) hwnd = cp.hFocusWindow;
    }
    if (!hwnd) return false;
    g_hwnd = hwnd;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    float h = (float)(rc.bottom - rc.top);
    if (bbh) h = (float)bbh;
    Conditions ini = SR_GetConditions();
    int fontSize = ini.fontSize;
    g_menuKey = ini.menuKey;
    g_fontPx = fontSize > 0 ? (float)fontSize : h * 0.024f;
    if (g_fontPx < 16) g_fontPx = 16;
    if (g_fontPx > 44) g_fontPx = 44;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 8;
    st.FrameRounding = 5;
    st.WindowBorderSize = 1;
    st.ScaleAllSizes(g_fontPx / 16.0f);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 0.94f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.22f, 0.45f, 1.0f);
    LoadFonts(g_fontPx);
    if (g_realGetState) {
        XState xs;
        for (DWORD i = 0; i < 4; ++i)
            if (g_realGetState(i, &xs) == 0) { g_inputDev = kDevPad; break; }
    }
    bool ok = ImGui_ImplWin32_Init(g_hwnd) && ImGui_ImplDX9_Init(dev);
    g_device = dev;
    if (!ok) {
        SR_Log("overlay: ImGui initialisation failed");
        return false;
    }
    g_wndUnicode = IsWindowUnicode(g_hwnd) != 0;
    g_origWndProc = (WNDPROC)(g_wndUnicode ? SetWindowLongW(g_hwnd, GWL_WNDPROC, (LONG)OverlayWndProc)
                                           : SetWindowLongA(g_hwnd, GWL_WNDPROC, (LONG)OverlayWndProc));
    char k[16];
    SR_Log("overlay: ready on window %p (%ux%u back buffer, font %d px, game fonts %s, prompts %s, starting with %s), "
           "menu key %s or Back / View on the controller", (void*)g_hwnd, bbw, bbh, (int)g_fontPx,
           g_fontReg != g_fontIt ? "yes" : "no", g_iconsReady ? "yes" : "no", g_inputDev == kDevPad ? "controller" : "keyboard",
           KeyName(g_menuKey, k));
    return true;
}

void OnPresent(IDirect3DDevice9* dev) {
    EnterCriticalSection(&g_imguiLock);
    if (!g_imguiReady) {
        g_imguiReady = InitImGui(dev);
        if (!g_imguiReady) {
            LeaveCriticalSection(&g_imguiLock);
            return;
        }
    }
    if (dev != g_device) {  // the game created another device
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplDX9_Init(dev);
        g_device = dev;
        SR_Log("overlay: device changed, renderer rebound");
    }
    HandleInput();
    if (!g_menuOpen || dev->TestCooperativeLevel() != D3D_OK) {
        LeaveCriticalSection(&g_imguiLock);
        return;
    }
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) {
        LeaveCriticalSection(&g_imguiLock);
        return;
    }
    D3DSURFACE_DESC bd;
    bb->GetDesc(&bd);
    ImGui::GetIO().MouseDrawCursor = false;
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().DisplaySize = ImVec2((float)bd.Width, (float)bd.Height);
    ImGui::NewFrame();
    DrawMenu();
    ImGui::EndFrame();
    ImGui::Render();
    IDirect3DSurface9* oldRt = nullptr;
    D3DVIEWPORT9 oldVp;
    dev->GetViewport(&oldVp);
    dev->GetRenderTarget(0, &oldRt);
    dev->SetRenderTarget(0, bb);
    if (SUCCEEDED(dev->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        dev->EndScene();
    }
    dev->SetRenderTarget(0, oldRt);
    dev->SetViewport(&oldVp);
    if (oldRt) oldRt->Release();
    bb->Release();
    LeaveCriticalSection(&g_imguiLock);
}

volatile LONG g_firstFrameLogged;
HRESULT WINAPI Hook_Present(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w, const RGNDATA* r) {
    const DevVt* v = FindDevVt(dev);
    if (!v) return E_FAIL;
    if (!InterlockedExchange(&g_firstFrameLogged, 1)) SR_Log("overlay: first frame (device Present)");
    InterlockedIncrement(&g_inDevicePresent);
    OnPresent(dev);
    HRESULT hr = v->present(dev, a, b, w, r);
    InterlockedDecrement(&g_inDevicePresent);
    return hr;
}

HRESULT WINAPI Hook_ScPresent(IDirect3DSwapChain9* sc, const RECT* a, const RECT* b, HWND w, const RGNDATA* r, DWORD f) {
    const ScVt* v = FindScVt(sc);
    if (!v) return E_FAIL;
    if (!g_inDevicePresent) {
        if (!InterlockedExchange(&g_firstFrameLogged, 1)) SR_Log("overlay: first frame (swap chain Present)");
        IDirect3DDevice9* dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(&dev)) && dev) {
            OnPresent(dev);
            dev->Release();
        }
    }
    return v->present(sc, a, b, w, r, f);
}

HRESULT WINAPI Hook_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    const DevVt* v = FindDevVt(dev);
    if (!v) return E_FAIL;
    EnterCriticalSection(&g_imguiLock);
    bool mine = g_imguiReady && dev == g_device;
    if (mine) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = v->reset(dev, pp);
    if (mine && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    LeaveCriticalSection(&g_imguiLock);
    return hr;
}

// Patches the vtables of the game's own device (and its swap chain) once per vtable.
void HookDevice(IDirect3DDevice9* dev, const char* how) {
    EnterCriticalSection(&g_vtLock);
    void** vt = *(void***)dev;
    if (!FindDevVt(dev) && g_nDevVt < 8 && vt[17] != (void*)Hook_Present) {
        DevVt& d = g_devVt[g_nDevVt];
        d.vt = vt;
        d.present = (Present_t)vt[17];
        d.reset = (Reset_t)vt[16];
        InterlockedIncrement(&g_nDevVt);
        PatchVtable(vt, 16, (void*)Hook_Reset);
        PatchVtable(vt, 17, (void*)Hook_Present);
        SR_Log("overlay: device %p hooked (%s, vtable %p)", (void*)dev, how, (void*)vt);
    }
    IDirect3DSwapChain9* sc = nullptr;
    if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc) {
        void** svt = *(void***)sc;
        if (!FindScVt(sc) && g_nScVt < 8 && svt[3] != (void*)Hook_ScPresent) {
            ScVt& d = g_scVt[g_nScVt];
            d.vt = svt;
            d.present = (ScPresent_t)svt[3];
            InterlockedIncrement(&g_nScVt);
            PatchVtable(svt, 3, (void*)Hook_ScPresent);
        }
        sc->Release();
    }
    LeaveCriticalSection(&g_vtLock);
}

// The game's Direct3DCreate9 import -> IDirect3D9::CreateDevice (vtable 16) -> the device the game really uses.
typedef HRESULT(WINAPI* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef IDirect3D9*(WINAPI* Create9_t)(UINT);
struct D3dVt { void** vt; CreateDevice_t create; };
D3dVt g_d3dVt[4];
volatile LONG g_nD3dVt;
Create9_t g_realCreate9;

HRESULT WINAPI Hook_CreateDevice(IDirect3D9* d9, UINT ad, D3DDEVTYPE t, HWND w, DWORD fl, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    void** vt = *(void***)d9;
    CreateDevice_t orig = nullptr;
    for (int i = 0; i < g_nD3dVt; ++i) if (g_d3dVt[i].vt == vt) orig = g_d3dVt[i].create;
    if (!orig) return E_FAIL;
    HRESULT hr = orig(d9, ad, t, w, fl, pp, out);
    SR_Log("overlay: game CreateDevice (type %d, flags %08lX, %ux%u %s) -> %08lX", (int)t, (unsigned long)fl,
           pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0, pp && pp->Windowed ? "windowed" : "fullscreen",
           (unsigned long)hr);
    if (SUCCEEDED(hr) && out && *out) HookDevice(*out, "game CreateDevice");
    return hr;
}

IDirect3D9* WINAPI Hook_Direct3DCreate9(UINT sdk) {
    IDirect3D9* d9 = g_realCreate9(sdk);
    if (d9) {
        EnterCriticalSection(&g_vtLock);
        void** vt = *(void***)d9;
        bool known = false;
        for (int i = 0; i < g_nD3dVt; ++i) known |= g_d3dVt[i].vt == vt;
        if (!known && g_nD3dVt < 4 && vt[16] != (void*)Hook_CreateDevice) {
            g_d3dVt[g_nD3dVt].vt = vt;
            g_d3dVt[g_nD3dVt].create = (CreateDevice_t)vt[16];
            InterlockedIncrement(&g_nD3dVt);
            PatchVtable(vt, 16, (void*)Hook_CreateDevice);
        }
        LeaveCriticalSection(&g_vtLock);
        SR_Log("overlay: game Direct3DCreate9 -> %p", (void*)d9);
    }
    return d9;
}

bool HookD3D() {
    void** slot = FindImport("d3d9.dll", "Direct3DCreate9", 0);
    if (!slot || !*slot) return false;
    g_realCreate9 = (Create9_t)*slot;
    PatchPointer(slot, (void*)Hook_Direct3DCreate9);
    return true;
}

DWORD WINAPI StartThread(LPVOID) {
    HookDirectInput();
    return 0;
}

}  // namespace

void SR_RequestMenu() { InterlockedExchange(&g_menuRequest, 1); }

void Overlay_Start() {
    InitializeCriticalSection(&g_imguiLock);
    InitializeCriticalSection(&g_vtLock);
    HookXInput();
    HookGetAsyncKeyState();
    // before the game creates its device (DllMain runs before the game's own code)
    if (HookD3D()) SR_Log("overlay: Direct3DCreate9 import hooked, waiting for the game's device");
    else SR_Log("overlay: Direct3DCreate9 import not found - the Race Settings menu is unavailable (the ini still works)");
    // d3d9 / dinput objects are not created under the loader lock: the rest runs once DllMain has returned
    CreateThread(nullptr, 0, StartThread, nullptr, 0, nullptr);
}
