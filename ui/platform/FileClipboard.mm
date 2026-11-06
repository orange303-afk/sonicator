#include "FileClipboard.h"
#import <AppKit/AppKit.h>
namespace sonicator {
bool copyFilesToClipboard(const juce::Array<juce::File>& files)
{
    @autoreleasepool
    {
        NSMutableArray<NSURL*>* urls = [NSMutableArray array];
        for (const auto& file : files)
            if (file.existsAsFile())
                [urls addObject:[NSURL fileURLWithPath:[NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()]]];
        if (urls.count == 0) return false;
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        if ([pasteboard writeObjects:urls]) return true;
        juce::StringArray paths;
        for (const auto& file : files) if (file.existsAsFile()) paths.add(file.getFullPathName());
        juce::SystemClipboard::copyTextToClipboard(paths.joinIntoString("\n"));
        return false;
    }
}
} // namespace sonicator
