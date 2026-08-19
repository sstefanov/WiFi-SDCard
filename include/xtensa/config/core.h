#pragma once

// Compatibility shim for toolchain-xtensa packages that ship core-isa.h
// without core.h while framework assembly still includes <xtensa/config/core.h>.
#include <xtensa/config/core-isa.h>
