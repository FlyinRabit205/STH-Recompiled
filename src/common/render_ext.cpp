// Shadow the Hedgehog renderer extension (Windows D3D12 build only).
//
// HD scene: the game draws the 3D world (and in-engine cutscenes and movies) into a 512x512 render target with a
// 512x512 depth buffer, then stretches it over the 640x480 screen. At the plain render scale that target ends up
// smaller than the screen area it covers (1152 pixels across 1440 at 1080p), so stages look soft. Giving 512x512
// targets 640/512 more scale makes one target pixel per screen pixel horizontally (and supersamples vertically).
// The small glow/blur targets (256, 128, 64) keep the plain scale.
//
// settings.toml: hd_scene = true (default)

#include "common/render_ext.h"

namespace shadow {
bool g_hd_scene = true;
}

#ifdef _WIN32
#include <memory>

#include "core/log.h"
#include "gpu/ext.h"

namespace shadow {
namespace {

constexpr uint32_t kSceneSize = 512;
constexpr float kSceneBoost = 640.0f / 512.0f;

class ShadowExtension final : public gpu::Extension {
public:
    void MakeTarget(const gpu::Surface&, bool depth, uint32_t gw, uint32_t gh, float& scale, DXGI_FORMAT&) override {
        if (!g_hd_scene || gw != kSceneSize || gh != kSceneSize) return;
        scale *= kSceneBoost;
        if (!logged_[depth ? 1 : 0]) {
            logged_[depth ? 1 : 0] = true;
            LOGI("shadow: HD scene %s target %ux%u at scale %.3f (%ux%u)", depth ? "depth" : "colour", gw, gh, scale,
                 unsigned(gw * scale + 0.5f), unsigned(gh * scale + 0.5f));
        }
    }

private:
    bool logged_[2] = {};
};

}  // namespace

void InitRender() { gpu::SetExtension(std::make_unique<ShadowExtension>()); }

}  // namespace shadow
#else
namespace shadow {
void InitRender() {}  // headless harness: no D3D12 backend
}  // namespace shadow
#endif
