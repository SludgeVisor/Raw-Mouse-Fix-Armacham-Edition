/*
 * F.E.A.R. 2: Project Origin - true raw mouse input fix
 * Built as a proxy for msacm32.dll (which FEAR2.exe imports), so it is
 * loaded automatically when the game starts.
 *
 * How the stock game reads the mouse (reverse engineered from FEAR2.exe):
 *   - The engine's mouse device (CLTInput) stores the last WM_MOUSEMOVE
 *     position, and GetAxisValue(1003/1004) returns
 *         (cursor position) - (client area centre)
 *     in whole pixels, i.e. AFTER Windows pointer ballistics.
 *   - Once per frame the engine recentres the cursor with SetCursorPos.
 *   Result: acceleration from "Enhance pointer precision", sub-pixel motion is
 *   thrown away (dead zone) and recentre/WM_MOUSEMOVE races at high FPS.
 *
 * What this DLL does:
 *   - Registers for Raw Input (WM_INPUT) on a hidden thread and accumulates
 *     the unfiltered integer counts from the mouse.
 *   - Detours the mouse GetAxisValue for X/Y so the engine gets those counts
 *     (optionally scaled) instead of the cursor-derived pixel delta.
 *   - Uses the engine's per-frame SetCursorPos call as the frame boundary.
 *
 * Freestanding build (no CRT, no Windows SDK headers needed).
 */

/* ---------- minimal Windows declarations ---------- */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef long               i32;      /* LONG is 32-bit on Win32 */
typedef unsigned long      ul32;
typedef long long          i64;
typedef unsigned int       uptr;     /* 32-bit target */
typedef void              *HANDLE, *HMODULE, *HWND, *HINSTANCE, *LPVOID;
typedef int                BOOL;
typedef u32                DWORD;
typedef u16                WORD;
typedef uptr               WPARAM;
typedef i32                LPARAM, LRESULT;
typedef u16                ATOM;

#define WINAPI __stdcall
#define TRUE 1
#define FALSE 0
#define NULL ((void*)0)

