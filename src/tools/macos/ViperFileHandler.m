//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#import <Cocoa/Cocoa.h>

static NSString *ViperShellQuote(NSString *value) {
    NSMutableString *escaped = [value mutableCopy];
    [escaped replaceOccurrencesOfString:@ "'"
                             withString:@ "'\\''"
                                options:0
                                  range:NSMakeRange(0, [escaped length])];
    NSString *quoted = [NSString stringWithFormat:@ "'%@'", escaped];
    [escaped release];
    return quoted;
}

static NSString *AppleScriptQuote(NSString *value) {
    NSMutableString *escaped = [value mutableCopy];
    [escaped replaceOccurrencesOfString:@ "\\"
                             withString:@ "\\\\"
                                options:0
                                  range:NSMakeRange(0, [escaped length])];
    [escaped replaceOccurrencesOfString:@ "\""
                             withString:@ "\\\""
                                options:0
                                  range:NSMakeRange(0, [escaped length])];
    NSString *quoted = [NSString stringWithFormat:@ "\"%@\"", escaped];
    [escaped release];
    return quoted;
}

static NSString *ViperModeForPath(NSString *path) {
    NSString *ext = [[path pathExtension] lowercaseString];
    if ([ext isEqualToString:@ "il"])
        return @ "-run";
    return @ "run";
}

static BOOL LaunchViperInTerminal(NSString *path, NSError **outError) {
    NSString *directory = [path stringByDeletingLastPathComponent];
    NSString *mode = ViperModeForPath(path);
    NSString *command = [NSString
        stringWithFormat:
            @ "cd %@ && /usr/local/bin/viper %@ %@; status=$?; printf '\\n[viper exited %%d]\\n' \"$status\"",
            ViperShellQuote(directory),
            mode,
            ViperShellQuote(path)];
    NSString *script = [NSString
        stringWithFormat:@ "tell application \"Terminal\"\nactivate\ndo script %@\nend tell",
                         AppleScriptQuote(command)];

    NSTask *task = [[[NSTask alloc] init] autorelease];
    [task setLaunchPath:@ "/usr/bin/osascript"];
    [task setArguments:@[ @ "-e", script ]];
    return [task launchAndReturnError:outError];
}

@interface ViperFileHandlerDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) BOOL sawOpenEvent;
@end

@implementation ViperFileHandlerDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    [self performSelector:@selector(terminateIfIdle) withObject:nil afterDelay:1.0];
}

- (void)application:(NSApplication *)sender openFiles:(NSArray<NSString *> *)filenames {
    self.sawOpenEvent = YES;
    BOOL launchedAll = YES;
    NSError *lastError = nil;
    for (NSString *path in filenames) {
        NSError *error = nil;
        if (!LaunchViperInTerminal(path, &error)) {
            launchedAll = NO;
            lastError = error;
            NSLog(@ "Viper file handler failed to launch Terminal for %@: %@", path, error);
        }
    }
    if (!launchedAll && lastError != nil) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@ "Viper could not open the selected file."];
        [alert setInformativeText:[lastError localizedDescription]];
        [alert runModal];
    }
    [sender replyToOpenOrPrint:(launchedAll ? NSApplicationDelegateReplySuccess
                                            : NSApplicationDelegateReplyFailure)];
    [self performSelector:@selector(terminateApp) withObject:nil afterDelay:1.0];
}

- (void)terminateIfIdle {
    if (!self.sawOpenEvent)
        [NSApp terminate:nil];
}

- (void)terminateApp {
    [NSApp terminate:nil];
}

@end

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        ViperFileHandlerDelegate *delegate = [[[ViperFileHandlerDelegate alloc] init] autorelease];
        [application setDelegate:delegate];
        [application run];
    }
    return 0;
}
