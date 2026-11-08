#include "gpu/vertex.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <dxgiformat.h>

#include "core/log.h"
#include "kernel/kernel.h"

namespace gpu {

namespace {

constexpr uint32_t kTypeFloat2H = 0x72;  // FLOAT2H
// D3DVSDT_NONE: a FLOAT with no components, the register reads no stream data
constexpr uint32_t kTypeNone = 0x02;

// D3DVSDT: low nibble the kind, high nibble the component count
void Convert(uint32_t type, const uint8_t* p, float* o) {
    o[0] = 0;
    o[1] = 0;
    o[2] = 0;
    o[3] = 1;
    uint32_t kind = type & 15;
    uint32_t n = std::min<uint32_t>((type >> 4) & 15, 4);
    if (type == kTypeFloat2H) {
        float v[3];
        std::memcpy(v, p, 12);
        o[0] = v[0];
        o[1] = v[1];
        o[3] = v[2];
        return;
    }
    switch (kind) {
        case 0:  // D3DCOLOR, bytes B G R A
            o[0] = p[2] / 255.0f;
            o[1] = p[1] / 255.0f;
            o[2] = p[0] / 255.0f;
            o[3] = p[3] / 255.0f;
            break;
        case 1:  // NORMSHORT
            for (uint32_t i = 0; i < n; i++) {
                int16_t s;
                std::memcpy(&s, p + i * 2, 2);
                o[i] = std::max(s / 32767.0f, -1.0f);
            }
            break;
        case 2:  // FLOAT
            std::memcpy(o, p, n * 4);
            break;
        case 4:  // PBYTE
            for (uint32_t i = 0; i < n; i++) o[i] = p[i] / 255.0f;
            break;
        case 5:  // SHORT
            for (uint32_t i = 0; i < n; i++) {
                int16_t s;
                std::memcpy(&s, p + i * 2, 2);
                o[i] = s;
            }
            break;
        case 6: {  // NORMPACKED3, 11 11 10 signed
            uint32_t v;
            std::memcpy(&v, p, 4);
            int32_t x = int32_t(v << 21) >> 21;
            int32_t y = int32_t(v << 10) >> 21;
            int32_t z = int32_t(v) >> 22;
            o[0] = std::max(x / 1023.0f, -1.0f);
            o[1] = std::max(y / 1023.0f, -1.0f);
            o[2] = std::max(z / 511.0f, -1.0f);
            break;
        }
        default:
            break;
    }
}

// A/B switch: 0 converts every vertex on the CPU, the older path
#ifndef XBE_RAW_VERTICES
#define XBE_RAW_VERTICES 1
#endif
constexpr bool kRawVertices = XBE_RAW_VERTICES;

// D3DVSDT type as the input assembler reads it: DXGI format, what the vertex shader does after, bytes the format reads
struct HostElement {
    uint8_t format = 0;
    uint8_t decode = kDecodeNone;
    uint8_t bytes = 0;
};
HostElement HostFormat(uint32_t type) {
    switch (type) {
        case 0x12: return {DXGI_FORMAT_R32_FLOAT, kDecodeNone, 4};
        case 0x22: return {DXGI_FORMAT_R32G32_FLOAT, kDecodeNone, 8};
        case 0x32: return {DXGI_FORMAT_R32G32B32_FLOAT, kDecodeNone, 12};
        case 0x42: return {DXGI_FORMAT_R32G32B32A32_FLOAT, kDecodeNone, 16};
        case kTypeFloat2H: return {DXGI_FORMAT_R32G32B32_FLOAT, kDecodeFloat2H, 12};
        case 0x40: return {DXGI_FORMAT_B8G8R8A8_UNORM, kDecodeNone, 4};  // D3DCOLOR, bytes B G R A
        case 0x11: return {DXGI_FORMAT_R16_SNORM, kDecodeNone, 2};
        case 0x21: return {DXGI_FORMAT_R16G16_SNORM, kDecodeNone, 4};
        case 0x31: return {DXGI_FORMAT_R16G16B16A16_SNORM, kDecodeW1, 8};
        case 0x41: return {DXGI_FORMAT_R16G16B16A16_SNORM, kDecodeNone, 8};
        case 0x14: return {DXGI_FORMAT_R8_UNORM, kDecodeNone, 1};
        case 0x24: return {DXGI_FORMAT_R8G8_UNORM, kDecodeNone, 2};
        case 0x34: return {DXGI_FORMAT_R8G8B8A8_UNORM, kDecodeW1, 4};
        case 0x44: return {DXGI_FORMAT_R8G8B8A8_UNORM, kDecodeNone, 4};
        case 0x15: return {DXGI_FORMAT_R16_SINT, kDecodeInt, 2};
        case 0x25: return {DXGI_FORMAT_R16G16_SINT, kDecodeInt, 4};
        case 0x35: return {DXGI_FORMAT_R16G16B16A16_SINT, kDecodeIntW1, 8};
        case 0x45: return {DXGI_FORMAT_R16G16B16A16_SINT, kDecodeInt, 8};
        case 0x16: return {DXGI_FORMAT_R32_UINT, kDecodePacked, 4};
        default: return {};
    }
}

// the index list for a primitive the input assembler has no topology for, or for converted vertices
template <typename F>
void ListIndices(uint32_t prim, uint32_t count, F local, std::vector<uint32_t>& ix, Topology& topology) {
    ix.reserve(size_t(count) * 3);
    topology = Topology::Triangles;
    switch (prim) {
        case 1:  // points
            topology = Topology::Points;
            for (uint32_t i = 0; i < count; i++) ix.push_back(local(i));
            break;
        case 2:  // line list
            topology = Topology::Lines;
            for (uint32_t i = 0; i + 1 < count; i += 2) ix.insert(ix.end(), {local(i), local(i + 1)});
            break;
        case 3:  // line loop
        case 4:  // line strip
            topology = Topology::Lines;
            for (uint32_t i = 0; i + 1 < count; i++) ix.insert(ix.end(), {local(i), local(i + 1)});
            if (prim == 3 && count > 2) ix.insert(ix.end(), {local(count - 1), local(0)});
            break;
        case 5:  // triangle list
            for (uint32_t i = 0; i + 2 < count; i += 3) ix.insert(ix.end(), {local(i), local(i + 1), local(i + 2)});
            break;
        case 6:  // triangle strip
            for (uint32_t i = 0; i + 2 < count; i++) {
                if (i & 1) ix.insert(ix.end(), {local(i + 1), local(i), local(i + 2)});
                else ix.insert(ix.end(), {local(i), local(i + 1), local(i + 2)});
            }
            break;
        case 7:   // fan
        case 10:  // polygon
            for (uint32_t i = 1; i + 1 < count; i++) ix.insert(ix.end(), {local(0), local(i), local(i + 1)});
            break;
        case 8:  // quad list
            for (uint32_t i = 0; i + 3 < count; i += 4) {
                ix.insert(ix.end(), {local(i), local(i + 1), local(i + 2), local(i), local(i + 2), local(i + 3)});
            }
            break;
        case 9:  // quad strip
            for (uint32_t i = 0; i + 3 < count; i += 2) {
                ix.insert(ix.end(), {local(i), local(i + 1), local(i + 2), local(i + 1), local(i + 3), local(i + 2)});
            }
            break;
    }
}

uint32_t TypeBytes(uint32_t t) {
    if (t == kTypeFloat2H) return 12;
    uint32_t kind = t & 15;
    uint32_t n = (t >> 4) & 15;
    switch (kind) {
        case 0: return 4;
        case 1: case 5: return 2 * n;
        case 4: return n;
        case 6: return 4;
        default: return 4 * n;
    }
}

constexpr uint32_t kResData = 4;

void ResetAssembled(Assembled& out) {
    out.vertices.clear();
    out.indices.clear();
    out.regs = 0;
    out.vertex_count = 0;
    out.topology = Topology::Triangles;
    out.strip = false;
    out.decode = 0;
    out.buffer_count = 0;
    out.index_src = nullptr;
    out.index16 = false;
    out.draw_count = 0;
    out.index_count = 0;
    out.base_vertex = 0;
}

// the draw as Assemble got it, plus the vertex range it reads
struct DrawRange {
    uint32_t prim = 0;
    uint32_t start = 0;
    uint32_t count = 0;
    const uint8_t* ip = nullptr;
    uint32_t first = 0;         // lowest index
    uint32_t first_vertex = 0;  // first vertex read, base vertex applied
    uint32_t span = 0;
};

// the input registers fed by a bound stream and the layout they come from
struct Inputs {
    const VertexShader& layout;
    const int* regs;
    int nregs;
};

struct IndexBounds {
    uint32_t first = 0;
    uint32_t last = 0;
};

// the declaration's register layout, or the one an FVF handle stands for, built into fvf_layout
const VertexShader* PickLayout(const State& s, VertexShader& fvf_layout) {
    if (!(s.vs_handle & 1)) {
        FvfInputs(s.vs_handle, fvf_layout);
        return &fvf_layout;
    }
    return s.vs.get();
}

// D3DFVF: an FVF handle with position XYZRHW
bool Pretransformed(const State& s) {
    return !(s.vs_handle & 1) && (s.vs_handle & 0xE) == 0x4;
}

IndexBounds IndexRange(const uint8_t* ip, uint32_t count) {
    uint16_t lo16 = 0xFFFF;
    uint16_t hi16 = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t v;
        std::memcpy(&v, ip + i * 2, 2);
        lo16 = std::min(lo16, v);
        hi16 = std::max(hi16, v);
    }
    return {lo16, hi16};
}

// logical vertex numbers in draw order
uint32_t DrawVertex(const State& s, const DrawRange& d, uint32_t i) {
    if (!d.ip) return d.start + i;
    uint16_t v;
    std::memcpy(&v, d.ip + i * 2, 2);
    return v + s.base_vertex;
}

// the input registers fed by a bound stream, also marked in out.regs
int CollectRegs(const State& s, const VertexShader& layout, int* regs, Assembled& out) {
    int nregs = 0;
    for (int r = 0; r < 16; r++) {
        const auto& in = layout.inputs[size_t(r)];
        if (in.stream < 0 || !in.type || in.type == kTypeNone) continue;
        const auto& st = s.streams[size_t(in.stream)];
        if (!st.vb) continue;
        regs[nregs++] = r;
        out.regs |= uint16_t(1u << r);
    }
    return nregs;
}

uint32_t StrideOf(const State& s, const VertexShader& layout, const VertexShader::Input& in) {
    const auto& st = s.streams[size_t(in.stream)];
    return st.stride ? st.stride : layout.stream_stride[size_t(in.stream)];
}

constexpr uint32_t kStrideAlign = 4;            // the input assembler reads strides in dwords
constexpr uint32_t kElementOffsetEnd = 0x10000;  // the element offset is stored as uint16_t

bool RawReadable(const State& s, const Inputs& inputs) {
    for (int k = 0; k < inputs.nregs; k++) {
        const auto& in = inputs.layout.inputs[size_t(inputs.regs[k])];
        HostElement he = HostFormat(in.type);
        uint32_t stride = StrideOf(s, inputs.layout, in);
        bool readable = he.format && stride && stride % kStrideAlign == 0 && in.offset < kElementOffsetEnd &&
                        in.offset % std::min<uint32_t>(he.bytes, 4) == 0;
        if (!readable) {
            LOG_FIRST(32, "vertex: converted on the CPU: v%d type %02X offset %u stride %u", inputs.regs[k], in.type,
                      in.offset, stride);
            return false;
        }
    }
    return true;
}

// sparse indices: the vertices they read, copied in draw order
void GatherVertices(const State& s, const DrawRange& d, const uint8_t* data, uint32_t stride,
                    std::vector<uint8_t>& g) {
    g.resize(size_t(d.count) * stride);
    for (uint32_t i = 0; i < d.count; i++) {
        std::memcpy(&g[size_t(i) * stride], data + size_t(DrawVertex(s, d, i)) * stride, stride);
    }
}

// returns the buffer slot the stream got
int AddRawBuffer(const State& s, const VertexShader& layout, const VertexShader::Input& in, const DrawRange& d,
                 bool dense, Assembled& out) {
    using namespace kernel;
    const auto& st = s.streams[size_t(in.stream)];
    uint32_t stride = StrideOf(s, layout, in);
    const uint8_t* data = static_cast<const uint8_t*>(HostPtr(mem::VirtualFromPhysical(R32(st.vb + kResData))));
    int slot = int(out.buffer_count);
    if (dense) {
        out.buffers[out.buffer_count++] = {data + size_t(d.first_vertex) * stride, d.span * stride, stride};
    } else {
        auto& g = out.gathered[out.buffer_count];
        GatherVertices(s, d, data, stride, g);
        out.buffers[out.buffer_count++] = {g.data(), d.count * stride, stride};
    }
    return slot;
}

// primitives the input assembler draws without a rebuilt index list
bool NativePrimitive(uint32_t prim) {
    return prim == 1 || prim == 2 || prim == 4 || prim == 5 || prim == 6;
}

Topology NativeTopology(uint32_t prim) {
    if (prim == 1) return Topology::Points;
    if (prim <= 4) return Topology::Lines;
    return Topology::Triangles;
}

uint32_t MinVertices(uint32_t prim) {
    if (prim == 1) return 1u;
    if (prim <= 4) return 2u;
    return 3u;
}

bool DrawNative(const DrawRange& d, bool dense, Assembled& out) {
    out.strip = d.prim == 4 || d.prim == 6;
    out.topology = NativeTopology(d.prim);
    if (d.ip && dense) {
        out.index_src = d.ip;
        out.index16 = true;
        out.index_count = d.count;
        out.base_vertex = -int32_t(d.first);
    } else {
        out.draw_count = d.count;
    }
    return d.count >= MinVertices(d.prim);
}

bool UseIndexList(Assembled& out) {
    out.index_src = out.indices.data();
    out.index_count = uint32_t(out.indices.size());
    return out.index_count != 0;
}

// a vertex range at most this far over the draw's count is read in place, a sparser one gathered
constexpr uint64_t kDenseSpanPerVertex = 4;
constexpr uint64_t kDenseSpanSlack = 1024;

bool AssembleRaw(const State& s, const Inputs& inputs, const DrawRange& d, Assembled& out) {
    bool dense = uint64_t(d.span) <= uint64_t(d.count) * kDenseSpanPerVertex + kDenseSpanSlack;
    int slot[16];
    std::fill(std::begin(slot), std::end(slot), -1);
    for (int k = 0; k < inputs.nregs; k++) {
        int reg = inputs.regs[k];
        const auto& in = inputs.layout.inputs[size_t(reg)];
        if (slot[in.stream] < 0) {
            slot[in.stream] = AddRawBuffer(s, inputs.layout, in, d, dense, out);
        }
        HostElement he = HostFormat(in.type);
        out.elements[size_t(reg)] = {he.format, uint8_t(slot[in.stream]), uint16_t(in.offset)};
        out.decode |= uint64_t(he.decode) << (reg * 4);
    }
    out.vertex_count = dense ? d.span : d.count;
    if (NativePrimitive(d.prim)) return DrawNative(d, dense, out);
    auto local = [&](uint32_t i) { return dense ? DrawVertex(s, d, i) - d.first_vertex : i; };
    ListIndices(d.prim, d.count, local, out.indices, out.topology);
    return UseIndexList(out);
}

// a vertex range at most this far over the draw's count is converted whole, in range order
constexpr uint64_t kRangedSpanPerVertex = 2;
constexpr uint64_t kRangedSpanSlack = 64;

// converted on the CPU: each element to a float4, vertices in range order when the indices are dense
bool AssembleConverted(const State& s, const Inputs& inputs, const DrawRange& d, Assembled& out) {
    using namespace kernel;
    int nregs = inputs.nregs;
    std::vector<uint32_t> seq(d.count);
    for (uint32_t i = 0; i < d.count; i++) seq[i] = DrawVertex(s, d, i);
    bool ranged = uint64_t(d.span) <= uint64_t(d.count) * kRangedSpanPerVertex + kRangedSpanSlack;
    uint32_t n = ranged ? d.span : d.count;
    out.vertex_count = n;
    out.vertices.resize(size_t(n) * nregs * 4);
    for (int k = 0; k < nregs; k++) {
        const auto& in = inputs.layout.inputs[size_t(inputs.regs[k])];
        const auto& st = s.streams[size_t(in.stream)];
        uint32_t data = mem::VirtualFromPhysical(R32(st.vb + kResData));
        uint32_t stride = StrideOf(s, inputs.layout, in);
        const uint8_t* src = static_cast<const uint8_t*>(HostPtr(data + in.offset));
        for (uint32_t v = 0; v < n; v++) {
            uint32_t id = ranged ? d.first_vertex + v : seq[v];
            Convert(in.type, src + size_t(id) * stride, &out.vertices[(size_t(v) * nregs + k) * 4]);
        }
        out.elements[size_t(inputs.regs[k])] = {DXGI_FORMAT_R32G32B32A32_FLOAT, 0, uint16_t(k * 16)};
    }
    out.buffer_count = 1;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(out.vertices.data());
    out.buffers[0] = {bytes, uint32_t(out.vertices.size() * 4), uint32_t(nregs * 16)};
    auto local = [&](uint32_t i) { return ranged ? seq[i] - d.first_vertex : i; };
    ListIndices(d.prim, d.count, local, out.indices, out.topology);
    return UseIndexList(out);
}

}  // namespace

