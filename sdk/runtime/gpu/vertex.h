#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "gpu/state.h"

namespace gpu {

enum class Topology : uint8_t { Points, Lines, Triangles };

// how the vertex shader finishes an element the input assembler reads raw, 4 bits per register in Assembled::decode
enum VertexDecode : uint8_t {
    kDecodeNone,
    kDecodeW1,       // 3 components read as 4: w = 1
    kDecodeInt,      // SHORT: integers to float
    kDecodeIntW1,    // SHORT3
    kDecodePacked,   // NORMPACKED3, 11:11:10 signed in a uint
    kDecodeFloat2H,  // x y w as 3 floats
};

// a buffer the draw reads: guest vertices as they are, or the float4 copy converted on the CPU
struct VertexBuffer {
    const uint8_t* src = nullptr;
    uint32_t bytes = 0, stride = 0;
};

// where input register r comes from: host vertex format (a DXGI_FORMAT), buffer, byte offset in the vertex
struct VertexElement {
    uint8_t format = 0, buffer = 0;
    uint16_t offset = 0;
};

// one draw's vertices and indices as the input assembler takes them
struct Assembled {
    std::vector<float> vertices;    // converted path: vertex_count * regs * 4
    std::vector<uint32_t> indices;  // converted path, or the list rebuilt for fans, quads and loops
    uint16_t regs = 0;              // input registers present, bit per v register
    uint32_t vertex_count = 0;
    Topology topology = Topology::Triangles;
    bool strip = false;             // line or triangle strip, drawn as a strip
    bool pretransformed = false;    // FVF XYZRHW
    uint64_t decode = 0;            // VertexDecode per register, 4 bits each
    std::array<VertexElement, 16> elements{};
    std::array<VertexBuffer, 16> buffers{};
    uint32_t buffer_count = 0;
    const void* index_src = nullptr;  // the title's 16 bit indices, or indices
    bool index16 = false;
    uint32_t index_count = 0;         // 0: an unindexed draw of draw_count vertices
    uint32_t draw_count = 0;
    int32_t base_vertex = 0;
    std::array<std::vector<uint8_t>, 16> gathered;  // raw vertices copied in draw order when the indices are sparse
};

// the FVF bits translated to the register layout the XDK's declarations use
void FvfInputs(uint32_t fvf, VertexShader& out);

// false when there is nothing to draw
// indices: the title's 16 bit indices, null for an unindexed draw
bool Assemble(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices, Assembled& out);

// ShadowNativePort widescreen: a 4:3 picture shown on a 16:9 screen with its 3D view widened (Hor+) still has its 2D
// (screen space, XYZRHW) draws stretched. g_hud_squeeze (0 = off) narrows each such draw back to its shape, toward
// an anchor: the screen's left edge, centre or right edge. With g_hud_anchor_edges, while a 3D scene is shown, the
// pieces of the HUD go to the nearest edge as whole blocks (text lines, text in its panel; see vertex.cpp), else
// everything keeps the centre. Only draws onto the 640 wide screen; draws spanning its whole width (the 3D scene,
// fades, movies, backgrounds) stay full width.
extern float g_hud_squeeze;
extern bool g_hud_anchor_edges;
// this draw's code for the shaders, 0 for none: factor + 2 * anchor (0 left, 1 centre, 2 right). tex: the draw's
// first texture (header address, 0 for none); scene_texture: that texture is the 512x512 3D scene target
float HudSqueeze(const Assembled& a, uint32_t w, bool textured, uint32_t tex = 0, bool scene_texture = false);
// once per frame, at present: this frame's blocks give the next frame's anchors
void HudFrameEnd();
// the screen space x of a vertex after HudSqueeze's code
inline float HudSqueezeX(float x, float code, float w) {
    if (code <= 0) return x;
    float anchor = float(int(code * 0.5f));
    float f = code - 2 * anchor;
    float pivot = w * 0.5f * anchor;
    return pivot + (x - pivot) * f;
}

}  // namespace gpu
