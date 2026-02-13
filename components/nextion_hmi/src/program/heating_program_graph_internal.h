#pragma once

#include <stddef.h>
#include <stdint.h>

#include "heating_program_models.h"

size_t program_build_graph(const ProgramDraft *draft, uint8_t *out, size_t max_len, int width_px, int max_temp_c, int start_temp_c);
