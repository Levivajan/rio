// Copyright 2026 The Rio Authors. All rights reserved.

#import <Foundation/Foundation.h>

#include <memory>
#include <string>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "clu/browser/updater/clu_update_rules.h"
#include "clu/browser/updater/clu_update_service.h"

// Sparkle 2 (chromium/sparkle.pin) runs Clu's updates. package.sh copies Sparkle.framework into
// Clu.app/Contents/Frameworks and writes SUFeedURL, SUPublicEDKey and the automatic-update keys into Info.plist; the
// build links nothing of it, so a dev build simply has no updater. Clu loads the framework at run time, the way Brave
// does, and calls the few methods declared below (Sparkle 2.10.1 headers SPUUpdater.h, SPUStandardUserDriver.h,
// SUAppcastItem.h, SPUUpdaterDelegate.h). Sparkle checks, downloads, verifies the EdDSA signature and installs; the
// delegate turns its events into app.update (CluUpdateService).

@protocol CluSPUUpdater <NSObject>
- (instancetype)initWithHostBundle:(NSBundle*)hostBundle
                 applicationBundle:(NSBundle*)applicationBundle
                        userDriver:(id)userDriver
                          delegate:(id)delegate;
- (BOOL)startUpdater:(NSError**)error;
- (void)checkForUpdatesInBackground;
@property(nonatomic, readonly) BOOL sessionInProgress;
// Sparkle keeps both in the app's user defaults (SUEnableAutomaticChecks, SUAutomaticallyUpdate), over Info.plist's.
@property(nonatomic) BOOL automaticallyChecksForUpdates;
@property(nonatomic) BOOL automaticallyDownloadsUpdates;
@end

@protocol CluSPUStandardUserDriver <NSObject>
- (instancetype)initWithHostBundle:(NSBundle*)hostBundle delegate:(id)delegate;
@end

@protocol CluSUAppcastItem <NSObject>
@property(nonatomic, copy, readonly) NSString* displayVersionString;
@end

namespace clu {
class CluSparkleUpdateDriver;
}  // namespace clu

@interface CluSparkleDelegate : NSObject
- (instancetype)initWithDriver:(clu::CluSparkleUpdateDriver*)driver;
- (void)detach;
@end

namespace clu {

namespace {

// SUErrors.h: SUSparkleErrorDomain, SUNoUpdateError. "No update" also arrives as an abort with this code.
NSString* const kSparkleErrorDomain = @"SUSparkleErrorDomain";
constexpr NSInteger kSparkleNoUpdateError = 1001;

std::string VersionOf(id item) {
  if (![item respondsToSelector:@selector(displayVersionString)]) {
    return std::string();
  }
  return base::SysNSStringToUTF8(
      base::apple::ObjCCast<NSString>([(id<CluSUAppcastItem>)item
          displayVersionString]));
}

std::string MessageOf(NSError* error) {
  return error ? base::SysNSStringToUTF8(error.localizedDescription)
               : std::string("unknown error");
}

bool IsNoUpdate(NSError* error) {
  return [error.domain isEqualToString:kSparkleErrorDomain] &&
         error.code == kSparkleNoUpdateError;
}

}  // namespace

class CluSparkleUpdateDriver : public CluUpdateDriver {
 public:
  CluSparkleUpdateDriver() = default;
  CluSparkleUpdateDriver(const CluSparkleUpdateDriver&) = delete;
  CluSparkleUpdateDriver& operator=(const CluSparkleUpdateDriver&) = delete;
  ~CluSparkleUpdateDriver() override { [delegate_ detach]; }

  // CluUpdateDriver:
  bool Start(Client* client) override {
    NSBundle* host = base::apple::OuterBundle();
    NSDictionary* info = host.infoDictionary;
    const base::expected<UpdateConfig, std::string> config = ParseUpdateConfig(
        base::SysNSStringToUTF8(
            base::apple::ObjCCast<NSString>(info[@"SUFeedURL"])),
        base::SysNSStringToUTF8(
            base::apple::ObjCCast<NSString>(info[@"SUPublicEDKey"])));
    if (!config.has_value()) {
      VLOG(1) << "Clu update: off, " << config.error();
      return false;
    }
    const base::FilePath framework = base::apple::OuterBundlePath()
                                         .Append("Contents")
                                         .Append("Frameworks")
                                         .Append("Sparkle.framework");
    NSBundle* sparkle = [NSBundle
        bundleWithPath:base::apple::FilePathToNSString(framework)];
    NSError* error = nil;
    if (!sparkle || ![sparkle loadAndReturnError:&error]) {
      LOG(WARNING) << "Clu update: off, Sparkle did not load: "
                   << MessageOf(error);
      return false;
    }
    Class updater_class = NSClassFromString(@"SPUUpdater");
    Class user_driver_class = NSClassFromString(@"SPUStandardUserDriver");
    if (!updater_class || !user_driver_class) {
      LOG(WARNING) << "Clu update: off, Sparkle has no SPUUpdater";
      return false;
    }
    client_ = client;
    feed_url_ = config->feed_url.spec();
    delegate_ = [[CluSparkleDelegate alloc] initWithDriver:this];
    // Sparkle's own windows show only where Sparkle needs the user (an update that cannot install silently); Clu's
    // sidebar shows a ready update.
    user_driver_ = [(id<CluSPUStandardUserDriver>)[user_driver_class alloc]
        initWithHostBundle:host
                  delegate:nil];
    updater_ = [(id<CluSPUUpdater>)[updater_class alloc]
        initWithHostBundle:host
         applicationBundle:host
                userDriver:user_driver_
                  delegate:delegate_];
    if (![updater_ startUpdater:&error]) {
      LOG(WARNING) << "Clu update: off, " << MessageOf(error);
      [delegate_ detach];
      delegate_ = nil;
      updater_ = nil;
      user_driver_ = nil;
      client_ = nullptr;
      return false;
    }
    return true;
  }

