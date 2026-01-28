// vg_filedialog_native.m - Native macOS file dialog implementation
#import <Cocoa/Cocoa.h>
#include <stdlib.h>
#include <string.h>

// Native open file dialog - returns allocated string or NULL
char *vg_native_open_file(const char *title,
                          const char *initial_path,
                          const char *filter_name,
                          const char *filter_pattern)
{
    @autoreleasepool
    {
        NSOpenPanel *panel = [NSOpenPanel openPanel];

        if (title)
        {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }

        if (initial_path && strlen(initial_path) > 0)
        {
            NSString *path = [NSString stringWithUTF8String:initial_path];
            [panel setDirectoryURL:[NSURL fileURLWithPath:path]];
        }

        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];

        // Parse filter pattern (e.g., "*.zia;*.bas;*.txt")
        if (filter_pattern && strlen(filter_pattern) > 0)
        {
            NSString *pattern = [NSString stringWithUTF8String:filter_pattern];
            NSMutableArray *extensions = [NSMutableArray array];

            NSArray *parts = [pattern componentsSeparatedByString:@ ";"];
            for (NSString *part in parts)
            {
                NSString *trimmed =
                    [part stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
                if ([trimmed hasPrefix:@ "*."])
                {
                    [extensions addObject:[trimmed substringFromIndex:2]];
                }
            }

            if ([extensions count] > 0)
            {
                [panel setAllowedFileTypes:extensions];
            }
        }

        if ([panel runModal] == NSModalResponseOK)
        {
            NSURL *url = [[panel URLs] firstObject];
            if (url)
            {
                const char *path = [[url path] UTF8String];
                return strdup(path);
            }
        }

        return NULL;
    }
}

// Native save file dialog - returns allocated string or NULL
char *vg_native_save_file(const char *title,
                          const char *initial_path,
                          const char *default_name,
                          const char *filter_name,
                          const char *filter_pattern)
{
    @autoreleasepool
    {
        NSSavePanel *panel = [NSSavePanel savePanel];

        if (title)
        {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }

        if (initial_path && strlen(initial_path) > 0)
        {
            NSString *path = [NSString stringWithUTF8String:initial_path];
            [panel setDirectoryURL:[NSURL fileURLWithPath:path]];
        }

        if (default_name && strlen(default_name) > 0)
        {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:default_name]];
        }

        // Parse filter pattern
        if (filter_pattern && strlen(filter_pattern) > 0)
        {
            NSString *pattern = [NSString stringWithUTF8String:filter_pattern];
            NSMutableArray *extensions = [NSMutableArray array];

            NSArray *parts = [pattern componentsSeparatedByString:@ ";"];
            for (NSString *part in parts)
            {
                NSString *trimmed =
                    [part stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
                if ([trimmed hasPrefix:@ "*."])
                {
                    [extensions addObject:[trimmed substringFromIndex:2]];
                }
            }

            if ([extensions count] > 0)
            {
                [panel setAllowedFileTypes:extensions];
            }
        }

        if ([panel runModal] == NSModalResponseOK)
        {
            NSURL *url = [panel URL];
            if (url)
            {
                const char *path = [[url path] UTF8String];
                return strdup(path);
            }
        }

        return NULL;
    }
}

// Native folder selection dialog - returns allocated string or NULL
char *vg_native_select_folder(const char *title, const char *initial_path)
{
    @autoreleasepool
    {
        NSOpenPanel *panel = [NSOpenPanel openPanel];

        if (title)
        {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }

        if (initial_path && strlen(initial_path) > 0)
        {
            NSString *path = [NSString stringWithUTF8String:initial_path];
            [panel setDirectoryURL:[NSURL fileURLWithPath:path]];
        }

        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];

        if ([panel runModal] == NSModalResponseOK)
        {
            NSURL *url = [[panel URLs] firstObject];
            if (url)
            {
                const char *path = [[url path] UTF8String];
                return strdup(path);
            }
        }

        return NULL;
    }
}
