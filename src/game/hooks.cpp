// Shadow the Hedgehog (USA) default.xbe: game-side replacements.
// A strong X86_FUNC(sub_XXXXXXXX) replaces the recompiled function at that address.

#include <cmath>

#include "kernel/kernel.h"
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
