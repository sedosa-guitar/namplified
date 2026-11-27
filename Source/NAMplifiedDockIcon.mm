#import <AppKit/AppKit.h>

void setNAMplifiedDockIcon()
{
    NSBundle* bundle = NSBundle.mainBundle;
    NSString* iconFile = [bundle objectForInfoDictionaryKey:@"CFBundleIconFile"];
    if (![iconFile isKindOfClass:NSString.class] || iconFile.length == 0)
        return;

    NSURL* iconURL = [bundle URLForResource:iconFile.stringByDeletingPathExtension
                             withExtension:iconFile.pathExtension];
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
