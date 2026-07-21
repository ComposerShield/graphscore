// DISPOSABLE — M0 spike, not production code
//
// This path opens a real window and cannot be verified by an agent.
// Human-observed PASS on render, resize, and keyboard focus, 2026-07-21,
// macOS 15.7.7 arm64. See ../EDITOR-GATE.md for the observation record.
//
// Note: onFocus(true) returns fail here while keyboard focus works correctly.
// onFocus is advisory in VST3; do not treat its return value as a signal.

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

@interface SpikeWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation SpikeWindowDelegate
- (void)windowWillClose:(NSNotification*)note
{
	(void)note;
	[NSApp stop:nil];
	// -stop: only takes effect once another event is dequeued.
	NSEvent* dummy = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
	                                    location:NSZeroPoint
	                               modifierFlags:0
	                                   timestamp:0
	                                windowNumber:0
	                                     context:nil
	                                     subtype:0
	                                       data1:0
	                                       data2:0];
	[NSApp postEvent:dummy atStart:YES];
}
@end

namespace {

// The host side of the resize negotiation: the plug-in calls resizeView when it
// wants to change its own size; the host resizes the container and calls back.
class SpikePlugFrame : public U::Implements<U::Directly<IPlugFrame>>
{
public:
	explicit SpikePlugFrame (NSView* container) : container (container) {}

	tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* newSize) override
	{
		std::printf ("  IPlugFrame::resizeView -> %d x %d\n", newSize->getWidth (),
		             newSize->getHeight ());
		NSRect frame = [container frame];
		frame.size.width = newSize->getWidth ();
		frame.size.height = newSize->getHeight ();
		[container setFrame:frame];
		[[container window] setContentSize:frame.size];
		view->onSize (newSize);
		return kResultTrue;
	}

private:
	NSView* container;
};

} // namespace

int runEditor (const std::string& path, const std::string& classFilter)
{
	@autoreleasepool
	{
		std::printf ("== editor: %s\n", path.c_str ());
		std::string err;
		auto mod = VST3::Hosting::Module::create (path, err);
		if (!mod)
		{
			std::printf ("  LOAD FAILED: %s\n", err.c_str ());
			return 2;
		}

		VST3::Hosting::ClassInfo chosen;
		bool found = false;
		for (auto& ci : mod->getFactory ().classInfos ())
		{
			if (ci.category () != kVstAudioEffectClass)
				continue;
			if (classFilter.empty () || ci.name ().find (classFilter) != std::string::npos)
			{
				chosen = ci;
				found = true;
				break;
			}
		}
		if (!found)
		{
			std::printf ("  no class matching \"%s\"\n", classFilter.c_str ());
			return 3;
		}
		std::printf ("  selected: %s\n", chosen.name ().c_str ());

		static HostApplication hostApp;
		PluginContextFactory::instance ().setPluginContext (&hostApp);

		auto provider = owned (new PlugProvider (mod->getFactory (), chosen, true));
		if (!provider->initialize ())
		{
			std::printf ("  PlugProvider::initialize FAILED\n");
			return 4;
		}
		auto controller = provider->getControllerPtr ();
		if (!controller)
		{
			std::printf ("  no IEditController\n");
			return 5;
		}

		IPtr<IPlugView> view = owned (controller->createView (ViewType::kEditor));
		if (!view)
		{
			std::printf ("  createView(kEditor) returned null: this plug-in has no custom "
			             "editor. GraphScore falls back to its generic parameter view.\n");
			return 6;
		}
		tresult platformOk = view->isPlatformTypeSupported (kPlatformTypeNSView);
		std::printf ("  isPlatformTypeSupported(NSView) -> %s\n",
		             platformOk == kResultTrue ? "yes" : "NO");

		ViewRect rect {};
		tresult sizeOk = view->getSize (&rect);
		std::printf ("  getSize -> %s  %d x %d\n", sizeOk == kResultOk ? "ok" : "fail",
		             rect.getWidth (), rect.getHeight ());
		if (rect.getWidth () <= 0 || rect.getHeight () <= 0)
			rect = ViewRect (0, 0, 640, 480);

		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		NSRect frame = NSMakeRect (200, 200, rect.getWidth (), rect.getHeight ());
		NSWindow* window = [[NSWindow alloc]
		    initWithContentRect:frame
		              styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
		                         NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
		                backing:NSBackingStoreBuffered
		                  defer:NO];
		[window setTitle:@"GraphScore M0 VST3 editor spike"];
		SpikeWindowDelegate* winDelegate = [[SpikeWindowDelegate alloc] init];
		[window setDelegate:winDelegate];

		NSView* container = [[NSView alloc]
		    initWithFrame:NSMakeRect (0, 0, rect.getWidth (), rect.getHeight ())];
		[window setContentView:container];

		auto frameImpl = owned (new SpikePlugFrame (container));
		std::printf ("  setFrame -> %s\n",
		             view->setFrame (frameImpl) == kResultOk ? "ok" : "fail");

		tresult attached = view->attached ((__bridge void*)container, kPlatformTypeNSView);
		std::printf ("  attached(NSView) -> %s\n", attached == kResultOk ? "ok" : "FAIL");
		if (attached != kResultOk)
			return 7;

		// Resize negotiation: ask for something 20% larger and see what is allowed.
		ViewRect want (0, 0, (int32)(rect.getWidth () * 1.2), (int32)(rect.getHeight () * 1.2));
		tresult constraint = view->checkSizeConstraint (&want);
		std::printf ("  checkSizeConstraint(%dx%d) -> %s (adjusted to %d x %d)\n",
		             (int32)(rect.getWidth () * 1.2), (int32)(rect.getHeight () * 1.2),
		             constraint == kResultTrue ? "accepted" : "rejected/adjusted", want.getWidth (),
		             want.getHeight ());
		std::printf ("  canResize -> %s\n", view->canResize () == kResultTrue ? "yes" : "no");
		if (view->canResize () == kResultTrue)
		{
			[container setFrameSize:NSMakeSize (want.getWidth (), want.getHeight ())];
			[window setContentSize:NSMakeSize (want.getWidth (), want.getHeight ())];
			std::printf ("  onSize(%dx%d) -> %s\n", want.getWidth (), want.getHeight (),
			             view->onSize (&want) == kResultOk ? "ok" : "fail");
		}

		if (auto scale = U::cast<IPlugViewContentScaleSupport> (view))
			std::printf ("  setContentScaleFactor(%.1f) -> %s\n",
			             (double)[window backingScaleFactor],
			             scale->setContentScaleFactor ((float)[window backingScaleFactor]) ==
			                     kResultTrue
			                 ? "ok"
			                 : "unsupported");

		[window makeKeyAndOrderFront:nil];
		[NSApp activateIgnoringOtherApps:YES];
		std::printf ("  onFocus(true) -> %s\n", view->onFocus (true) == kResultOk ? "ok" : "fail");
		std::printf ("  window is open. Close it to finish.\n");
		std::fflush (stdout);

		[NSApp run];

		view->onFocus (false);
		view->removed ();
		view->setFrame (nullptr);
		view = nullptr;
		provider = nullptr;
		std::printf ("  editor torn down cleanly\n");
	}
	return 0;
}
