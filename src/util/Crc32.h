#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: Crc32.cpp (WP-A).
#include <cstddef>
#include <cstdint>

// Standard CRC-32 (reflected polynomial 0xEDB88320, as used by ZIP). Start with crc = 0.
// Crc32Update(Crc32Update(0, a, n), b, m) == Crc32Update(0, a+b, n+m).
uint32_t Crc32Update(uint32_t crc, const void* data, size_t len);
