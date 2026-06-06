#include "dropsource.h"
#include "msgbox.h"

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/osx/private.h>
#include <wx/utils.h>
#include <wx/filename.h>

#include <AppKit/AppKit.h>

wxDragResult NSFileDragOperationToWxDragResult(NSDragOperation code)
{
	switch (code) {
		case NSDragOperationGeneric:
			return wxDragCopy;
		case NSDragOperationCopy:
			return wxDragCopy;
		case NSDragOperationMove:
			return wxDragMove;
		case NSDragOperationLink:
			return wxDragLink;
		case NSDragOperationNone:
			return wxDragNone;
		case NSDragOperationDelete:
			return wxDragNone;
		default:
			wxFAIL_MSG("Unexpected result code");
	}
	return wxDragNone;
}

@interface FileDropSourceDelegate : NSObject<NSDraggingSource>
{
	BOOL dragFinished;
	int resultCode;
	DropSource* impl;

	// Flags for drag and drop operations (wxDrag_* ).
	int m_dragFlags;
}

- (void)setImplementation:(DropSource *)dropSource flags:(int)flags;
- (BOOL)finished;
- (NSDragOperation)code;
- (NSDragOperation)draggingSession:(nonnull NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context;
- (void)draggedImage:(NSImage *)anImage movedTo:(NSPoint)aPoint;
- (void)draggedImage:(NSImage *)anImage endedAt:(NSPoint)aPoint operation:(NSDragOperation)operation;
@end

@implementation FileDropSourceDelegate

- (id)init
{
	if (self = [super init]) {
		dragFinished = NO;
		resultCode = NSDragOperationCopy;
		impl = 0;
		m_dragFlags = wxDrag_CopyOnly;
	}
	return self;
}

- (void)setImplementation:(DropSource *)dropSource flags:(int)flags
{
	impl = dropSource;
	m_dragFlags = flags;
}

- (BOOL)finished
{
	return dragFinished;
}

- (NSDragOperation)code
{
	return resultCode;
}

- (NSDragOperation)draggingSession:(nonnull NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
	(void)session;

	if (context == NSDraggingContextOutsideApplication) {
		return NSDragOperationCopy;
	}

	NSDragOperation allowedDragOperations = NSDragOperationEvery;

	// NSDragOperationGeneric also makes a drag to the trash possible
	// resulting in something we don't support (NSDragOperationDelete)

	allowedDragOperations &= ~(NSDragOperationDelete | NSDragOperationGeneric);

	if (m_dragFlags == wxDrag_CopyOnly) {
		allowedDragOperations &= ~NSDragOperationMove;
	}

	// we might adapt flags here in the future
	// context can be NSDraggingContextOutsideApplication or NSDraggingContextWithinApplication

	return allowedDragOperations;
}

- (void)draggedImage:(NSImage *)anImage movedTo:(NSPoint)aPoint
{
	(void)anImage;
	(void)aPoint;

	bool optionDown = GetCurrentKeyModifiers() & optionKey;
	wxDragResult result = optionDown ? wxDragCopy : wxDragMove;

	if (impl) {
		impl->GiveFeedback(result);
	}
 }

- (void)draggedImage:(NSImage *)anImage endedAt:(NSPoint)aPoint operation:(NSDragOperation)operation
{
	(void)anImage;
	(void)aPoint;

	resultCode = operation;
	dragFinished = YES;
}

@end

@interface FilePromiseProviderDelegate : NSObject<NSFilePromiseProviderDelegate>
{
	NSOperationQueue * workQueue;
	NSString * path;
}

- (id<NSPasteboardWriting>)pasteboardWriter;
- (NSString *)finalPath;
- (void)clear;
@end

@implementation FilePromiseProviderDelegate
- (id)init
{
	self = [super init];
	workQueue = [[NSOperationQueue alloc] init];
	workQueue.qualityOfService = NSQualityOfServiceUserInitiated;
	path = nil;
	return self;
}

- (void)clear {
	[workQueue release];
	[path release];

	workQueue = nil;
	path = nil;
}

- (NSString *)finalPath {
	return path;
}

- (id<NSPasteboardWriting>)pasteboardWriter
{
	NSFilePromiseProvider * provider = [[[NSFilePromiseProvider alloc] initWithFileType:(NSString *)kUTTypeFileURL delegate:self] autorelease];
	return provider;
}

/* Return the base filename (not a full path) for this promise item. Do not start writing the file yet. */
- (NSString *)filePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider fileNameForType:(NSString *)fileType
{
	return @"DragandDrop.Files";
}

/* Write the contents of this promise item to the provided URL and call completionHandler when done. NSFilePromiseReceiver automatically wraps this message with NSFileCoordinator when the promise destination is an NSFilePromiseReceiver. Always use the supplied URL. Note: This request shall occur on the NSOperationQueue supplied by -promiseOperationQueue.
 */
- (void)filePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider writePromiseToURL:(NSURL *)url completionHandler:(void (^)(NSError * _Nullable errorOrNil))completionHandler
{
	path = [[[url URLByDeletingLastPathComponent] path] retain];
	completionHandler(nil);
}

/* The operation queue that the write request will be issued from. If this method is not implemented, the mainOperationQueue is used. */
- (NSOperationQueue *)operationQueueForFilePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider
{
	return workQueue;
}
@end

wxDragResult DropSource::DoFileDragDrop(int flags)
{
	if (!m_data || !m_data->GetFormatCount()) {
		return wxDragError;
	}

	NSView* view = m_window->GetPeer()->GetWXWidget();
	if (!view) {
		return wxDragError;
	}

	NSEvent* theEvent = (NSEvent*)wxTheApp->MacGetCurrentEvent();
	wxASSERT_MSG(theEvent, "DoDragDrop must be called in response to a mouse down or drag event.");

	FileDropSourceDelegate* delegate = [[FileDropSourceDelegate alloc] init];
	[delegate setImplementation:this flags:flags];

	// add a dummy square as dragged image for the moment,
	// TODO: proper drag image for data
	NSSize sz = NSMakeSize(16,16);
	NSRect fillRect = NSMakeRect(0, 0, 16, 16);
	NSImage* image = [[NSImage alloc] initWithSize: sz];

	[image lockFocus];

	[[[NSColor whiteColor] colorWithAlphaComponent:0.8] set];
	NSRectFill(fillRect);
	[[NSColor blackColor] set];
	NSFrameRectWithWidthUsingOperation(fillRect,1.0f,NSCompositeDestinationOver);

	[image unlockFocus];

	NSPoint down = [theEvent locationInWindow];
	NSPoint p = [view convertPoint:down fromView:nil];

	NSMutableArray *items = [[NSMutableArray alloc] init];

	FilePromiseProviderDelegate * filePromiseDelegate = [[FilePromiseProviderDelegate alloc] init];
	NSDraggingItem* draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:[filePromiseDelegate pasteboardWriter]];
	[draggingItem setDraggingFrame:NSMakeRect(p.x, p.y, 16, 16) contents:image];
	[items addObject:draggingItem];

	[view beginDraggingSessionWithItems:items event:theEvent source:delegate];

	wxEventLoopBase * const loop = wxEventLoop::GetActive();
	while ( ![delegate finished] ) {
		loop->Dispatch();
	}

	CFStringRef path = (CFStringRef)[filePromiseDelegate finalPath];
	if (path != NULL) {
		wxString targetPath = wxCFStringRef(path).AsString();
		wxString tempPath = wxFileName::GetTempDir();
		if (!targetPath.StartsWith(tempPath)) {
			m_OutDir = targetPath;
		}
	}

	wxDragResult result = NSFileDragOperationToWxDragResult([delegate code]);
	[delegate release];
	[image release];
	[filePromiseDelegate release];

	wxWindow* mouseUpTarget = wxWindow::GetCapture();

	if (!mouseUpTarget) {
		mouseUpTarget = m_window;
	}

	if (mouseUpTarget) {
		wxMouseEvent wxevent(wxEVT_LEFT_DOWN);
		((wxWidgetCocoaImpl*)mouseUpTarget->GetPeer())->SetupMouseEvent(wxevent , theEvent) ;
		wxevent.SetEventType(wxEVT_LEFT_UP);
		mouseUpTarget->HandleWindowEvent(wxevent);
	}

	return result;
}
