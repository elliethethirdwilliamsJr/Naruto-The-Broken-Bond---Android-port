#pragma once

#include <cstdint>

namespace rex { class Runtime; }

struct GuestChecks {
  uint32_t registered_functions;
  uint32_t original_routines_executed;
  uint32_t routine_cases;
  uint32_t guest_threads_completed;
};

GuestChecks CheckOriginalGuestCode(rex::Runtime& runtime);
