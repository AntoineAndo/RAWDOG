#include "MacAppearance.h"
#include <AppKit/AppKit.h>

void setNativeAppearanceDark(bool useDarkAppearance)
{
    NSApplication.sharedApplication.appearance =
        [NSAppearance appearanceNamed:useDarkAppearance ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}
