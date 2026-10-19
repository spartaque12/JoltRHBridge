/*
 * JoltRHBridge.asi - makes plugin-sdk's renderSceneEvent and gameProcessEvent (used by
 * JoltPhysics.SA.asi) fire under gtaRenderHook.
 *
 * gtaRenderHook replaces the game's Idle (0x53E920) with its own deferred renderer, so the
 * calls inside the original Idle where plugin-sdk hooks those events never run:
 *   0x53E981 call CGame::Process  (gameProcessEvent)
 *   0x53EABF call RenderScene     (renderSceneEvent)
 *
 * renderSceneEvent: RenderHook still calls Render2dStuff through the call target at 0x53EB12,
 * read every frame. We hook that call; when it comes from RenderHook (not from the original
 * Idle) we run the 0x53EABF hook chain with RenderScene temporarily turned into a bare `ret`,
 * so subscribers get their frame without the scene being drawn twice.
 *
 * gameProcessEvent: RenderHook calls CGame::Process through function pointers it fills with
 * 0x53BEE0 at startup. We repoint those at a function that runs the 0x53E981 hook chain
 * instead, which calls CGame::Process once and then every gameProcessEvent subscriber.
 */
#include <windows.h>
#include <intrin.h>
#include <stdio.h>

#define ADDR_RENDERSCENE_CALL 0x53EABF /* call RenderScene inside Idle (renderSceneEvent) */
#define ADDR_RENDER2D_CALL    0x53EB12 /* call Render2dStuff inside Idle */
#define ADDR_RENDERSCENE      0x53DF40 /* RenderScene() */
#define ADDR_GAMEPROCESS_CALL 0x53E981 /* call CGame::Process inside Idle (gameProcessEvent) */
#define ADDR_GAMEPROCESS      0x53BEE0 /* CGame::Process() */
#define EXE_CODE_BEGIN        0x401000
#define EXE_CODE_END          0x857000

typedef int(__cdecl *Render2dStuff_t)(void);
typedef void(__cdecl *RenderScene_t)(void);

static Render2dStuff_t g_prevRender2d;
static LONG g_fired;
static FILE *g_log;

static DWORD CallTarget(DWORD site)
{
    return site + 5 + *(int *)(site + 1);
}

static void Log(const char *msg, DWORD a, DWORD b)
{
    if (!g_log) return;
    fprintf(g_log, msg, a, b);
    fputc('\n', g_log);
    fflush(g_log);
}

static void FireRenderSceneEvent(void)
{
    BYTE *rs = (BYTE *)ADDR_RENDERSCENE;
    DWORD head, old;
    BYTE saved;

    if (*(BYTE *)ADDR_RENDERSCENE_CALL != 0xE8) return;
    head = CallTarget(ADDR_RENDERSCENE_CALL);
    if (head == ADDR_RENDERSCENE) return; /* nobody subscribed */

    VirtualProtect(rs, 1, PAGE_EXECUTE_READWRITE, &old);
    saved = *rs;
    *rs = 0xC3; /* ret: RenderHook already drew the scene */
    FlushInstructionCache(GetCurrentProcess(), rs, 1);

    ((RenderScene_t)head)();

    *rs = saved;
    FlushInstructionCache(GetCurrentProcess(), rs, 1);
    VirtualProtect(rs, 1, old, &old);

    if (!InterlockedExchange(&g_fired, 1))
        Log("first renderSceneEvent fired via chain head 0x%08X", head, 0);
}

static void __cdecl GameProcessHook(void)
{
    DWORD head = ADDR_GAMEPROCESS;
    if (*(BYTE *)ADDR_GAMEPROCESS_CALL == 0xE8)
        head = CallTarget(ADDR_GAMEPROCESS_CALL); /* ends in the real CGame::Process */
    ((RenderScene_t)head)();
}

/* Repoint RenderHook's CGame::Process pointers (writable data holding 0x53BEE0) at our hook. */
static void PatchRenderHookGameProcess(void)
{
    HMODULE rh = GetModuleHandleA("_gtaRenderHook.asi");
    BYTE *p, *end;
    MEMORY_BASIC_INFORMATION mbi;
    DWORD patched = 0;

    if (!rh) { Log("_gtaRenderHook.asi not loaded, gameProcessEvent not bridged", 0, 0); return; }
    p = (BYTE *)rh;
    end = p + ((IMAGE_NT_HEADERS *)(p + ((IMAGE_DOS_HEADER *)p)->e_lfanew))->OptionalHeader.SizeOfImage;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
        BYTE *r = (BYTE *)mbi.BaseAddress, *rend = r + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY))) {
            DWORD *d;
            for (d = (DWORD *)r; (BYTE *)(d + 1) <= rend && (BYTE *)(d + 1) <= end; d++)
                if (*d == ADDR_GAMEPROCESS) { *d = (DWORD)(UINT_PTR)GameProcessHook; patched++; }
        }
        p = rend;
    }
    Log("repointed %u RenderHook CGame::Process pointer(s) to the 0x53E981 chain head 0x%08X",
        patched, CallTarget(ADDR_GAMEPROCESS_CALL));
}

static int __cdecl Render2dStuffHook(void)
{
    static LONG rhPatched;
    DWORD ret = (DWORD)(UINT_PTR)_ReturnAddress();
    /* From the original Idle the events already fired; only bridge RenderHook's Idle. */
    if (ret < EXE_CODE_BEGIN || ret >= EXE_CODE_END) {
        if (!InterlockedExchange(&rhPatched, 1))
            PatchRenderHookGameProcess(); /* every mod is loaded and hooked by now */
        FireRenderSceneEvent();
    }
    return g_prevRender2d();
}

static void Install(HMODULE self)
{
    char path[MAX_PATH], *slash;
    DWORD site = ADDR_RENDER2D_CALL, old;

    GetModuleFileNameA(self, path, MAX_PATH);
    slash = strrchr(path, '\\');
    strcpy(slash ? slash + 1 : path, "JoltRHBridge.log");
    g_log = fopen(path, "w");

    if (*(BYTE *)site != 0xE8) {
        Log("0x%08X is not a call (byte 0x%02X), not installing", site, *(BYTE *)site);
        return;
    }
    g_prevRender2d = (Render2dStuff_t)CallTarget(site);

    VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int *)(site + 1) = (int)((DWORD)(UINT_PTR)Render2dStuffHook - site - 5);
    VirtualProtect((void *)site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, 5);

    Log("hooked Render2dStuff call at 0x%08X (previous target 0x%08X)", site, (DWORD)(UINT_PTR)g_prevRender2d);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Install(inst);
    }
    return TRUE;
}
