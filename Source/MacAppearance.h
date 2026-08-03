#pragma once

// macOS-only: pins the app's overall appearance to Aqua or Dark Aqua
// regardless of the system-wide setting, so the OS-drawn native title bar
// always matches RawdogLookAndFeel's own active palette (see
// RawdogLookAndFeel::Palette::isDarkModeEnabled()) instead of independently
// following the system's dark mode.
void setNativeAppearanceDark(bool useDarkAppearance);
