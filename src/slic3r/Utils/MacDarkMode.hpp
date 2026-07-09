///|/ Copyright (c) Prusa Research 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MacDarkMode_hpp_
#define slic3r_MacDarkMode_hpp_

namespace Slic3r {
namespace GUI {

#if __APPLE__
extern bool mac_dark_mode();
extern double mac_max_scaling_factor();
// Restore the pre-macOS 14 behavior of clipping a view's drawing to its
// bounds. Pass the NSView* obtained from wxWindow::GetHandle().
extern void mac_set_clips_to_bounds(void* view);
#endif


} // namespace GUI
} // namespace Slic3r

#endif // MacDarkMode_h