typedef struct { i32 x, y; } POINT;
typedef struct { HWND hwnd; u32 message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef LRESULT (WINAPI *WNDPROC)(HWND, u32, WPARAM, LPARAM);
typedef struct {
    u32 cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance; HANDLE hIcon, hCursor, hbrBackground;
    const char *lpszMenuName, *lpszClassName; HANDLE hIconSm;
} WNDCLASSEXA;
typedef struct { u16 usUsagePage, usUsage; DWORD dwFlags; HWND hwndTarget; } RAWINPUTDEVICE;
typedef struct { DWORD dwType, dwSize; HANDLE hDevice; WPARAM wParam; } RAWINPUTHEADER;
typedef struct {
    u16 usFlags; u16 _pad;
    ul32 ulButtons; ul32 ulRawButtons; i32 lLastX; i32 lLastY; ul32 ulExtraInformation;
} RAWMOUSE;
typedef struct { RAWINPUTHEADER header; union { RAWMOUSE mouse; u8 raw[64]; } data; } RAWINPUT;
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(void *);

/* kernel32 */
HMODULE WINAPI GetModuleHandleA(const char *);
BOOL    WINAPI VirtualProtect(void *, uptr, DWORD, DWORD *);
void   *WINAPI VirtualAlloc(void *, uptr, DWORD, DWORD);
BOOL    WINAPI FlushInstructionCache(HANDLE, const void *, uptr);
HANDLE  WINAPI GetCurrentProcess(void);
DWORD   WINAPI GetCurrentProcessId(void);
HANDLE  WINAPI CreateThread(void *, uptr, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *);
BOOL    WINAPI CloseHandle(HANDLE);
BOOL    WINAPI QueryPerformanceCounter(i64 *);
BOOL    WINAPI QueryPerformanceFrequency(i64 *);
HMODULE WINAPI LoadLibraryA(const char *);
void   *WINAPI GetProcAddress(HMODULE, const char *);
u32     WINAPI GetSystemDirectoryA(char *, u32);
DWORD   WINAPI GetModuleFileNameA(HMODULE, char *, DWORD);
DWORD   WINAPI GetPrivateProfileStringA(const char *, const char *, const char *, char *, DWORD, const char *);
u32     WINAPI GetPrivateProfileIntA(const char *, const char *, int, const char *);
BOOL    WINAPI DisableThreadLibraryCalls(HMODULE);
HANDLE  WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL    WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
BOOL    WINAPI SetThreadPriority(HANDLE, int);
HANDLE  WINAPI GetCurrentThread(void);
void    WINAPI Sleep(DWORD);
/* user32 */
ATOM    WINAPI RegisterClassExA(const WNDCLASSEXA *);
HWND    WINAPI CreateWindowExA(DWORD, const char *, const char *, DWORD, int, int, int, int, HWND, HANDLE, HINSTANCE, void *);
LRESULT WINAPI DefWindowProcA(HWND, u32, WPARAM, LPARAM);
BOOL    WINAPI GetMessageA(MSG *, HWND, u32, u32);
LRESULT WINAPI DispatchMessageA(const MSG *);
BOOL    WINAPI RegisterRawInputDevices(const RAWINPUTDEVICE *, u32, u32);
u32     WINAPI GetRawInputData(HANDLE, u32, void *, u32 *, u32);
HWND    WINAPI GetForegroundWindow(void);
DWORD   WINAPI GetWindowThreadProcessId(HWND, DWORD *);
BOOL    WINAPI SetCursorPos(int, int);
BOOL    WINAPI SystemParametersInfoA(u32, u32, void *, u32);

#define WM_INPUT              0x00FF
#define SPI_GETMOUSESPEED     0x0070
#define RID_INPUT             0x10000003u
#define RIDEV_INPUTSINK       0x00000100u
#define RIM_TYPEMOUSE         0
#define MOUSE_MOVE_ABSOLUTE   0x01
#define HWND_MESSAGE          ((HWND)(i32)-3)
#define PAGE_EXECUTE_READWRITE 0x40
#define MEM_COMMIT_RESERVE    0x3000
#define DLL_PROCESS_ATTACH    1
#define GENERIC_WRITE         0x40000000u
#define CREATE_ALWAYS         2
#define THREAD_PRIORITY_HIGHEST 2

/* ---------- configuration ---------- */
static int   g_enabled = 1;
static int   g_log     = 1;
static float g_scaleX  = 1.0f;
static float g_scaleY  = 1.0f;
static float g_master  = 1.0f;           /* 'Sensitivity' from the ini */
static float g_speed   = 1.0f;           /* Windows pointer-speed factor (if MatchWindowsSpeed=1) */
static int   g_noSmooth = 0;             /* ForceNoSmoothing */
static int   g_ignoreSens = 0;           /* IgnoreGameSensitivity */
static char  g_dir[260];            /* directory of this DLL (with trailing \) */
static HINSTANCE g_hInst;

/* ---------- tiny helpers (no CRT) ---------- */
void *memset(void *d, int c, uptr n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, uptr n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }

static uptr slen(const char *s) { uptr n = 0; while (s[n]) n++; return n; }
static void scat(char *d, const char *s) { uptr n = slen(d); while ((d[n++] = *s++)) {} }

static float parse_float(const char *s, float def)
{
    float v = 0, f = 0.1f; int neg = 0, any = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { v += (*s - '0') * f; f *= 0.1f; s++; any = 1; } }
    if (!any) return def;
    return neg ? -v : v;
}

static void Log(const char *msg)
{
    char path[300]; HANDLE h; DWORD w;
    static HANDLE hlog = NULL; static int tried = 0;
    if (!g_log) return;
    if (!tried) {
        tried = 1; path[0] = 0; scat(path, g_dir); scat(path, "FEAR2RawMouse.log");
        h = CreateFileA(path, GENERIC_WRITE, 1 /*FILE_SHARE_READ*/, NULL, CREATE_ALWAYS, 0x80, NULL);
        if (h != (HANDLE)(i32)-1) hlog = h;
    }
    if (hlog) { WriteFile(hlog, msg, (DWORD)slen(msg), &w, NULL); WriteFile(hlog, "\r\n", 2, &w, NULL); }
}

/* ---------- raw input state ---------- */
static volatile i32 g_pendX, g_pendY;     /* written by raw thread, drained by game thread */
static volatile int g_hasRaw;             /* set once a relative raw event was seen         */
static volatile int g_fg = 1;             /* cached "our process is foreground"             */
static i64 g_fgStamp;
static i64 g_qpf = 1;
static i64 g_fgTicks = 1;
int _fltused;

static i64 qpc(void) { i64 t = 0; QueryPerformanceCounter(&t); return t; }

