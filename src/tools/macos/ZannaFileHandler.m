//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#import <Cocoa/Cocoa.h>

static NSString *ZannaShellQuote(NSString *value) {
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

static NSString *ZannaModeForPath(NSString *path) {
    NSString *ext = [[path pathExtension] lowercaseString];
    if ([ext isEqualToString:@ "il"])
        return @ "-run";
    return @ "run";
}

static BOOL LaunchZannaInTerminal(NSString *path, NSError **outError) {
    NSString *directory = [path stringByDeletingLastPathComponent];
    NSString *mode = ZannaModeForPath(path);
    NSString *command = [NSString
        stringWithFormat:
            @ "cd %@ && /usr/local/bin/zanna %@ %@; status=$?; printf '\\n[zanna exited %%d]\\n' \"$status\"",
            ZannaShellQuote(directory),
            mode,
            ZannaShellQuote(path)];
    NSString *script = [NSString
        stringWithFormat:@ "tell application \"Terminal\"\nactivate\ndo script %@\nend tell",
                         AppleScriptQuote(command)];

    NSTask *task = [[[NSTask alloc] init] autorelease];
    [task setLaunchPath:@ "/usr/bin/osascript"];
    [task setArguments:@[ @ "-e", script ]];
    return [task launchAndReturnError:outError];
}

@interface ZannaFileHandlerDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) BOOL sawOpenEvent;
@end

@implementation ZannaFileHandlerDelegate

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
        if (!LaunchZannaInTerminal(path, &error)) {
            launchedAll = NO;
            lastError = error;
            NSLog(@ "Zanna file handler failed to launch Terminal for %@: %@", path, error);
        }
    }
    if (!launchedAll && lastError != nil) {
        NSAlert *alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@ "Zanna could not open the selected file."];
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
        ZannaFileHandlerDelegate *delegate = [[[ZannaFileHandlerDelegate alloc] init] autorelease];
        [application setDelegate:delegate];
        [application run];
    }
    return 0;
}
