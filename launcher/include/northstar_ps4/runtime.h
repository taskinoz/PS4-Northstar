#pragma once
#include <cstddef>
#include <cstdint>

namespace northstar::ps4 {
struct ModuleProfile { const char* name; const char* sha256; std::uintptr_t imageBase; };
enum class InitStage : std::uint8_t { ModuleLoaded, EngineAvailable, UiVmAvailable };
bool Initialize(InitStage stage) noexcept;
void Log(const char* message) noexcept;
} // namespace northstar::ps4