static int is_foreground(void)
{
    i64 now = qpc();
    if (now - g_fgStamp > g_fgTicks) {             /* refresh every ~20 ms */
        DWORD pid = 0; HWND w = GetForegroundWindow();
        if (w) GetWindowThreadProcessId(w, &pid);
        g_fg = (pid == GetCurrentProcessId());
        g_fgStamp = now;
    }
    return g_fg;
}

static LRESULT WINAPI RawWndProc(HWND h, u32 m, WPARAM w, LPARAM l)
{
    if (m == WM_INPUT) {
        RAWINPUT ri; u32 size = sizeof(ri);
        if (GetRawInputData((HANDLE)l, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (u32)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE &&
            !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
        {
            i32 dx = ri.data.mouse.lLastX, dy = ri.data.mouse.lLastY;
            g_hasRaw = 1;
            if ((dx || dy) && is_foreground()) {
                __atomic_fetch_add(&g_pendX, dx, __ATOMIC_RELAXED);
                __atomic_fetch_add(&g_pendY, dy, __ATOMIC_RELAXED);
            }
        }
        return DefWindowProcA(h, m, w, l);          /* required cleanup for WM_INPUT */
    }
    return DefWindowProcA(h, m, w, l);
}

static DWORD WINAPI RawThread(void *arg)
{
    WNDCLASSEXA wc; HWND hw; RAWINPUTDEVICE rid; MSG msg; (void)arg;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = RawWndProc; wc.hInstance = g_hInst;
    wc.lpszClassName = "FEAR2RawMouseWnd";
    RegisterClassExA(&wc);
    hw = CreateWindowExA(0, "FEAR2RawMouseWnd", "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_hInst, NULL);
    if (!hw) { Log("CreateWindowEx failed"); return 1; }
    rid.usUsagePage = 1; rid.usUsage = 2; rid.dwFlags = RIDEV_INPUTSINK; rid.hwndTarget = hw;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) { Log("RegisterRawInputDevices failed"); return 1; }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    Log("raw input registered");
    while (GetMessageA(&msg, NULL, 0, 0) > 0) DispatchMessageA(&msg);
    return 0;
}

/* ---------- game-side hooks ---------- */
typedef float (__attribute__((thiscall)) *GetAxisFn)(void *self, int id);
static GetAxisFn g_origGetAxis;

static int   g_frozen;
static i64   g_snapT, g_lastReadT;
static float g_frameX, g_frameY;
static i64   g_freezeTicks, g_gapTicks;

static void take_snapshot(i64 now)
{
    i32 dx = __atomic_exchange_n(&g_pendX, 0, __ATOMIC_RELAXED);
    i32 dy = __atomic_exchange_n(&g_pendY, 0, __ATOMIC_RELAXED);
    if (now - g_lastReadT > g_gapTicks) { dx = 0; dy = 0; }   /* nobody was reading (menu/loading): drop stale motion */
    { float m = g_master * g_speed;
      g_frameX = (float)dx * g_scaleX * m;
      g_frameY = (float)dy * g_scaleY * m; }
    g_snapT = now; g_frozen = 1;
}

/* Replaces CLTInput mouse device GetAxisValue (FEAR2.exe 0x46C781).
 * ids 1003 / 1004 are mouse X / Y movement. Everything else is passed through. */
static float __attribute__((thiscall)) hk_GetAxis(void *self, int id)
{
    int a = id - 1000;
    if ((a == 3 || a == 4) && g_hasRaw) {
        i64 now = qpc();
        if (!g_frozen || (now - g_snapT) > g_freezeTicks) take_snapshot(now);
        g_lastReadT = now;
        return (a == 3) ? g_frameX : g_frameY;
    }
    return g_origGetAxis(self, id);
}

/* The engine's only SetCursorPos call (recentre). Marks the frame boundary. */
static int WINAPI hk_SetCursorPos(int x, int y)
{
    g_frozen = 0;
    return SetCursorPos(x, y);
}

static int patch_write(u8 *dst, const u8 *src, uptr n)
{
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(dst, src, n);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return 1;
}

static int bytes_eq(const u8 *a, const u8 *b, uptr n) { while (n--) if (*a++ != *b++) return 0; return 1; }

/* Preferred image base of the verified FEAR2.exe is 0x400000 and it is not ASLR'd;
 * RVAs are used anyway so a relocated image still works. */
#define RVA_GETAXIS     0x6C781u   /* VA 0x46C781 */
#define RVA_SETCURSOR   0x69105u   /* VA 0x469105: call dword ptr [SetCursorPos IAT] */
#define RVA_SETCURSOR_IAT 0x26D604u

static int install_hooks(void)
{
    u8 *base = (u8 *)GetModuleHandleA(NULL);
    u8 *getaxis = base + RVA_GETAXIS;
    u8 *scp     = base + RVA_SETCURSOR;
    static const u8 sig_getaxis[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x8B, 0x45, 0x08, 0xD9, 0xEE };
    u8 sig_scp[8]; u32 iat = (u32)(base + RVA_SETCURSOR_IAT);
    u8 tramp_src[16], jmp[6], call[6];
    u8 *tramp; u32 rel;

    sig_scp[0] = 0xFF; sig_scp[1] = 0x15; memcpy(sig_scp + 2, &iat, 4); sig_scp[6] = 0x33; sig_scp[7] = 0xC0;

    if (!bytes_eq(getaxis, sig_getaxis, sizeof(sig_getaxis))) { Log("GetAxisValue signature mismatch - unsupported FEAR2.exe, not patching"); return 0; }
    if (!bytes_eq(scp, sig_scp, 8))                          { Log("SetCursorPos call signature mismatch - unsupported FEAR2.exe, not patching"); return 0; }

    /* trampoline: original 6 bytes + jmp back */
    tramp = (u8 *)VirtualAlloc(NULL, 32, MEM_COMMIT_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) { Log("VirtualAlloc failed"); return 0; }
    memcpy(tramp_src, getaxis, 6);
    memcpy(tramp, tramp_src, 6);
    tramp[6] = 0xE9; rel = (u32)((getaxis + 6) - (tramp + 11)); memcpy(tramp + 7, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), tramp, 32);
    g_origGetAxis = (GetAxisFn)tramp;

    /* detour GetAxisValue: jmp hk_GetAxis ; nop */
    jmp[0] = 0xE9; rel = (u32)((u8 *)hk_GetAxis - (getaxis + 5)); memcpy(jmp + 1, &rel, 4); jmp[5] = 0x90;
    /* patch the SetCursorPos call: call hk_SetCursorPos ; nop  (same 6 byte footprint) */
    call[0] = 0xE8; rel = (u32)((u8 *)hk_SetCursorPos - (scp + 5)); memcpy(call + 1, &rel, 4); call[5] = 0x90;

    if (!patch_write(getaxis, jmp, 6)) { Log("patch GetAxisValue failed"); return 0; }
    if (!patch_write(scp, call, 6))    { Log("patch SetCursorPos failed");  return 0; }
    Log("hooks installed");
    return 1;
}

/* ---------- ForceNoSmoothing: GameClient.dll ----------
 * GameClient's mouse-look function (RVA 0xBFE20) blends each frame's turn with the
 * previous frame's, weighted by the profile "InputRate" option. The weight is
 * computed at RVA 0xC0020..0xC0028:
 *     fld [esp+2Ch] ; fmul [esp+0Ch] ; fstp [esp+2Ch]
 * Replacing "fmul [esp+0Ch]" (D8 4C 24 0C) with "fstp st(0) ; fldz" (DD D8 D9 EE)
 * forces that weight to exactly 0 -> output = this frame's raw turn, no smoothing. */
#define RVA_SMOOTH 0xC0020u
static int patch_smoothing(u8 *mod)
{
    static const u8 orig[]  = { 0xD9,0x44,0x24,0x2C, 0xD8,0x4C,0x24,0x0C, 0xD9,0x5C,0x24,0x2C, 0xD9,0x44,0x24,0x1C, 0xD8,0xE2 };
    static const u8 patched[] = { 0xDD, 0xD8, 0xD9, 0xEE };
    u8 *site = mod + RVA_SMOOTH;
    if (bytes_eq(site + 4, patched, 4)) return 1;                 /* already done */
    if (!bytes_eq(site, orig, sizeof(orig))) return -1;           /* different GameClient build */
    return patch_write(site + 4, patched, 4) ? 1 : 0;
}

/* ---------- IgnoreGameSensitivity: GameClient.dll ----------
 * The Options-menu "Aim Sensitivity" slider is stored in the profile (+0x7E4) and applied in the
 * same mouse-look function at RVA 0xBFF29:  fmul dword ptr [edi+4]  (D8 4F 04).
 * Replacing those 3 bytes with NOPs multiplies by 1 instead, so only the ini Sensitivity counts. */
#define RVA_SENS 0xBFF25u
static int patch_sens(u8 *mod)
{
    static const u8 orig[]    = { 0xD8,0x4C,0x24,0x10, 0xD8,0x4F,0x04, 0xD9,0x5C,0x24,0x30 };
    static const u8 patched[] = { 0x90, 0x90, 0x90 };
    u8 *site = mod + RVA_SENS;
    if (bytes_eq(site + 4, patched, 3)) return 1;
    if (!bytes_eq(site, orig, sizeof(orig))) return -1;
    return patch_write(site + 4, patched, 3) ? 1 : 0;
}

static DWORD WINAPI SmoothThread(void *arg)
{
    HMODULE last = NULL; (void)arg;
    for (;;) {                                  /* GameClient.dll is loaded by the engine at runtime */
        HMODULE h = GetModuleHandleA("GameClient.dll");
        if (h && h != last) {
            if (g_noSmooth) {
                int r = patch_smoothing((u8 *)h);
                Log(r == 1 ? "GameClient mouse smoothing disabled" :
                    r < 0  ? "GameClient.dll signature mismatch - smoothing not patched" :
                             "GameClient smoothing patch failed");
            }
            if (g_ignoreSens) {
                int r = patch_sens((u8 *)h);
                Log(r == 1 ? "game aim-sensitivity option ignored" :
                    r < 0  ? "GameClient.dll signature mismatch - sensitivity not patched" :
                             "GameClient sensitivity patch failed");
            }
            last = h;
        } else if (!h) last = NULL;
        Sleep(500);
    }
    return 0;
}

/* ---------- msacm32.dll proxy ----------
 * FEAR2.exe imports msacm32.dll (audio codec manager); a copy in the game folder
 * is loaded instead of the system one. Every export is a naked jmp thunk to the
 * real System32 msacm32, so arguments / calling convention pass through untouched. */
#define NEXP 43
void *g_real[NEXP];
static HMODULE g_realDll;
static const char *const g_names[NEXP] = {
    "acmDriverAddA", "acmDriverAddW", "acmDriverClose", "acmDriverDetailsA",
    "acmDriverDetailsW", "acmDriverEnum", "acmDriverID", "acmDriverMessage",
    "acmDriverOpen", "acmDriverPriority", "acmDriverRemove", "acmFilterChooseA",
    "acmFilterChooseW", "acmFilterDetailsA", "acmFilterDetailsW", "acmFilterEnumA",
    "acmFilterEnumW", "acmFilterTagDetailsA", "acmFilterTagDetailsW", "acmFilterTagEnumA",
    "acmFilterTagEnumW", "acmFormatChooseA", "acmFormatChooseW", "acmFormatDetailsA",
    "acmFormatDetailsW", "acmFormatEnumA", "acmFormatEnumW", "acmFormatSuggest",
    "acmFormatTagDetailsA", "acmFormatTagDetailsW", "acmFormatTagEnumA", "acmFormatTagEnumW",
    "acmGetVersion", "acmMessage32", "acmMetrics", "acmStreamClose",
    "acmStreamConvert", "acmStreamMessage", "acmStreamOpen", "acmStreamPrepareHeader",
    "acmStreamReset", "acmStreamSize", "acmStreamUnprepareHeader"
};

void __cdecl ensure_real(void)
{
    char path[300]; int i;
    if (g_realDll) return;
    path[0] = 0;
    GetSystemDirectoryA(path, 260);
    scat(path, "\\msacm32.dll");
    g_realDll = LoadLibraryA(path);
    if (g_realDll) for (i = 0; i < NEXP; i++) g_real[i] = GetProcAddress(g_realDll, g_names[i]);
}

#define THUNK(name, off) \
    __attribute__((naked)) void name(void) { __asm__ volatile("call _ensure_real\n\tjmpl *_g_real+" #off); }
THUNK(acmDriverAddA, 0)
THUNK(acmDriverAddW, 4)
THUNK(acmDriverClose, 8)
THUNK(acmDriverDetailsA, 12)
THUNK(acmDriverDetailsW, 16)
THUNK(acmDriverEnum, 20)
THUNK(acmDriverID, 24)
THUNK(acmDriverMessage, 28)
THUNK(acmDriverOpen, 32)
THUNK(acmDriverPriority, 36)
THUNK(acmDriverRemove, 40)
THUNK(acmFilterChooseA, 44)
THUNK(acmFilterChooseW, 48)
THUNK(acmFilterDetailsA, 52)
THUNK(acmFilterDetailsW, 56)
THUNK(acmFilterEnumA, 60)
THUNK(acmFilterEnumW, 64)
THUNK(acmFilterTagDetailsA, 68)
THUNK(acmFilterTagDetailsW, 72)
THUNK(acmFilterTagEnumA, 76)
THUNK(acmFilterTagEnumW, 80)
THUNK(acmFormatChooseA, 84)
THUNK(acmFormatChooseW, 88)
THUNK(acmFormatDetailsA, 92)
THUNK(acmFormatDetailsW, 96)
THUNK(acmFormatEnumA, 100)
THUNK(acmFormatEnumW, 104)
THUNK(acmFormatSuggest, 108)
THUNK(acmFormatTagDetailsA, 112)
THUNK(acmFormatTagDetailsW, 116)
THUNK(acmFormatTagEnumA, 120)
THUNK(acmFormatTagEnumW, 124)
THUNK(acmGetVersion, 128)
THUNK(acmMessage32, 132)
THUNK(acmMetrics, 136)
THUNK(acmStreamClose, 140)
THUNK(acmStreamConvert, 144)
THUNK(acmStreamMessage, 148)
THUNK(acmStreamOpen, 152)
THUNK(acmStreamPrepareHeader, 156)
THUNK(acmStreamReset, 160)
THUNK(acmStreamSize, 164)
THUNK(acmStreamUnprepareHeader, 168)

/* ---------- entry ---------- */
static void load_config(void)
{
    char ini[300], buf[64]; uptr n;
    GetModuleFileNameA(g_hInst, g_dir, 260);
    n = slen(g_dir); while (n && g_dir[n - 1] != '\\') n--; g_dir[n] = 0;
    ini[0] = 0; scat(ini, g_dir); scat(ini, "FEAR2RawMouse.ini");
    g_enabled = (int)GetPrivateProfileIntA("RawMouse", "Enabled", 1, ini);
    g_log     = (int)GetPrivateProfileIntA("RawMouse", "Log", 1, ini);
    GetPrivateProfileStringA("RawMouse", "ScaleX", "1.0", buf, sizeof(buf), ini); g_scaleX = parse_float(buf, 1.0f);
    GetPrivateProfileStringA("RawMouse", "ScaleY", "1.0", buf, sizeof(buf), ini); g_scaleY = parse_float(buf, 1.0f);
    GetPrivateProfileStringA("RawMouse", "Sensitivity", "1.0", buf, sizeof(buf), ini); g_master = parse_float(buf, 1.0f);
    g_noSmooth = (int)GetPrivateProfileIntA("RawMouse", "ForceNoSmoothing", 1, ini);
    g_ignoreSens = (int)GetPrivateProfileIntA("RawMouse", "IgnoreGameSensitivity", 0, ini);
    if (GetPrivateProfileIntA("RawMouse", "MatchWindowsSpeed", 1, ini)) {
        /* Windows pointer-speed slider (1..20, default 10) scale factors, acceleration ignored */
        static const float tbl[20] = { 0.03125f, 0.0625f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f,
                                       1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f, 3.25f, 3.5f };
        int spd = 10;
        SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &spd, 0);
        if (spd >= 1 && spd <= 20) g_speed = tbl[spd - 1];
        { char m[48]; m[0] = 0; scat(m, "windows pointer speed "); m[22] = (char)('0' + spd / 10); m[23] = (char)('0' + spd % 10); m[24] = 0; Log(m); }
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE th;
        g_hInst = inst;
        DisableThreadLibraryCalls(inst);
        load_config();
        Log("FEAR2 raw mouse fix loaded");
        if (!g_enabled) { Log("disabled in ini"); return TRUE; }
        { i64 f = 0; QueryPerformanceFrequency(&f); g_qpf = f ? f : 1; }
        g_freezeTicks = (i64)((double)g_qpf * 0.0015);   /* 1.5 ms: reads within one frame get the same value */
        g_gapTicks    = (i64)((double)g_qpf * 0.25);     /* 250 ms without reads: discard stale motion        */
        g_fgTicks     = (i64)((double)g_qpf * 0.02);     /* re-check foreground at most every 20 ms           */
        if (install_hooks()) {
            th = CreateThread(NULL, 0, RawThread, NULL, 0, NULL);
            if (th) CloseHandle(th);
        }
        if (g_noSmooth || g_ignoreSens) { th = CreateThread(NULL, 0, SmoothThread, NULL, 0, NULL); if (th) CloseHandle(th); }
    }
    return TRUE;
}
