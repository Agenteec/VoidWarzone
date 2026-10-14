#pragma once

#include <bgfx/bgfx.h>

namespace vw::client {

bgfx::ProgramHandle createMeshProgram();
bgfx::ProgramHandle createTexProgram();
bgfx::ProgramHandle createTerrainProgram();
bgfx::ProgramHandle createShadowProgram();
bgfx::ProgramHandle createShadowTexProgram();

}
