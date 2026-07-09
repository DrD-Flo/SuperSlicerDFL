#import "MacDarkMode.hpp"

#import <algorithm>

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <AppKit/NSScreen.h>

@interface MacDarkMode : NSObject {}
@end

@implementation MacDarkMode

namespace Slic3r {
namespace GUI {

bool mac_dark_mode()
{
    NSString *style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return style && [style isEqualToString:@"Dark"];

}

double mac_max_scaling_factor()
{
    double scaling = 1.;
    if ([NSScreen screens] == nil) {
        scaling = [[NSScreen mainScreen] backingScaleFactor];
    } else {
	    for (int i = 0; i < [[NSScreen screens] count]; ++ i)
	    	scaling = std::max<double>(scaling, [[[NSScreen screens] objectAtIndex:0] backingScaleFactor]);
	}
    return scaling;
}

void mac_set_clips_to_bounds(void* view)
{
#if defined(MAC_OS_VERSION_14_0) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_14_0
    // Since macOS 14 views no longer clip their drawing to their bounds by
    // default, which lets the content of scrolled windows be drawn over the
    // sibling controls above them.
    if (@available(macOS 14.0, *)) {
        NSView* nsview = (__bridge NSView*)view;
        nsview.clipsToBounds = YES;
    }
#else
    (void)view;
#endif
}

}
}

@end
