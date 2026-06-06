#include "dialogex.h"

#include <AppKit/AppKit.h>

bool wxDialogEx::MacIsMouseTracking() {
    NSString* mode = [[NSRunLoop currentRunLoop] currentMode];
    return [mode isEqualToString:NSEventTrackingRunLoopMode];
}
