// Shadow the Hedgehog (USA) default.xbe: game-side replacements.
// A strong X86_FUNC(sub_XXXXXXXX) replaces the recompiled function at that address.

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#include "app/app.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// ---------------------------------------------------------------------------------------------------------------
// DirectSound exports the SDK's signatures did not name. Their console versions program the Xbox APU directly,
// which does not exist here. Identified from the .XTLID export table by id ordering, argument counts checked
// against each function's `ret N`.
// ---------------------------------------------------------------------------------------------------------------

// IDirectSound_SetAllParameters(this, const DS3DLISTENER*, dwApply) - .XTLID 0xB0046
// 3D listener parameters. The host mixer does not position sounds in 3D yet; accept and ignore.
X86_FUNC(sub_003E577A) {
    (void)base;
    kernel::Return(ctx, 0, 3);
}

// IDirectSoundBuffer_SetConeAngles(this, dwInside, dwOutside, dwApply) - .XTLID 0xB0061
X86_FUNC(sub_003E4932) {
    (void)base;
    kernel::Return(ctx, 0, 4);
}

// IDirectSoundBuffer_SetPitch(this, LONG lPitch) - .XTLID 0xB0076
// Xbox pitch is 4096ths of an octave relative to 48 kHz: freq = 48000 * 2^(pitch / 4096).
// Rewrite the argument in place and hand over to the host's SetFrequency, which takes (this, DWORD freq) the same way.
X86_EXTERN_FUNC(GUEST(IDirectSoundBuffer_SetFrequency));
X86_FUNC(sub_003E4148) {
    int32_t pitch = int32_t(kernel::R32(ctx.esp + 8));
    double hz = 48000.0 * std::exp2(double(pitch) / 4096.0);
    if (hz < 100.0) hz = 100.0;
    if (hz > 200000.0) hz = 200000.0;
    kernel::W32(ctx.esp + 8, uint32_t(std::lround(hz)));
    GUEST(IDirectSoundBuffer_SetFrequency)(ctx, base);
}

// XAudioCalculatePitch (.XTLID 0xB00A3, 003E34F2) is pure arithmetic and runs fine as recompiled code.

// ---------------------------------------------------------------------------------------------------------------
// Cutscenes skippable on the first playthrough (settings.toml: skip_cutscenes = true, the default).
// The game only lets START skip a cutscene it has already shown. Both checks read a flag into AL and branch on it;
// these [[hook]]s run after that load (config/game.toml) and replace the value.
// ---------------------------------------------------------------------------------------------------------------
namespace shadow {
bool g_skip_cutscenes = true;
bool g_cutscene_60 = true;
}

// In-engine cutscenes at 60 FPS (settings.toml: cutscene_60fps). The stage loop (00056FC0) waits [0x54DD40] frame
// periods of 1/[0x54DD64] s per update; the event start (002291AA) sets 2, i.e. 30 FPS, sets [0x54D481] while the
// event plays, and the event end puts the saved [0x54DD40] back. Checked every frame, so the option takes effect at
// once: 1 while an event plays with the option on, the game's 2 with it off.
// Shadow's loader tasks (thread proc 0004F440, a step at a time) build RenderWare objects while the main thread's
// frame updates and syncs the same frame lists. One core on the Xbox kept them apart; here they ran at once and
// corrupted the dirty frame list (self-linked frame, main thread looping in 002EC720 = the black screen after
// event2101 / on pause). Each task step and the main thread's frame (from the end of the frame wait to the next
// one) now hold the guest CPU; blocking kernel calls and the present let it go.
X86_HOST_API void ShadowTaskEnter(X86Context& ctx, uint8_t* base) {
    (void)ctx, (void)base;
    kernel::GuestCpuAcquire();
}
X86_HOST_API void ShadowTaskLeave(X86Context& ctx, uint8_t* base) {
    (void)ctx, (void)base;
    kernel::GuestCpuRelease();
}
X86_HOST_API void ShadowMainEnter(X86Context& ctx, uint8_t* base) {
    (void)ctx, (void)base;
    kernel::GuestCpuAcquire();
}
X86_HOST_API void ShadowMainLeave(X86Context& ctx, uint8_t* base) {
    (void)ctx, (void)base;
    kernel::GuestCpuRelease();
}

