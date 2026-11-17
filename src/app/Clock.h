#pragma once
// FROZEN CONTRACT (Wave 0).
#include "util/TimeHelpers.h"

struct Clock {
    virtual ~Clock() = default;
    virtual Timestamp Now() = 0;
};

struct SystemClock final : Clock {
    Timestamp Now() override { return CaptureNow(); }
};
