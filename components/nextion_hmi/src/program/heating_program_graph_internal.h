#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core_types.h"

size_t program_build_graph(const program_draft_t *draft, uint8_t *out, size_t max_len, int width_px, int max_temp_c, int start_temp_c);

/**
 * @brief Convert a temperature to the byte (0..255) expected by the Nextion
 *        graph element's `add <id>,<ch>,<value>` command.
 *
 * The graph component on the Nextion side scales the byte to its configured
 * pixel height (`h` attribute), so the encoding side must ALWAYS use the
 * 0..255 range regardless of the on-screen pixel height — otherwise curves
 * encoded against `h` directly come out compressed by `h/255` and the
 * projected and live traces don't overlap.
 *
 * Centralising the encoder here guarantees the projected curve and the live
 * waveform produce the same byte for the same temperature, byte-for-byte.
 */
uint8_t program_graph_encode_temp(float temp_c, int max_temp_c);

/**
 * @brief Rate-based total stage duration (excluding the trailing cooldown).
 *
 * Walks the program's is_set stages forward from start_temp_c, computing
 * each stage's duration the same way the runtime does:
 *   - Rate-based ( |Δtemp| / rate ) where the stage has a real temp change
 *     and a configured rate.
 *   - cooldown_rate_x10 used as a fallback rate for cooling-direction
 *     stages with no own rate.
 *   - t_min used directly for pure dwell stages.
 *
 * Single source of truth for the projected-curve renderer and the live-
 * waveform total — keeps them aligned on the X axis.
 *
 * @param draft              The program to walk. Must be non-NULL.
 * @param start_temp_c       Chamber temperature at program start (real ambient).
 * @param cooldown_rate_x10  Program-level cooldown rate in 0.1 °C/min units
 *                           (used only for stages that lack their own rate
 *                           and are cooling-direction). Pass 0 to disable.
 * @param out_last_temp      Out (nullable): final stage target temperature,
 *                           useful for adding the implicit cooldown after.
 * @return Total milliseconds of all is_set stages (no cooldown). 0 on bad input.
 */
uint32_t program_calculate_stages_duration_ms(const program_draft_t *draft,
                                              int start_temp_c,
                                              int cooldown_rate_x10,
                                              float *out_last_temp);