X86_HOST_API void ShadowEventFrameRate(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    kernel::GuestCpuRelease();  // the frame wait starts: the loader tasks may run
    if (base[0x54D481]) *reinterpret_cast<uint32_t*>(base + 0x54DD40) = shadow::g_cutscene_60 ? 1u : 2u;
}

// The event scene player (event+0x180; set time 0022BDE0 -> 0022BBE0) seeks its scene to the event time cut to a
// whole frame (cvttss2si [esi+8] at 0022BD02, back to float at 0022BD2C), so at 60 FPS, where the time moves half a
// frame per update, cameras and actors only move every other update. With cutscenes at 60 the seek uses the time
// as it is; at 30 the time is whole anyway.
X86_HOST_API void ShadowEventSceneTime(X86Context& ctx, uint8_t* base) {
    if (shadow::g_cutscene_60) ctx.xmm[0].f[0] = *reinterpret_cast<const float*>(base + ctx.esi + 8);
}

// DBG-STALE-FRAME: a RenderWare frame added to the dirty list (002E1300) whose own memory or root's is freed: the
// use-after-free behind the stage-end crashes and the black screen. Logs the guest stack the first times.
X86_HOST_API void ShadowCheckDirtyFrame(X86Context& ctx, uint8_t* base) {
    static int reports = 0;
    const uint32_t frame = ctx.eax;
    if (!frame || reports >= 8) return;
    const bool frame_freed = mem::IsFreed(frame);
    const uint32_t root = frame_freed ? 0 : *reinterpret_cast<const uint32_t*>(base + frame + 0xA0);
    if (frame_freed || (root && mem::IsFreed(root))) {
        reports++;
        LOGE("stale frame: frame %08X%s root %08X%s", frame, frame_freed ? " (freed)" : "", root,
             root && mem::IsFreed(root) ? " (freed)" : "");
        kernel::LogGuestStack(ctx, 40);
    }
}

// The frame wait busy-loops on the CPU clock (rdtsc / 733 = microseconds, 000116B0) until the elapsed time, counted
// in whole periods on a grid of multiples of the period, reaches the target: one core at 100%. Here, inside that
// loop, eax = target and ebx:esi = elapsed (both microseconds), [esp+0x10] = the period. The loop leaves when the
// clock crosses the boundary (target - elapsed) / period - 1 periods after the next one; sleep until about 2.5 ms
// before that and let the loop spin the rest, so the frame still ends on time.
X86_HOST_API void ShadowFrameWait(X86Context& ctx, uint8_t* base) {
    const uint64_t target = ctx.eax, elapsed = uint64_t(ctx.esi) << 32 | ctx.ebx;
    const uint64_t period = *reinterpret_cast<const uint32_t*>(base + ctx.esp + 0x10);
    if (!period || elapsed >= target || ctx.edx) return;
    // (Tried 2026-10-31: ending this wait at once to run one update per present. The render thread decouples
    // presents from V-Sync, so with V-Sync on and the port's pacing off nothing held the game to 60: removed.)
    // One clock: with the port presenting at 60 (fps cap 60, its own pacing and V-Sync), the game's own 60 Hz grid
    // is a second clock that drifts against the display, so now and then one frame waits two periods and the next
    // none (uneven 33/0 ms frames). One update per present instead: the wait ends here as if one period passed,
    // so the game's frame time is exactly 1/60 s. Event frames at 30 (target two periods) keep the game's wait.
    const uint64_t now = x86_rdtsc() / 733;
    const uint64_t left = (target - elapsed) / period * period - now % period;  // to the boundary that ends the wait
    constexpr uint64_t kSpin = 2500;  // microseconds kept spinning: Sleep(1) can take up to ~2 ms
    if (left > kSpin + 1000) std::this_thread::sleep_for(std::chrono::milliseconds((left - kSpin) / 1000));
}

// Pre-rendered movies (StageMovie, E####.sfd): `mov al, [esi+0x18]` at 001717DF reads "unskippable" (the constructor
// sets 1, cleared for scenes already seen); 0 lets the START test at 001717FF run.
X86_HOST_API void ShadowMovieSkippable(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (shadow::g_skip_cutscenes) ctx.eax &= ~0xFFu;
}

