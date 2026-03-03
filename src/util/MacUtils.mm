#include "util/MacUtils.h"
#import <AppKit/AppKit.h>
#include <QWidget>
#include <dispatch/dispatch.h>

// Persistent observer that uses KVO on titlebarAppearsTransparent.
// KVO fires synchronously the instant Qt resets the property, so we
// set it back to YES before the frame is even composited — no flicker.
@interface CccppTitleBarKeeper : NSObject
+ (instancetype)shared;
- (void)watchWindow:(NSWindow *)window r:(CGFloat)r g:(CGFloat)g b:(CGFloat)b dark:(BOOL)dark;
@end

@implementation CccppTitleBarKeeper {
    NSMapTable *_windows; // weak NSWindow -> NSDictionary (color info)
    BOOL _applying;       // guard against KVO recursion
}

+ (instancetype)shared {
    static CccppTitleBarKeeper *inst;
    static dispatch_once_t token;
    dispatch_once(&token, ^{ inst = [CccppTitleBarKeeper new]; });
    return inst;
}

- (instancetype)init {
    if ((self = [super init])) {
        _windows = [[NSMapTable weakToStrongObjectsMapTable] retain];
        _applying = NO;
    }
    return self;
}

- (void)applyToWindow:(NSWindow *)win info:(NSDictionary *)info {
    _applying = YES;
    CGFloat r = [info[@"r"] doubleValue];
    CGFloat g = [info[@"g"] doubleValue];
    CGFloat b = [info[@"b"] doubleValue];
    BOOL dark = [info[@"dark"] boolValue];
    NSAppearanceName name = dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
    win.appearance = [NSAppearance appearanceNamed:name];
    win.titlebarAppearsTransparent = YES;
    win.titleVisibility = NSWindowTitleHidden;
    win.backgroundColor = [NSColor colorWithRed:r green:g blue:b alpha:1.0];
    _applying = NO;
}

- (void)watchWindow:(NSWindow *)window r:(CGFloat)r g:(CGFloat)g b:(CGFloat)b dark:(BOOL)dark {
    NSDictionary *existing = [_windows objectForKey:window];
    NSDictionary *info = @{@"r": @(r), @"g": @(g), @"b": @(b), @"dark": @(dark)};
    [_windows setObject:info forKey:window];

    if (!existing) {
        // First time watching this window — register KVO
        [window addObserver:self
                 forKeyPath:@"titlebarAppearsTransparent"
                    options:NSKeyValueObservingOptionNew
                    context:NULL];
    }

    [self applyToWindow:window info:info];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context {
    if (_applying) return;
    NSWindow *win = (NSWindow *)object;
    if (!win.titlebarAppearsTransparent) {
        NSDictionary *info = [_windows objectForKey:win];
        if (info)
            [self applyToWindow:win info:info];
    }
}

@end

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

        CGFloat r = bgColor.redF(), g = bgColor.greenF(), b = bgColor.blueF();

        // Register with keeper (adds KVO on first call, updates color on subsequent).
        // KVO on titlebarAppearsTransparent fires synchronously when Qt resets it,
        // so we fix it before the frame is composited — zero flicker.
        [[CccppTitleBarKeeper shared] watchWindow:nsWin r:r g:g b:b dark:dark];

        // Deferred re-apply for initial window setup (Qt may not have finished
        // configuring the NSWindow yet during the first showEvent).
        for (double delay : {0.05, 0.15, 0.5}) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)),
                           dispatch_get_main_queue(), ^{
                if (!nsWin) return;
                NSDictionary *info = @{@"r": @(r), @"g": @(g), @"b": @(b), @"dark": @(dark)};
                [[CccppTitleBarKeeper shared] applyToWindow:nsWin info:info];
            });
        }
    }
}

} // namespace MacUtils
