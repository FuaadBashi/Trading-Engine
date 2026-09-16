#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace te {

// Vendored, not linked from a crypto library: this is a corruption-detection check, not a
// security boundary, and a small stable reference implementation is proportionate to that use.
// See the capture-validation design discussion for the reasoning.
std::string sha256Hex(std::span<const std::byte> data);

}  // namespace te
