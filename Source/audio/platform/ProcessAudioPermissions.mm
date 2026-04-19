/*
  ==============================================================================

    ProcessAudioPermissions.mm
    Created for osci-render

    Obj-C++ implementation for checking macOS 14.2+ process audio capture availability.

  ==============================================================================
*/

#include "ProcessAudioPermissions.h"

#if JUCE_MAC && OSCI_PREMIUM

#include <dlfcn.h>
#include <atomic>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <AppKit/AppKit.h>

namespace ProcessAudioPermissions
{

bool isProcessTapAvailable()
{
  return __builtin_available (macOS 14.2, *);
}

namespace
{
constexpr const char* tccPath = "/System/Library/PrivateFrameworks/TCC.framework/Versions/A/TCC";

using PreflightFuncType = int (*) (CFStringRef, CFDictionaryRef);
using RequestCompletion = void (^) (bool);
using RequestFuncType = void (*) (CFStringRef, CFDictionaryRef, RequestCompletion);

static void* getTccHandle()
{
  static void* handle = dlopen (tccPath, RTLD_NOW);
  return handle;
}

static PreflightFuncType getPreflightFn()
{
  if (auto* h = getTccHandle())
    return reinterpret_cast<PreflightFuncType> (dlsym (h, "TCCAccessPreflight"));
  return nullptr;
}

static RequestFuncType getRequestFn()
{
  if (auto* h = getTccHandle())
    return reinterpret_cast<RequestFuncType> (dlsym (h, "TCCAccessRequest"));
  return nullptr;
}

static CFStringRef audioCaptureService()
{
  return CFSTR ("kTCCServiceAudioCapture");
}

// -1: no request made yet this run
//  0: last request denied
//  1: last request granted
static std::atomic<int> lastRequestState { -1 };
} // namespace

AudioCapturePermissionStatus getAudioCapturePermissionStatus()
{
  // If the OS can't do process taps, permission isn't relevant.
  if (! isProcessTapAvailable())
    return AudioCapturePermissionStatus::unavailable;

  // If we've explicitly requested this run, trust the callback result.
  const int reqState = lastRequestState.load();
  if (reqState == 1)
    return AudioCapturePermissionStatus::authorized;
  if (reqState == 0)
    return AudioCapturePermissionStatus::denied;

  // Otherwise, use preflight. On some OS builds, preflight may return the same
  // code for both "not determined" and "denied". We treat anything other than
  // explicit success as unknown so the UI can still offer a Request button.
  auto* preflight = getPreflightFn();
  if (preflight == nullptr)
    return AudioCapturePermissionStatus::unknown;

  const int result = preflight (audioCaptureService(), nullptr);
  if (result == 0)
    return AudioCapturePermissionStatus::authorized;

  return AudioCapturePermissionStatus::unknown;
}

bool requestAudioCapturePermission (std::function<void (bool granted)> completion)
{
  auto* request = getRequestFn();
  if (request == nullptr)
    return false;

  // TCC prompts are most reliable when requested from the main thread while
  // the app is active.
  dispatch_async (dispatch_get_main_queue(), ^{
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];

    // If the usage description is missing, macOS will refuse to prompt.
    if ([[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSAudioCaptureUsageDescription"] == nil)
    {
      lastRequestState.store (0);
      if (completion)
        completion (false);
      return;
    }

    request (audioCaptureService(), nullptr, ^(bool granted) {
      // Completion may arrive on a non-main queue.
      dispatch_async (dispatch_get_main_queue(), ^{
        lastRequestState.store (granted ? 1 : 0);

        if (completion)
          completion (granted);
      });
    });
  });

  return true;
}

}

#endif