// In-engine cutscenes: the event manager (TPjsEventActionManager, vtable 0046E038, update 00228740) takes START only
// when `[0x541F20]+8` is 0. 00226B00 sets that byte to 1 when the event's bit in the save (+0x1754 bitmask) was not
// set yet, i.e. on the first viewing. 0 here lets the START test go on to the skip below.
X86_HOST_API void ShadowEventFirstView(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (shadow::g_skip_cutscenes) ctx.ecx &= ~0xFFu;
}

// In-engine cutscenes (event####_scene*.one): after START, `mov al, [0x54DE01]` at 0022879B reads the global
// "skip allowed" byte; 1 sets the event's skip request (this+0x34).
X86_HOST_API void ShadowEventSkippable(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (shadow::g_skip_cutscenes) ctx.eax = (ctx.eax & ~0xFFu) | 1u;
}

// ---------------------------------------------------------------------------------------------------------------
// True widescreen (Hor+, issue #4). The game draws a 4:3 view, which a 16:9 screen stretched. RwCameraSetViewWindow
// (002DD1C0, cdecl camera, const RwV2d*: stores the window at camera + 0x68, its reciprocal at + 0x70, then syncs the
// camera's frame; the same RenderWare code as Sonic Heroes) runs as before, then a 4:3 window is widened to the
// screen's shape: the same height, more to the left and right, nothing stretched. Other shapes (render to texture
// views) stay as the game asks. The 2D side (HUD, text, menus) is narrowed back to its 4:3 shape in the middle by
// gpu::g_hud_squeeze (sdk/runtime/gpu/vertex.cpp). settings.toml: widescreen; the menu switch takes effect when the
// game next sets its cameras (a new stage or area).
// ---------------------------------------------------------------------------------------------------------------
#include <mutex>
#include <unordered_map>

#include "gpu/vertex.h"

namespace shadow {
bool g_widescreen = true;
// the frame is shown 16:9 when the output is widescreen (sdk/runtime/gpu/d3d12.cpp, Present)
constexpr float kWide = (16.0f / 9.0f) / (4.0f / 3.0f);
void ApplyWidescreen() {
    gpu::g_hud_squeeze = g_widescreen && app::Widescreen() ? 1.0f / kWide : 0.0f;
    static int last = -1;
    int now = g_widescreen ? (app::Widescreen() ? 2 : 1) : 0;
    if (now != last) {
        last = now;
        app::Mode m = app::Output();
        LOGI("widescreen: %s (output %ux%u)", now == 2 ? "on" : now == 1 ? "on, but the output is not wide (4:3 shown)" : "off",
             m.width, m.height);
    }
}
}  // namespace shadow

X86_EXTERN_FUNC(__imp__sub_002DD1C0);
X86_FUNC(sub_002DD1C0) {
    uint32_t camera = kernel::R32(ctx.esp + 4), window = kernel::R32(ctx.esp + 8);
    float asked = 0;
    if (window) {
        uint32_t v = kernel::R32(window);
        std::memcpy(&asked, &v, 4);
    }
    __imp__sub_002DD1C0(ctx, base);
    shadow::ApplyWidescreen();
    if (!shadow::g_widescreen || !app::Widescreen() || !camera) return;
    // the window this hook last gave each camera: the game handing it back (it re-sets a camera from its own
    // fields) must not be widened a second time
    static std::mutex lock;
    static std::unordered_map<uint32_t, float> given;
    std::lock_guard<std::mutex> l(lock);
    auto it = given.find(camera);
    if (it != given.end() && it->second == asked) return;
    float x, y;
    uint32_t xr = kernel::R32(camera + 0x68), yr = kernel::R32(camera + 0x6C);
    std::memcpy(&x, &xr, 4);
    std::memcpy(&y, &yr, 4);
    if (!(x > 0 && y > 0) || std::fabs(x / y - 4.0f / 3.0f) > 0.03f) return;
    x *= shadow::kWide;
    float rx = 1.0f / x;
    std::memcpy(&xr, &x, 4);
    kernel::W32(camera + 0x68, xr);
    std::memcpy(&xr, &rx, 4);
    kernel::W32(camera + 0x70, xr);
    given[camera] = x;
    static int logged = 0;
    if (logged < 8) {
        logged++;
        LOGI("widescreen: camera %08X view window %.4f x %.4f widened to %.4f", camera, x / shadow::kWide, y, x);
    }
}
