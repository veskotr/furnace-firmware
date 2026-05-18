#pragma once

/**
 * @brief Initialize the fan controller.
 *
 * Configures the fan GPIO output and subscribes to the temperature
 * processor and coordinator event streams. In auto mode (no program
 * running) the fan follows the configured on/off temperature thresholds.
 * When a heating program starts, the fan is held on for the duration
 * of the program; when the program ends, control returns to auto mode.
 */
void fan_controller_init(void);
