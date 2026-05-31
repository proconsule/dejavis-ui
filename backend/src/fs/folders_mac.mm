#include "folders.h"
#import <Foundation/Foundation.h>

std::string getMacAppSupportPath() {
    @autoreleasepool {
        NSArray *urls = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];
        NSURL *appSupportURL = [[urls firstObject] URLByAppendingPathComponent:@"DejavisUI"];

        [[NSFileManager defaultManager] createDirectoryAtURL:appSupportURL withIntermediateDirectories:YES attributes:nil error:nil];

        return [[appSupportURL path] UTF8String];
    }
}

std::string getMacResourcesFrontendPath() {
    @autoreleasepool {
        NSBundle *mainBundle = [NSBundle mainBundle];
        NSString *frontendPath = [mainBundle pathForResource:@"frontend" ofType:nil];

        if (frontendPath) {
            return [frontendPath UTF8String];
        }
        return "./webpages";
    }
}