#pragma once

// macOS-only: forces the app's overall appearance to Aqua (light), so the
// OS-drawn native title bar renders in its standard light grey rather than
// following the system's dark mode (which paints it near-black) -- the
// Platinum theme is a fixed light chrome, not a light/dark pair, so the
// native title bar should always match it rather than tracking the user's
// system-wide appearance setting.
void forceLightAppearance();