void FvfInputs(uint32_t fvf, VertexShader& vs) {
    vs.inputs.fill({});
    uint32_t off = 0;
    auto add = [&](int reg, uint32_t type) {
        vs.inputs[size_t(reg)] = {0, off, type};
        off += TypeBytes(type);
    };
    // D3DFVF: position 0xE, normal 0x10, point size 0x20, diffuse 0x40, specular 0x80, texture count bits 8 to 11,
    // sizes from bit 16
    switch (fvf & 0xE) {
        case 0x2: add(0, 0x32); break;
        case 0x4: add(0, 0x42); break;
        case 0x6: case 0x8: case 0xA: case 0xC: {
            add(0, 0x32);
            uint32_t weights = ((fvf & 0xE) - 0x4) / 2;
            add(1, 0x02 | (weights << 4));
            break;
        }
        default: break;
    }
    if (fvf & 0x10) add(2, 0x32);
    if (fvf & 0x20) add(6, 0x12);
    if (fvf & 0x40) add(3, 0x40);
    if (fvf & 0x80) add(4, 0x40);
    uint32_t tex = (fvf >> 8) & 15;
    static const uint32_t kTexType[4] = {0x22, 0x32, 0x42, 0x12};
    for (uint32_t i = 0; i < tex && i < 4; i++) add(int(9 + i), kTexType[(fvf >> (16 + i * 2)) & 3]);
    vs.stream_stride.fill(0);
    vs.stream_stride[0] = off;
}

