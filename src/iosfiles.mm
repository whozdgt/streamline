#include "iosfiles.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// Built with ARC (see CMakeLists.txt).

@interface StreamlineFolderPicker : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) void (^done)(NSString *dir, NSString *error);
@end

namespace {
StreamlineFolderPicker *g_picker = nil;      // kept while the picker is on screen

UIViewController *topController()
{
    UIWindow *window = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        for (UIWindow *w in ((UIWindowScene *)scene).windows)
            if (w.isKeyWindow) { window = w; break; }
        if (!window && ((UIWindowScene *)scene).windows.count) window = ((UIWindowScene *)scene).windows.firstObject;
        if (window) break;
    }
    UIViewController *vc = window.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}
} // namespace

@implementation StreamlineFolderPicker
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    NSURL *src = urls.firstObject;
    void (^done)(NSString *, NSString *) = self.done;
    g_picker = nil;
    if (!src) return;
    // the copy is made away from the screen's thread: a big folder in iCloud has to download first
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *error = nil, *dir = nil;
        const BOOL access = [src startAccessingSecurityScopedResource];
        NSFileManager *fm = NSFileManager.defaultManager;
        NSURL *docs = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
        NSURL *dest = [docs URLByAppendingPathComponent:src.lastPathComponent isDirectory:YES];
        __block NSError *copyError = nil;
        NSError *coordError = nil;
        // a coordinated read: files that only live in the cloud are downloaded before they are copied
        [[[NSFileCoordinator alloc] initWithFilePresenter:nil]
            coordinateReadingItemAtURL:src options:NSFileCoordinatorReadingWithoutChanges error:&coordError
                            byAccessor:^(NSURL *readable) {
                                // the folder again: replace the old copy, so files removed there go here too
                                NSURL *tmp = [docs URLByAppendingPathComponent:@".streamline-import" isDirectory:YES];
                                [fm removeItemAtURL:tmp error:nil];
                                if ([fm copyItemAtURL:readable toURL:tmp error:&copyError]) {
                                    [fm removeItemAtURL:dest error:nil];
                                    [fm moveItemAtURL:tmp toURL:dest error:&copyError];
                                }
                            }];
        if (access) [src stopAccessingSecurityScopedResource];
        if (coordError || copyError)
            error = [NSString stringWithFormat:@"Couldn't copy the folder: %@", (coordError ?: copyError).localizedDescription];
        else
            dir = dest.path;
        dispatch_async(dispatch_get_main_queue(), ^{ if (done) done(dir, error); });
    });
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    void (^done)(NSString *, NSString *) = self.done;
    g_picker = nil;
    if (done) done(nil, nil);      // nothing chosen: no folder and no error
}
@end

namespace iosfiles {
void importFolder(std::function<void(const QString &, const QString &)> done)
{
    UIViewController *top = topController();
    if (!top) { done(QString(), QStringLiteral("Couldn't open the Files app's picker.")); return; }
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]];
    picker.allowsMultipleSelection = NO;
    g_picker = [StreamlineFolderPicker new];
    auto cb = std::make_shared<std::function<void(const QString &, const QString &)>>(std::move(done));
    g_picker.done = ^(NSString *dir, NSString *error) {
        (*cb)(dir ? QString::fromNSString(dir) : QString(), error ? QString::fromNSString(error) : QString());
    };
    picker.delegate = g_picker;
    [top presentViewController:picker animated:YES completion:nil];
}
}
