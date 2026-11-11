"""Runs Sparkle's own updater for an installed app bundle, without the app and without any window.

update-smoke.sh uses it when there is no Rio build to run: the bundle it updates is a stand-in package made by
package.sh (the same Sparkle.framework, Info.plist keys, signature and appcast as a real package), and this process
plays the app's part. It loads the bundle's Sparkle.framework through ctypes and the Objective-C runtime (nothing is
compiled), starts SPUUpdater for the bundle with a user driver that shows nothing, lets Sparkle check the feed,
download, verify the EdDSA signature and stage the update, and exits; Sparkle's installer then replaces the bundle,
as it does when Rio quits.

Usage: python3 -I sparkle_drive.py <app bundle> [timeout seconds]
Prints Sparkle's events. Exits 0 when Sparkle found a valid update, 1 otherwise, 2 when the updater did not start.
"""

import ctypes
import sys
import time
from ctypes import c_bool, c_char_p, c_double, c_long, c_size_t, c_uint64, c_void_p


def main(argv):
    if len(argv) < 2:
        print('usage: python3 -I sparkle_drive.py <app bundle> [timeout seconds]')
        return 2
    app = argv[1]
    timeout = float(argv[2]) if len(argv) > 2 else 60.0

    ctypes.CDLL('/System/Library/Frameworks/Foundation.framework/Foundation')
    cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
    objc = ctypes.CDLL('/usr/lib/libobjc.dylib')
    ctypes.CDLL(app + '/Contents/Frameworks/Sparkle.framework/Sparkle')
    for name, restype, argtypes in [
        ('objc_getClass', c_void_p, [c_char_p]),
        ('sel_registerName', c_void_p, [c_char_p]),
        ('objc_allocateClassPair', c_void_p, [c_void_p, c_char_p, c_size_t]),
        ('class_addMethod', c_bool, [c_void_p, c_void_p, c_void_p, c_char_p]),
        ('objc_registerClassPair', None, [c_void_p]),
        ('objc_getProtocol', c_void_p, [c_char_p]),
        ('class_addProtocol', c_bool, [c_void_p, c_void_p]),
    ]:
        fn = getattr(objc, name)
        fn.restype = restype
        fn.argtypes = argtypes
    msg_send = ctypes.cast(objc.objc_msgSend, c_void_p).value

    def send(restype, receiver, selector, *args):
        proto = ctypes.CFUNCTYPE(restype, c_void_p, c_void_p, *[a[0] for a in args])
        return proto(msg_send)(receiver, objc.sel_registerName(selector.encode()), *[a[1] for a in args])

    def cls(name):
        return objc.objc_getClass(name.encode())

    def nsstring(text):
        return send(c_void_p, cls('NSString'), 'stringWithUTF8String:', (c_char_p, text.encode()))

    def text_of(obj):
        return send(c_char_p, obj, 'UTF8String').decode() if obj else ''

    def describe(error):
        return text_of(send(c_void_p, error, 'localizedDescription')) if error else 'none'

    log = []
    state = {'done': False}
    keep = []

    def make(name, methods, protocol=None):
        new = objc.objc_allocateClassPair(cls('NSObject'), name.encode(), 0)
        for selector, (types, restype, argtypes, impl) in methods.items():
            imp = ctypes.CFUNCTYPE(restype, c_void_p, c_void_p, *argtypes)(impl)
            keep.append(imp)
            objc.class_addMethod(new, objc.sel_registerName(selector.encode()), ctypes.cast(imp, c_void_p), types.encode())
        if protocol:
            objc.class_addProtocol(new, objc.objc_getProtocol(protocol.encode()))
        objc.objc_registerClassPair(new)
        return send(c_void_p, send(c_void_p, new, 'alloc'), 'init')

    def note(text, done=False):
        def impl(*_):
            log.append(text)
            if done:
                state['done'] = True
        return impl

    def error_note(_self, _cmd, error, _ack):
        log.append('error: ' + describe(error))
        state['done'] = True

    def quiet(*_):
        return None

    v = 'v@:'
    # SPUUserDriver (Sparkle 2.10.1 SPUUserDriver.h): every method, none of them shows anything. A method that would
    # need the user ends the run.
    driver = make('CluSmokeUserDriver', {
        'showUpdatePermissionRequest:reply:': (v + '@@?', None, [c_void_p, c_void_p], note('permission-request', True)),
        'showUserInitiatedUpdateCheckWithCancellation:': (v + '@?', None, [c_void_p], quiet),
        'showUpdateFoundWithAppcastItem:state:reply:': (v + '@@@?', None, [c_void_p] * 3, note('needs-the-user', True)),
        'showUpdateReleaseNotesWithDownloadData:': (v + '@', None, [c_void_p], quiet),
        'showUpdateReleaseNotesFailedToDownloadWithError:': (v + '@', None, [c_void_p], quiet),
        'showUpdateNotFoundWithError:acknowledgement:': (v + '@@?', None, [c_void_p] * 2, note('not-found', True)),
        'showUpdaterError:acknowledgement:': (v + '@@?', None, [c_void_p] * 2, error_note),
        'showDownloadInitiatedWithCancellation:': (v + '@?', None, [c_void_p], quiet),
        'showDownloadDidReceiveExpectedContentLength:': (v + 'Q', None, [c_uint64], quiet),
        'showDownloadDidReceiveDataOfLength:': (v + 'Q', None, [c_uint64], quiet),
        'showDownloadDidStartExtractingUpdate': (v, None, [], quiet),
        'showExtractionReceivedProgress:': (v + 'd', None, [c_double], quiet),
        'showReadyToInstallAndRelaunch:': (v + '@?', None, [c_void_p], note('needs-the-user', True)),
        'showInstallingUpdateWithApplicationTerminated:retryTerminatingApplication:': (v + 'B@?', None, [c_bool, c_void_p], quiet),
        'showUpdateInstalledAndRelaunched:acknowledgement:': (v + 'B@?', None, [c_bool, c_void_p], quiet),
        'dismissUpdateInstallation': (v, None, [], quiet),
        'showUpdateInFocus': (v, None, [], quiet),
    }, 'SPUUserDriver')

    def found(_self, _cmd, _updater, item):
        log.append('found ' + text_of(send(c_void_p, item, 'versionString')))

    def abort(_self, _cmd, _updater, error):
        log.append('abort: ' + describe(error))

    def cycle(_self, _cmd, _updater, _check, error):
        log.append('cycle finished: ' + describe(error))
        state['done'] = True

    # SPUUpdaterDelegate: the events Rio's driver maps too (clu_sparkle_update_driver_mac.mm).
    delegate = make('CluSmokeUpdaterDelegate', {
        'updater:didFindValidUpdate:': (v + '@@', None, [c_void_p] * 2, found),
        'updater:didAbortWithError:': (v + '@@', None, [c_void_p] * 2, abort),
        'updater:didFinishUpdateCycleForUpdateCheck:error:': (v + '@q@', None, [c_void_p, c_long, c_void_p], cycle),
    })

    host = send(c_void_p, cls('NSBundle'), 'bundleWithPath:', (c_void_p, nsstring(app)))
    updater = send(c_void_p, send(c_void_p, cls('SPUUpdater'), 'alloc'),
                   'initWithHostBundle:applicationBundle:userDriver:delegate:',
                   (c_void_p, host), (c_void_p, host), (c_void_p, driver), (c_void_p, delegate))
    error = c_void_p()
    if not send(c_bool, updater, 'startUpdater:', (ctypes.POINTER(c_void_p), ctypes.pointer(error))):
        print('the updater did not start: ' + describe(error.value))
        return 2
    # SUEnableAutomaticChecks with no earlier check: Sparkle checks at once. Its callbacks arrive on the main queue,
    # which the main run loop serves.
    cf.CFRunLoopRunInMode.argtypes = [c_void_p, c_double, c_bool]
    mode = c_void_p.in_dll(cf, 'kCFRunLoopDefaultMode')
    end = time.time() + timeout
    while not state['done'] and time.time() < end:
        cf.CFRunLoopRunInMode(mode, 0.2, False)
    if not state['done']:
        log.append('timed out')
    print('\n'.join(log))
    return 0 if any(line.startswith('found ') for line in log) and not any(
        line.startswith(('error', 'needs-the-user')) for line in log) else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