  void CheckNow() override {
    // A session Sparkle runs already reports its own events.
    if (updater_ && !updater_.sessionInProgress) {
      [updater_ checkForUpdatesInBackground];
    }
  }

  void SetAutomatic(bool automatic) override {
    if (!updater_) {
      return;
    }
    // Off stops the scheduled checks and the silent downloads; "Check for Updates…" still checks, and downloads
    // through SetDownloads().
    updater_.automaticallyChecksForUpdates = automatic;
    updater_.automaticallyDownloadsUpdates = automatic;
  }

  void SetDownloads(bool downloads) override {
    // checkForUpdatesInBackground downloads silently and reaches willInstallUpdateOnQuit only with this on; off, it
    // would show Sparkle's own update alert.
    if (updater_) {
      updater_.automaticallyDownloadsUpdates = downloads;
    }
  }

  bool InstallNow() override {
    if (!install_) {
      return false;
    }
    // Sparkle quits Clu (the normal quit path, so pages may still ask to stay), installs and relaunches. It may be
    // called again if the quit is cancelled.
    install_();
    return true;
  }

  Client* client() { return client_; }
  // The checked feed. Sparkle would also take one from user defaults (the deprecated setFeedURL:); the delegate
  // answers with this one, so only what package.sh wrote and the rules accepted is ever read.
  const std::string& feed_url() const { return feed_url_; }
  void set_install(void (^install)(void)) { install_ = install; }

 private:
  raw_ptr<Client> client_ = nullptr;
  std::string feed_url_;
  CluSparkleDelegate* __strong delegate_;
  id __strong user_driver_;
  id<CluSPUUpdater> __strong updater_;
  void (^__strong install_)(void);
};

std::unique_ptr<CluUpdateDriver> CreatePlatformUpdateDriver() {
  return std::make_unique<CluSparkleUpdateDriver>();
}

}  // namespace clu

@implementation CluSparkleDelegate {
  raw_ptr<clu::CluSparkleUpdateDriver> _driver;
}

- (instancetype)initWithDriver:(clu::CluSparkleUpdateDriver*)driver {
  if ((self = [super init])) {
    _driver = driver;
  }
  return self;
}

- (void)detach {
  _driver = nullptr;
}

- (clu::CluUpdateDriver::Client*)client {
  return _driver ? _driver->client() : nullptr;
}

// SPUUpdaterDelegate. Sparkle calls these on the main thread, which is the browser's UI thread.

- (NSString*)feedURLStringForUpdater:(id)updater {
  return _driver ? base::SysUTF8ToNSString(_driver->feed_url()) : nil;
}

- (BOOL)updater:(id)updater
    mayPerformUpdateCheck:(NSInteger)updateCheck
                    error:(NSError**)error {
  if (auto* client = [self client]) {
    client->OnCheckStarted();
  }
  return YES;
}

- (void)updater:(id)updater didFindValidUpdate:(id)item {
  if (auto* client = [self client]) {
    client->OnUpdateFound(clu::VersionOf(item));
  }
}

- (void)updaterDidNotFindUpdate:(id)updater error:(NSError*)error {
  if (auto* client = [self client]) {
    client->OnUpdateNotFound();
  }
}

- (void)updater:(id)updater
    failedToDownloadUpdate:(id)item
                     error:(NSError*)error {
  if (auto* client = [self client]) {
    client->OnUpdateError(clu::MessageOf(error));
  }
}

// Downloaded and verified: Clu keeps the install block for its Relaunch button and takes over the reminder, so
// Sparkle shows no window of its own. Sparkle still installs when Clu quits.
- (BOOL)updater:(id)updater
       willInstallUpdateOnQuit:(id)item
    immediateInstallationBlock:(void (^)(void))immediateInstallHandler {
  if (!_driver) {
    return NO;
  }
  _driver->set_install(immediateInstallHandler);
  if (auto* client = [self client]) {
    client->OnUpdateReady(clu::VersionOf(item));
  }
  return YES;
}

- (void)updater:(id)updater didAbortWithError:(NSError*)error {
  if (clu::IsNoUpdate(error)) {
    return;
  }
  if (auto* client = [self client]) {
    client->OnUpdateError(clu::MessageOf(error));
  }
}

- (void)updater:(id)updater
    didFinishUpdateCycleForUpdateCheck:(NSInteger)updateCheck
                                 error:(NSError*)error {
  if (auto* client = [self client]) {
    client->OnCycleFinished();
  }
}

@end
