#import <AppKit/AppKit.h>

void setNAMplifiedDockIcon()
{
    NSURL* iconURL = [NSBundle.mainBundle URLForResource:@"NAMplified" withExtension:@"icns"];
    if (iconURL != nil)
    {
        NSImage* icon = [[NSImage alloc] initWithContentsOfURL:iconURL];
        if (icon != nil)
            NSApplication.sharedApplication.applicationIconImage = icon;
       #if ! __has_feature(objc_arc)
        [icon release];
       #endif
    }
}
