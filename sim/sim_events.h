// SDL event pump shared by the sim main loop and blocking stubs (the captive
// portal loop in wifimgr_sim.cpp). Implemented in lvgl_driver_sim.cpp, which
// owns the SDL window.
#pragma once

// Poll pending SDL events. Returns false once the user closed the window or
// pressed ESC — callers should shut down (the main loop exits; blocking stubs
// call std::exit).
bool sim_pump_events();

// Latest unconsumed keypress as an SDL keycode ('c', 'd', ...), 0 if none.
// Consuming: each keypress is returned exactly once.
int sim_take_key();
