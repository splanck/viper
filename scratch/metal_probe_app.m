#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#include <pthread.h>
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        printf("pthread_main_np(before)=%d\n", pthread_main_np());
        id<MTLDevice> beforeApp = MTLCreateSystemDefaultDevice();
        printf("device_before_app=%p\n", (__bridge void *)beforeApp);

        [NSApplication sharedApplication];
        printf("nsapp=%p\n", (__bridge void *)NSApp);
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        id<MTLDevice> afterApp = MTLCreateSystemDefaultDevice();
        printf("device_after_app=%p\n", (__bridge void *)afterApp);

        NSRect rect = NSMakeRect(0, 0, 100, 100);
        NSWindow *window = [[NSWindow alloc] initWithContentRect:rect
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        printf("window=%p\n", (__bridge void *)window);

        id<MTLDevice> afterWindow = MTLCreateSystemDefaultDevice();
        printf("device_after_window=%p\n", (__bridge void *)afterWindow);
    }
    return 0;
}