bool Assemble(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* ip, Assembled& out) {
    ResetAssembled(out);
    if (!count || prim < 1 || prim > 10) return false;

    VertexShader fvf_layout;
    const VertexShader* layout = PickLayout(s, fvf_layout);
    out.pretransformed = Pretransformed(s);
    if (!layout) return false;

    // the vertex range the draw reads
    IndexBounds bounds = {0, count - 1};
    if (ip) bounds = IndexRange(ip, count);
    DrawRange d;
    d.prim = prim;
    d.start = start;
    d.count = count;
    d.ip = ip;
    d.first = bounds.first;
    d.first_vertex = ip ? bounds.first + s.base_vertex : start;
    d.span = bounds.last - bounds.first + 1;

    int regs[16];
    int nregs = CollectRegs(s, *layout, regs, out);
    if (!nregs) return false;
    Inputs inputs = {*layout, regs, nregs};

    // the guest's own vertices when the input assembler can read every element of them
    bool raw = kRawVertices && RawReadable(s, inputs);
    if (raw) return AssembleRaw(s, inputs, d, out);
    return AssembleConverted(s, inputs, d, out);
}

float g_hud_squeeze = 0;
bool g_hud_anchor_edges = true;

float HudCode(float factor, float pivot) {
    float p = std::clamp(std::round(pivot), -2000.0f, 2600.0f);
    return factor + 2.0f * (p + 4096.0f);
}

