#include "util/MacUtils.h"
#import <AppKit/AppKit.h>
#include <QWidget>
#include <dispatch/dispatch.h>

namespace MacUtils {

static void configureNSWindow(NSWindow *nsWin, const QColor &bgColor)
{
    nsWin.titlebarAppearsTransparent = YES;
    nsWin.titleVisibility = NSWindowTitleHidden;

    NSColor *color = [NSColor colorWithRed:bgColor.redF()
                                     green:bgColor.greenF()
                                      blue:bgColor.blueF()
                                     alpha:1.0];
    nsWin.backgroundColor = color;
}

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

        // Set the window-level appearance too (not just NSApp)
        nsWin.appearance = [NSAppearance appearanceNamed:name];

        // Apply immediately
        configureNSWindow(nsWin, bgColor);

        // Qt 6.4+ resets NSWindow properties during its own window setup.
        // Re-apply after a short delay to override Qt's reset.
        CGFloat r = bgColor.redF(), g = bgColor.greenF(), b = bgColor.blueF();
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            if (!nsWin) return;
            nsWin.titlebarAppearsTransparent = YES;
            nsWin.titleVisibility = NSWindowTitleHidden;
            nsWin.backgroundColor = [NSColor colorWithRed:r green:g blue:b alpha:1.0];
        });
    }
}

}
