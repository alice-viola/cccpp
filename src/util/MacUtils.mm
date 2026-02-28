#include "util/MacUtils.h"
#import <AppKit/AppKit.h>
#include <QWidget>

namespace MacUtils {

void applyTitleBarStyle(QWidget *window, bool dark, const QColor &bgColor)
{
    @autoreleasepool {
        NSAppearanceName name = dark
            ? NSAppearanceNameDarkAqua
            : NSAppearanceNameAqua;
        [NSApp setAppearance:[NSAppearance appearanceNamed:name]];

        if (!window) return;
        NSView *view = reinterpret_cast<NSView *>(window->winId());
        NSWindow *nsWin = [view window];
        if (!nsWin) return;

        nsWin.titlebarAppearsTransparent = YES;
        nsWin.titleVisibility = NSWindowTitleHidden;
        nsWin.styleMask |= NSWindowStyleMaskFullSizeContentView;

        NSColor *color = [NSColor colorWithRed:bgColor.redF()
                                         green:bgColor.greenF()
                                          blue:bgColor.blueF()
                                         alpha:1.0];
        nsWin.backgroundColor = color;
    }
}

}
