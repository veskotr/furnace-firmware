#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core_types.h"

size_t program_build_graph(const program_draft_t *draft, uint8_t *out, size_t max_len, int width_px, int max_temp_c, int start_temp_c);
