#pragma once

#include <bgfx/bgfx.h>

namespace vw::client {

bgfx::ProgramHandle createMeshProgram();
bgfx::ProgramHandle createTexProgram();

}
