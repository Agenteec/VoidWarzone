#include "render/Shaders.h"

#include <cstdint>

#include "essl/vs_mesh.sc.bin.h"
#include "essl/fs_mesh.sc.bin.h"
#include "essl/vs_tex.sc.bin.h"
#include "essl/fs_tex.sc.bin.h"
#include "essl/vs_terrain.sc.bin.h"
#include "essl/fs_terrain.sc.bin.h"
#include "essl/vs_shadow.sc.bin.h"
#include "essl/fs_shadow.sc.bin.h"
#include "essl/vs_shadow_tex.sc.bin.h"
#include "essl/fs_shadow_tex.sc.bin.h"
#include "glsl/vs_mesh.sc.bin.h"
#include "glsl/fs_mesh.sc.bin.h"
#include "glsl/vs_tex.sc.bin.h"
#include "glsl/fs_tex.sc.bin.h"
#include "glsl/vs_terrain.sc.bin.h"
#include "glsl/fs_terrain.sc.bin.h"
#include "glsl/vs_shadow.sc.bin.h"
#include "glsl/fs_shadow.sc.bin.h"
#include "glsl/vs_shadow_tex.sc.bin.h"
#include "glsl/fs_shadow_tex.sc.bin.h"
#include "spirv/vs_mesh.sc.bin.h"
#include "spirv/fs_mesh.sc.bin.h"
#include "spirv/vs_tex.sc.bin.h"
#include "spirv/fs_tex.sc.bin.h"
#include "spirv/vs_terrain.sc.bin.h"
#include "spirv/fs_terrain.sc.bin.h"
#include "spirv/vs_shadow.sc.bin.h"
#include "spirv/fs_shadow.sc.bin.h"
#include "spirv/vs_shadow_tex.sc.bin.h"
#include "spirv/fs_shadow_tex.sc.bin.h"
#if defined(_WIN32)
#include "dxbc/vs_mesh.sc.bin.h"
#include "dxbc/fs_mesh.sc.bin.h"
#include "dxbc/vs_tex.sc.bin.h"
#include "dxbc/fs_tex.sc.bin.h"
#include "dxbc/vs_terrain.sc.bin.h"
#include "dxbc/fs_terrain.sc.bin.h"
#include "dxbc/vs_shadow.sc.bin.h"
#include "dxbc/fs_shadow.sc.bin.h"
#include "dxbc/vs_shadow_tex.sc.bin.h"
#include "dxbc/fs_shadow_tex.sc.bin.h"
#include "dxil/vs_mesh.sc.bin.h"
#include "dxil/fs_mesh.sc.bin.h"
#include "dxil/vs_tex.sc.bin.h"
#include "dxil/fs_tex.sc.bin.h"
#include "dxil/vs_terrain.sc.bin.h"
#include "dxil/fs_terrain.sc.bin.h"
#include "dxil/vs_shadow.sc.bin.h"
#include "dxil/fs_shadow.sc.bin.h"
#include "dxil/vs_shadow_tex.sc.bin.h"
#include "dxil/fs_shadow_tex.sc.bin.h"
#endif

namespace vw::client {

namespace {

struct Blob {
    const std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
};

struct ShaderSet {
    Blob dxbc, dxil, spirv, glsl, essl;
};

#define VW_BLOB(n) Blob{n, static_cast<std::uint32_t>(sizeof(n))}
#if defined(_WIN32)
#define VW_SHADER(n) ShaderSet{VW_BLOB(n##_dxbc), VW_BLOB(n##_dxil), VW_BLOB(n##_spv), VW_BLOB(n##_glsl), VW_BLOB(n##_essl)}
#else
#define VW_SHADER(n) ShaderSet{{}, {}, VW_BLOB(n##_spv), VW_BLOB(n##_glsl), VW_BLOB(n##_essl)}
#endif

Blob select(const ShaderSet& s) {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Direct3D11:
        return s.dxbc;
    case bgfx::RendererType::Direct3D12:
        return s.dxil.data ? s.dxil : s.dxbc;
    case bgfx::RendererType::Vulkan:
        return s.spirv;
    case bgfx::RendererType::OpenGL:
        return s.glsl;
    case bgfx::RendererType::OpenGLES:
        return s.essl;
    default:
        return {};
    }
}

bgfx::ShaderHandle load(const ShaderSet& set, const char* name) {
    const Blob b = select(set);
    if (!b.data)
        return BGFX_INVALID_HANDLE;
    const auto h = bgfx::createShader(bgfx::makeRef(b.data, b.size));
    bgfx::setName(h, name);
    return h;
}

bgfx::ProgramHandle program(const ShaderSet& vsSet, const char* vsName, const ShaderSet& fsSet, const char* fsName) {
    const auto vs = load(vsSet, vsName);
    const auto fs = load(fsSet, fsName);
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        if (bgfx::isValid(vs))
            bgfx::destroy(vs);
        if (bgfx::isValid(fs))
            bgfx::destroy(fs);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vs, fs, true);
}

#define VW_PROGRAM(vs, fs) program(VW_SHADER(vs), #vs, VW_SHADER(fs), #fs)

}

bgfx::ProgramHandle createMeshProgram() {
    return VW_PROGRAM(vs_mesh, fs_mesh);
}

bgfx::ProgramHandle createTexProgram() {
    return VW_PROGRAM(vs_tex, fs_tex);
}

bgfx::ProgramHandle createTerrainProgram() {
    return VW_PROGRAM(vs_terrain, fs_terrain);
}

bgfx::ProgramHandle createShadowProgram() {
    return VW_PROGRAM(vs_shadow, fs_shadow);
}

bgfx::ProgramHandle createShadowTexProgram() {
    return VW_PROGRAM(vs_shadow_tex, fs_shadow_tex);
}

}
