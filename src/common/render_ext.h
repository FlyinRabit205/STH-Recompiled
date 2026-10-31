#pragma once
// Shadow's D3D12 renderer extension (src/common/render_ext.cpp)
namespace shadow {
extern bool g_hd_scene;  // settings.toml: hd_scene
void InitRender();       // GameInfo::init
}  // namespace shadow
