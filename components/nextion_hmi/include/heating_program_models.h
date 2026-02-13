// This header is a compatibility shim. The canonical definitions of
// ProgramDraft, ProgramStage, and the PROGRAMS_* constants now live in
// components/common/include/program_types.h so that coordinator and
// temperature_profile_controller can use them without depending on nextion_hmi.
//
// All nextion_hmi code can continue to #include "program_models.h" — this
// file simply re-exports the common header.
#pragma once
#include "heating_program_types.h"
