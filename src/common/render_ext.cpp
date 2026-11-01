// Shadow the Hedgehog renderer extension (Windows D3D12 build only).
//
// HD scene: the game draws the 3D world (and in-engine cutscenes and movies) into a 512x512 render target with a
// 512x512 depth buffer, then stretches it over the 640x480 screen. At the plain render scale that target ends up
// smaller than the screen area it covers (1152 pixels across 1440 at 1080p), so stages look soft. Giving 512x512
// targets 640/512 more scale makes one target pixel per screen pixel horizontally (and supersamples vertically).
// The small glow/blur targets (256, 128, 64) keep the plain scale.
//
// settings.toml: hd_scene = true (default)
//
// Glow target: each frame the game copies the scene into a second 512x512 target with a pre-transformed quad, draws
// glowing effects over it, blurs it and adds the blur back in. The copy quad is screen sized (480 rows), so the
// target's last 32 rows are never overwritten: the blur added back each frame piles up there until it saturates,
// and the blur chain spreads it into the bottom of the screen (a white and rainbow smear, seen in Cosmic Fall).
// The first draw of a frame into a 512x512 target that is such a copy (pre-transformed, no blending, no depth
// buffer) now clears the target to black first.

#include "common/render_ext.h"
#include "common/pack_import.h"
#include "common/pc_text.h"

namespace shadow {
bool g_hd_scene = true;
}

#ifdef _WIN32
#include <memory>
#include <unordered_map>

#include "core/log.h"
#include "gpu/ext.h"

namespace shadow {
namespace {

constexpr uint32_t kSceneSize = 512;
constexpr float kSceneBoost = 640.0f / 512.0f;

class ShadowExtension final : public gpu::Extension {
public:
    // the settings menu turned HD scene on or off: every target is made again
    bool Settings() override {
        bool changed = applied_ != g_hd_scene;
        applied_ = g_hd_scene;
        return changed;
    }

    void MakeTarget(const gpu::Surface&, bool depth, uint32_t gw, uint32_t gh, float& scale, DXGI_FORMAT&) override {
        if (!g_hd_scene || gw != kSceneSize || gh != kSceneSize) return;
        scale *= kSceneBoost;
        if (!logged_[depth ? 1 : 0]) {
            logged_[depth ? 1 : 0] = true;
            LOGI("shadow: HD scene %s target %ux%u at scale %.3f (%ux%u)", depth ? "depth" : "colour", gw, gh, scale,
                 unsigned(gw * scale + 0.5f), unsigned(gh * scale + 0.5f));
        }
    }

    bool Bound(gpu::Context& ctx, const gpu::State& s, gpu::Target* rt, gpu::Target* ds, bool pretransformed) override {
        if (!rt || rt->depth || rt->gw != kSceneSize || rt->gh != kSceneSize) return false;
        const uint64_t frame = ctx.Presents();
        auto& seen = seen_[rt->data];
        if (seen == frame + 1) return false;  // not the first draw into it this frame
        seen = frame + 1;
        if (ds || !pretransformed || s.simple[gpu::kMBlendEnable >> 2]) return false;
        static const float black[4] = {};
        ctx.List()->ClearRenderTargetView(ctx.View(*rt), black, 0, nullptr);
        if (!logged_clear_) {
            logged_clear_ = true;
            LOGI("shadow: glow target %08X cleared before its copy", rt->data);
        }
        return false;
    }

private:
    std::unordered_map<uint32_t, uint64_t> seen_;  // target data -> presents + 1 at its first draw that frame
    bool logged_clear_ = false;
    bool applied_ = g_hd_scene;
    bool logged_[2] = {};
};

}  // namespace

void InitRender() {
    InitPcText();
    ImportTexturePack();  // a texture pack dropped into textures\pack, before the game reads the paks
    gpu::SetExtension(std::make_unique<ShadowExtension>());
}

}  // namespace shadow
#else
namespace shadow {
void InitRender() { InitPcText(); }  // headless harness: no D3D12 backend
}  // namespace shadow
#endif
