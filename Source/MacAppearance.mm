#include "MacAppearance.h"
#include <AppKit/AppKit.h>

void forceLightAppearance()
{
    NSApplication.sharedApplication.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
}
