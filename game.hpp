#pragma once

#include "32blit.hpp"

// Read-only view of the game for the self-test and the clip recorder.
namespace dbg {
float player_x();
bool dialog_open();
bool dialog_typing();
bool torch_on();
int lang();
float wind_at_player();
uint32_t render_us();  // how long the last render() took
}