namespace {

// Widescreen 2D layout. The game draws its 2D one piece at a time (text one glyph per draw), so where a piece goes
// depends on what it belongs to. The screen's 2D draws of a frame are grouped into blocks as they come: consecutive
// draws with one texture that continue a line of text (same line, small gap) or start the next line below it. The
// layout is worked out from the whole frame when it is submitted (HudLayoutFrame), and each draw's constants are
// filled in then (the renderer keeps them until the frame goes to the GPU), so every piece is placed by the frame it
// is in: nothing waits a frame, nothing jumps.
// Each block gets a pivot (its draws are narrowed to their 4:3 shape around it, or kNoSqueeze) and a shift:
// - the stage HUD (its two top gauge bars are drawn): left half to the left edge, right half to the right edge (past
//   the console's safe area margin, so at the very edges), across the middle the centre, pieces in a panel with
//   their panel;
// - menus and screens drawn over a cover (title cards, pause): the centre, except small groups of touching pieces
//   off to one side (an emblem, "loading"), which go to that edge; decorations running across and off the screen
//   (the title cards' lines, drawn in segments) are stretched with the background; text rows inside a dialog panel
//   are centred in the panel;
// - subtitles (the game's text in the lower part of the screen, not in a panel): full size, centred on the screen.
struct Block {
    uint32_t tex;
    float lo, hi, top, bottom;             // the block's extent
    float last_lo, last_hi, last_top, last_bottom;  // its last piece
    bool cover;                            // drawn after a full screen cover this frame (a screen of its own)
    int pieces;                            // draws in it (glyphs for text)
    bool projected;                        // drawn through the transform pipeline (the game's text)
};

constexpr float kNoSqueeze = -1e9f;        // a pivot meaning "leave it stretched"

struct Result {
    float code = 0, shift = 0;
    float ypivot = 0, yscale = 0;  // yscale > 0: also grown upright about ypivot (console pixels)
};

struct HudLayout {
    std::vector<Block> blocks;     // this frame's, in draw order
    std::vector<Result> results;   // HudLayoutFrame's, per block
    bool overlay = false;          // a full height cover was drawn this frame
    bool open = false;             // the last block may take the next piece
    int hud_frames = 0;            // recent frames with the stage HUD: > 0 for a few frames after one
};
HudLayout g_layout;
std::mutex g_layout_lock;

int RootAnchor(const Block& b, float w) {
    if (b.hi <= w * 0.5f + 1) return 0;
    if (b.lo >= w * 0.5f - 1) return 2;
    return 1;
}

bool Touch(const Block& a, const Block& b, float slack) {
    return a.lo < b.hi + slack && b.lo < a.hi + slack && a.top < b.bottom + slack && b.top < a.bottom + slack;
}

bool Holds(const Block& c, const Block& b) {
    return c.lo <= b.lo + 2 && c.hi >= b.hi - 2 && c.top <= b.top + 2 && c.bottom >= b.bottom - 2 &&
           (c.hi - c.lo) * (c.bottom - c.top) > (b.hi - b.lo) * (b.bottom - b.top) * 1.2f;
}

// the share of the smaller of two pieces covered by the other
float Overlap(const Block& a, const Block& b) {
    float w = std::min(a.hi, b.hi) - std::max(a.lo, b.lo), h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
    if (w <= 0 || h <= 0) return 0;
    float small = std::min((a.hi - a.lo) * (a.bottom - a.top), (b.hi - b.lo) * (b.bottom - b.top));
    return small > 0 ? w * h / small : 0;
}

// a piece large both ways (a line segment, an art layer), not text or a prompt
bool Big(const Block& b) { return b.hi - b.lo >= 40 && b.bottom - b.top >= 40; }

// the stage HUD's two top bars (the hero gauge top left, the dark gauge top right): a matching pair, one hugging the
// left, one the right, the same height on screen and about the same size
bool HudBars(const std::vector<Block>& all, float w) {
    for (const Block& a : all) {
        if (a.cover || a.top > 480 * 0.15f || a.bottom - a.top > 480 * 0.15f || a.hi - a.lo < w * 0.15f) continue;
        if (!(a.lo < w * 0.15f && a.hi < w * 0.5f)) continue;
        for (const Block& b : all) {
            if (b.cover || !(b.hi > w * 0.85f && b.lo > w * 0.5f)) continue;
            if (std::fabs(b.top - a.top) > 8 || std::fabs((b.hi - b.lo) - (a.hi - a.lo)) > (a.hi - a.lo) * 0.2f) continue;
            return true;
        }
    }
    return false;
}

void Layout(HudLayout& L) {
    // the game's text is laid out where it would sit on the 4:3 screen (it draws it at x * 0.75, see below)
    std::vector<Block> all = L.blocks;
    for (Block& b : all)
        if (b.projected) b.lo /= 0.75f, b.hi /= 0.75f;
    size_t n = all.size();
    const float w = 640.0f, f = std::min(g_hud_squeeze, 0.999f);
    L.results.assign(n, Result{});
    if (!n) return;
    bool stage = L.hud_frames > 0 || HudBars(all, w);
    auto menu = [&](size_t i) { return !stage || all[i].cover; };
    std::vector<float> pivot(n, w * 0.5f), shift(n, 0.0f);
    // decorations: a chain of large touching pieces of one texture, meeting at their ends, that runs across most of
    // the screen and off it (the title cards' lines, drawn in segments)
    std::vector<bool> deco(n, false);
    {
        std::vector<size_t> chain(n);
        for (size_t i = 0; i < n; i++) chain[i] = i;
        auto root = [&](size_t i) {
            while (chain[i] != i) i = chain[i] = chain[chain[i]];
            return i;
        };
        for (size_t i = 0; i < n; i++) {
            if (!menu(i) || !Big(all[i])) continue;
            for (size_t j = i + 1; j < n; j++)
                if (menu(j) && Big(all[j]) && all[j].tex == all[i].tex && Touch(all[i], all[j], 1) &&
                    Overlap(all[i], all[j]) < 0.3f)
                    chain[root(i)] = root(j);
        }
        std::vector<float> clo(n, 1e30f), chi(n, -1e30f), ctop(n, 1e30f), cbot(n, -1e30f);
        std::vector<int> count(n, 0);
        for (size_t i = 0; i < n; i++) {
            if (!menu(i) || !Big(all[i])) continue;
            size_t r = root(i);
            clo[r] = std::min(clo[r], all[i].lo), chi[r] = std::max(chi[r], all[i].hi);
            ctop[r] = std::min(ctop[r], all[i].top), cbot[r] = std::max(cbot[r], all[i].bottom);
            count[r]++;
        }
        for (size_t i = 0; i < n; i++) {
            if (!menu(i) || !Big(all[i])) continue;
            size_t r = root(i);
            bool off = clo[r] < -2 || chi[r] > w + 2 || ctop[r] < -2 || cbot[r] > 482;
            bool long_ = chi[r] - clo[r] >= w * 0.8f || cbot[r] - ctop[r] >= 480 * 0.8f;
            if (off && long_ && count[r] >= 2) deco[i] = true;
        }
    }
    // menu groups of touching pieces: the centre, except a small group off to one side
    std::vector<size_t> parent(n);
    for (size_t i = 0; i < n; i++) parent[i] = i;
    auto find = [&](size_t i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    // tall background art (full-height side pieces) holds no group together: the prompts drawn over it keep their own
    auto tall = [&](size_t i) { return all[i].bottom - all[i].top >= 480 * 0.75f && !all[i].cover; };
    for (size_t i = 0; i < n; i++) {
        if (!menu(i) || deco[i] || tall(i)) continue;
        for (size_t j = i + 1; j < n; j++)
            if (menu(j) && !deco[j] && !tall(j) && Touch(all[i], all[j], 8)) parent[find(i)] = find(j);
    }
    std::vector<float> lo(n, 1e30f), hi(n, -1e30f), top(n, 1e30f), bottom(n, -1e30f);
    for (size_t i = 0; i < n; i++) {
        if (!menu(i) || deco[i]) continue;
        size_t r = find(i);
        lo[r] = std::min(lo[r], all[i].lo), hi[r] = std::max(hi[r], all[i].hi);
        top[r] = std::min(top[r], all[i].top), bottom[r] = std::max(bottom[r], all[i].bottom);
    }
    const float kEdge[3] = {-64.0f, w * 0.5f, w + 64.0f};  // past the safe area margin: at the very edges
    std::vector<int> got(n, 1);
    for (size_t i = 0; i < n; i++) {
        const Block& b = all[i];
        if (menu(i)) {
            if (deco[i]) {
                pivot[i] = kNoSqueeze;
                continue;
            }
            size_t r = find(i);
            if (tall(i)) {  // side art at a screen edge stays at it (one piece: a block of several also holds menu items)
                if (b.pieces > 1) continue;
                if (b.lo <= 2 && b.hi < w * 0.6f) pivot[i] = kEdge[0];
                else if (b.hi >= w - 2 && b.lo > w * 0.4f) pivot[i] = kEdge[2];
                continue;
            }
            if (hi[r] - lo[r] <= w * 0.35f && bottom[r] - top[r] <= 480 * 0.35f) {
                // only a group that starts near an edge (prompts, logos): menu items over the middle stay put
                if (hi[r] < w * 0.4f && lo[r] < w * 0.2f) pivot[i] = kEdge[0];
                else if (lo[r] > w * 0.6f && hi[r] > w * 0.8f) pivot[i] = kEdge[2];
            }
        } else {  // the stage HUD and the game's text during play: true shape, held by its panel or nearest edge
            int anchor = -1;
            for (size_t j = i; j-- > 0;) {  // the latest earlier block holding this one (a panel and its contents)
                if (!menu(j) && !all[j].projected && Holds(all[j], b)) {
                    anchor = got[j];
                    break;
                }
            }
            if (anchor < 0) anchor = RootAnchor(b, w);
            got[i] = anchor;
            pivot[i] = kEdge[anchor];
        }
    }
    // text rows in a dialog panel (a block at least 40% of the screen wide holding them): moved with the panel
    for (size_t i = 0; i < n; i++) {
        const Block& b = all[i];
        if (!menu(i) || deco[i] || (b.pieces < 2 && !b.projected) || b.bottom - b.top > 40) continue;
        size_t panel = n;
        for (size_t j = i; j-- > 0;) {
            // a dialog panel, not a full-height background piece (the story menu's left art holds its "exit" prompt)
            if (menu(j) && !deco[j] && all[j].hi - all[j].lo >= w * 0.4f && all[j].bottom - all[j].top < 480 * 0.75f &&
                Holds(all[j], b)) {
                panel = j;
                break;
            }
        }
        if (panel == n) continue;
        // placed as its panel is, so rows and columns stay lined up (centred text stays centred)
        pivot[i] = pivot[panel];
    }
    // the game's text: the console is reported in 16:9 mode, so the game already draws its text narrowed for a wide
    // screen (x * 0.75, about the left edge). Its glyphs keep that width; the text only moves to where its block's
    // placement puts it: x43 = x / 0.75 placed by the pivot P, P + (x43 - P) * f = x * f / 0.75 + P * (1 - f)
    constexpr float kGameWide = 0.75f;
    for (size_t i = 0; i < n; i++) {
        const Block& b = all[i];
        if (!b.projected || deco[i]) {
            L.results[i] = {pivot[i] == kNoSqueeze ? 0.0f : HudCode(f, pivot[i]), shift[i]};
            continue;
        }
        float P = pivot[i] == kNoSqueeze ? w * 0.5f : pivot[i];
        float g = f / kGameWide, add = P * (1 - f);
        if (std::fabs(g - 1) < 1e-3f) L.results[i] = {0.0f, shift[i] + add};
        else L.results[i] = {HudCode(g, add / (1 - g)), shift[i]};
    }
}

}  // namespace

int HudPiece(const Assembled& a, uint32_t w, bool textured, uint32_t tex, bool screen, const float* mx, const float* my,
             const float* proj, const float* vp) {
    if (g_hud_squeeze <= 0 || !(a.pretransformed || screen) || !(a.regs & 1) || w != 640) return -1;
    float lo = 1e30f, hi = -1e30f, top = 1e30f, bottom = -1e30f;
    auto add = [&](const float* p) {
        float x = p[0], y = p[1];
        if (proj) {
            float cx = p[0] * proj[0] + p[1] * proj[4] + p[2] * proj[8] + proj[12];
            float cy = p[0] * proj[1] + p[1] * proj[5] + p[2] * proj[9] + proj[13];
            float cw = p[0] * proj[3] + p[1] * proj[7] + p[2] * proj[11] + proj[15];
            if (!(std::fabs(cw) > 1e-6f)) return;
            x = vp[0] + (cx / cw + 1) * 0.5f * vp[2];
            y = vp[1] + (1 - cy / cw) * 0.5f * vp[3];
        } else if (mx) {
            x = p[0] * mx[0] + p[1] * mx[1] + p[2] * mx[2] + mx[3];
            y = p[0] * my[0] + p[1] * my[1] + p[2] * my[2] + my[3];
        }
        lo = std::min(lo, x), hi = std::max(hi, x);
        top = std::min(top, y), bottom = std::max(bottom, y);
    };
    if (!a.vertices.empty()) {
        uint32_t regs = 0;
        for (int r = 0; r < 16; r++) regs += (a.regs >> r) & 1;
        for (uint32_t v = 0; v < a.vertex_count && v < 8192; v++) add(&a.vertices[size_t(v) * regs * 4]);
    } else {
        const VertexElement& e = a.elements[0];
        const VertexBuffer& b = a.buffers[e.buffer];
        if (!b.src || !b.stride || (e.format != DXGI_FORMAT_R32G32B32A32_FLOAT && e.format != DXGI_FORMAT_R32G32B32_FLOAT))
            return -1;
        uint32_t n = std::min<uint32_t>(b.bytes / b.stride, 8192);
        for (uint32_t v = 0; v < n; v++) {
            float xyz[3];
            std::memcpy(xyz, b.src + size_t(v) * b.stride + e.offset, 12);
            add(xyz);
        }
    }
    if (!(hi > lo)) return -1;
    std::lock_guard<std::mutex> l(g_layout_lock);
    HudLayout& L = g_layout;
    // over the whole width: the 3D scene stretched onto the screen, fades, movies, backgrounds: left full screen
    if (lo <= w * 0.02f && hi >= w * 0.98f) {
        L.open = false;
        if (!textured && top <= 480 * 0.02f && bottom >= 480 * 0.98f) L.overlay = true;  // a full screen cover
        return -1;
    }
    // a fade drawn as full height strips (the pause darkening is ten 64 pixel columns): left as it is
    if (!textured && top <= 480 * 0.02f && bottom >= 480 * 0.98f) {
        L.open = false;
        L.overlay = true;
        return -1;
    }
    Block* open = L.open && !L.blocks.empty() ? &L.blocks.back() : nullptr;
    float pw = hi - lo, ph = bottom - top;
    bool joins = false;
    if (open && open->tex == tex && open->projected == (proj != nullptr)) {
        float lh = std::max(open->last_bottom - open->last_top, ph);
        bool same_line = std::fabs(top - open->last_top) < lh * 0.6f && lo >= open->last_lo - 1 &&
                         lo - open->last_hi < std::max(pw, open->last_hi - open->last_lo) * 2.5f + 4;
        bool next_line = top > open->last_top + lh * 0.4f && top - open->last_bottom < lh * 1.5f + 2 &&
                         lo < open->hi + 8 && hi > open->lo - 8;
        joins = same_line || next_line;
    }
    if (joins) {
        open->lo = std::min(open->lo, lo), open->hi = std::max(open->hi, hi);
        open->top = std::min(open->top, top), open->bottom = std::max(open->bottom, bottom);
        open->last_lo = lo, open->last_hi = hi, open->last_top = top, open->last_bottom = bottom;
        open->pieces++;
    } else {
        L.blocks.push_back(Block{tex, lo, hi, top, bottom, lo, hi, top, bottom, L.overlay, 1, proj != nullptr});
        L.open = true;
    }
    return int(L.blocks.size()) - 1;
}

void HudLayoutFrame() {
    std::lock_guard<std::mutex> l(g_layout_lock);
    Layout(g_layout);
}

bool HudResult(int block, float& code, float& shift, float& ypivot, float& yscale) {
    std::lock_guard<std::mutex> l(g_layout_lock);
    if (block < 0 || size_t(block) >= g_layout.results.size()) return false;
    const Result& r = g_layout.results[size_t(block)];
    code = r.code, shift = r.shift, ypivot = r.ypivot, yscale = r.yscale;
    return true;
}

void HudDump(std::FILE* f) {
    std::lock_guard<std::mutex> l(g_layout_lock);
    const HudLayout& L = g_layout;
    std::fprintf(f, "\nwidescreen 2D: %zu blocks, stage HUD %d\n", L.blocks.size(), L.hud_frames > 0 || HudBars(L.blocks, 640.0f));
    for (size_t i = 0; i < L.blocks.size(); i++) {
        const Block& b = L.blocks[i];
        Result r = i < L.results.size() ? L.results[i] : Result{};
        float k = float(int(r.code * 0.5f));
        std::fprintf(f, "  block %zu tex %08X x %.0f..%.0f y %.0f..%.0f pieces %d%s%s -> %s pivot %.0f shift %.0f%s\n", i,
                     b.tex, b.lo, b.hi, b.top, b.bottom, b.pieces, b.projected ? " text" : "", b.cover ? " cover" : "",
                     r.code > 0 ? "squeezed" : "full", r.code > 0 ? k - 4096 : 0.0f, r.shift, r.yscale > 0 ? " subtitle" : "");
    }
}

void HudFrameEnd() {
    std::lock_guard<std::mutex> l(g_layout_lock);
    HudLayout& L = g_layout;
    if (!L.blocks.empty()) {
        // the stage HUD seen in one of the last few frames with 2D: a frame or two without it changes nothing
        if (HudBars(L.blocks, 640.0f)) L.hud_frames = 3;
        else if (L.hud_frames > 0) L.hud_frames--;
    }
    L.blocks.clear();
    L.results.clear();
    L.overlay = false;
    L.open = false;
}

float HudSqueeze(const Assembled& a, uint32_t w, bool textured, uint32_t tex, bool scene_texture, bool screen,
                 const float* mx, const float* my, const float* proj, const float* vp) {
    (void)scene_texture;
    // the reference renderer draws as it goes: the frame so far decides
    int b = HudPiece(a, w, textured, tex, screen, mx, my, proj, vp);
    if (b < 0) return 0;
    HudLayoutFrame();
    float code = 0, shift = 0;
    float yp = 0, ys = 0;
    HudResult(b, code, shift, yp, ys);
    return code;
}

}  // namespace gpu
