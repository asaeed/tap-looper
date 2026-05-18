#pragma once
#include "types.h"

namespace tl {
enum class BankMode : uint8_t { Loop = 0, OneShot = 1 };
enum class Speed   : uint8_t { Half = 0, Normal = 1, Double = 2 };
} // namespace tl
