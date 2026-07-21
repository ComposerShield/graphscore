// DISPOSABLE — M0 spike, not production code
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/memoryibstream.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

extern char** environ;

int runEditor (const std::string& path, const std::string& classFilter);

namespace {

const char* tres (tresult r)
{
	switch (r)
	{
		case kResultOk: return "kResultOk";
		case kResultFalse: return "kResultFalse";
		case kInvalidArgument: return "kInvalidArgument";
		case kNotImplemented: return "kNotImplemented";
		case kInternalError: return "kInternalError";
		case kNotInitialized: return "kNotInitialized";
		case kOutOfMemory: return "kOutOfMemory";
		default: return "kUnknownResult";
	}
}

HostApplication& hostApp ()
{
	static HostApplication app;
	static bool once = [] {
		PluginContextFactory::instance ().setPluginContext (&app);
		return true;
	}();
	(void)once;
	return app;
}

VST3::Hosting::Module::Ptr loadModule (const std::string& path)
{
	std::string err;
	auto mod = VST3::Hosting::Module::create (path, err);
	if (!mod)
		std::printf ("  LOAD FAILED: %s\n", err.c_str ());
	return mod;
}

void printFactory (const VST3::Hosting::Module::Ptr& mod)
{
	auto factory = mod->getFactory ();
	auto fi = factory.info ();
	std::printf ("  factory vendor=\"%s\" url=\"%s\" flags=0x%x\n", fi.vendor ().c_str (),
	             fi.url ().c_str (), fi.flags ());
	int i = 0;
	for (auto& ci : factory.classInfos ())
	{
		std::printf ("  [%2d] %-32s cat=%-18s sub=%-22s ver=%s\n", i++, ci.name ().c_str (),
		             ci.category ().c_str (), ci.subCategoriesString ().c_str (),
		             ci.version ().c_str ());
	}
}

bool pickClass (const VST3::Hosting::Module::Ptr& mod, const std::string& filter,
                VST3::Hosting::ClassInfo& out)
{
	for (auto& ci : mod->getFactory ().classInfos ())
	{
		if (ci.category () != kVstAudioEffectClass)
			continue;
		if (filter.empty () || ci.name ().find (filter) != std::string::npos)
		{
			out = ci;
			return true;
		}
	}
	return false;
}

void activateAllBuses (IComponent* c, MediaType type, BusDirection dir)
{
	int32 n = c->getBusCount (type, dir);
	for (int32 i = 0; i < n; ++i)
	{
		BusInfo bi {};
		if (c->getBusInfo (type, dir, i, bi) != kResultOk)
			continue;
		std::string name = Steinberg::Vst::StringConvert::convert (bi.name);
		std::printf ("    bus %s/%s [%d] \"%s\" ch=%d type=%s\n",
		             type == kAudio ? "audio" : "event", dir == kInput ? "in" : "out", i,
		             name.c_str (), bi.channelCount, bi.busType == kMain ? "main" : "aux");
		c->activateBus (type, dir, i, true);
	}
}

// Full lifecycle: load -> instantiate -> buses -> setupProcessing -> setActive ->
// process a block with a MIDI note-on -> latency/tail -> state save/restore.
int runHost (const std::string& path, const std::string& classFilter)
{
	std::printf ("== host: %s\n", path.c_str ());
	auto mod = loadModule (path);
	if (!mod)
		return 2;
	printFactory (mod);

	VST3::Hosting::ClassInfo ci;
	if (!pickClass (mod, classFilter, ci))
	{
		std::printf ("  no audio-module class matching \"%s\"\n", classFilter.c_str ());
		return 3;
	}
	std::printf ("  selected: %s (%s)\n", ci.name ().c_str (), ci.subCategoriesString ().c_str ());

	hostApp ();
	auto provider = owned (new PlugProvider (mod->getFactory (), ci, true));
	if (!provider->initialize ())
	{
		std::printf ("  PlugProvider::initialize FAILED\n");
		return 4;
	}
	auto component = provider->getComponentPtr ();
	auto controller = provider->getControllerPtr ();
	std::printf ("  component=%p controller=%p\n", (void*)component.get (),
	             (void*)controller.get ());
	if (controller)
		std::printf ("  parameterCount=%d\n", controller->getParameterCount ());

	FUnknownPtr<IAudioProcessor> processor (component);
	if (!processor)
	{
		std::printf ("  no IAudioProcessor\n");
		return 5;
	}

	activateAllBuses (component, kAudio, kInput);
	activateAllBuses (component, kAudio, kOutput);
	activateAllBuses (component, kEvent, kInput);
	activateAllBuses (component, kEvent, kOutput);

	// Ask for stereo everywhere; report what the plug-in accepted.
	std::vector<SpeakerArrangement> ins (component->getBusCount (kAudio, kInput),
	                                     SpeakerArr::kStereo);
	std::vector<SpeakerArrangement> outs (component->getBusCount (kAudio, kOutput),
	                                      SpeakerArr::kStereo);
	tresult arr = processor->setBusArrangements (ins.empty () ? nullptr : ins.data (),
	                                             (int32)ins.size (),
	                                             outs.empty () ? nullptr : outs.data (),
	                                             (int32)outs.size ());
	std::printf ("  setBusArrangements(stereo) -> %s\n", tres (arr));

	std::printf ("  canProcessSampleSize(32)=%s (64)=%s\n",
	             tres (processor->canProcessSampleSize (kSample32)),
	             tres (processor->canProcessSampleSize (kSample64)));

	constexpr int32 kBlock = 512;
	constexpr double kRate = 48000.0;
	ProcessSetup setup {kRealtime, kSample32, kBlock, kRate};
	std::printf ("  setupProcessing(48k/512/f32) -> %s\n", tres (processor->setupProcessing (setup)));
	std::printf ("  setActive(true) -> %s\n", tres (component->setActive (true)));
	std::printf ("  setProcessing(true) -> %s\n", tres (processor->setProcessing (true)));

	HostProcessData data;
	data.prepare (*component, kBlock, kSample32);
	data.numSamples = kBlock;
	data.symbolicSampleSize = kSample32;
	data.processMode = kRealtime;

	ProcessContext ctx {};
	ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid;
	ctx.sampleRate = kRate;
	ctx.tempo = 120.0;
	data.processContext = &ctx;

	EventList events (16);
	Event noteOn {};
	noteOn.busIndex = 0;
	noteOn.sampleOffset = 0;
	noteOn.type = Event::kNoteOnEvent;
	noteOn.noteOn.channel = 0;
	noteOn.noteOn.pitch = 60;
	noteOn.noteOn.velocity = 0.8f;
	noteOn.noteOn.noteId = 1;
	events.addEvent (noteOn);
	data.inputEvents = &events;

	ParameterChanges inParams, outParams;
	data.inputParameterChanges = &inParams;
	data.outputParameterChanges = &outParams;

	// Fill the input with a 440 Hz tone so effects have something to chew on.
	for (int32 b = 0; b < data.numInputs; ++b)
		for (int32 c = 0; c < data.inputs[b].numChannels; ++c)
			for (int32 s = 0; s < kBlock; ++s)
				data.inputs[b].channelBuffers32[c][s] =
				    0.25f * std::sin (2.0f * 3.14159265f * 440.0f * (float)s / (float)kRate);

	// 128 blocks == ~1.37 s at 48 kHz, long enough to clear a 1 s default delay.
	constexpr int kBlocks = 128;
	float peak = 0.f;
	for (int block = 0; block < kBlocks; ++block)
	{
		tresult pr = processor->process (data);
		if (block == 0)
			std::printf ("  process() block 0 -> %s\n", tres (pr));
		data.inputEvents = nullptr; // note-on only on the first block
		for (int32 b = 0; b < data.numOutputs; ++b)
			for (int32 c = 0; c < data.outputs[b].numChannels; ++c)
				for (int32 s = 0; s < kBlock; ++s)
					peak = std::max (peak, std::fabs (data.outputs[b].channelBuffers32[c][s]));
	}
	std::printf ("  output peak over %d blocks = %.6f (%s)\n", kBlocks, peak,
	             peak > 0.f ? "non-silent" : "SILENT");

	std::printf ("  getLatencySamples=%u getTailSamples=%u\n", processor->getLatencySamples (),
	             processor->getTailSamples ());

	// --- state save / restore ------------------------------------------------
	ResizableMemoryIBStream compState (4096), ctrlState (4096);
	std::printf ("  component->getState -> %s (%zu bytes)\n", tres (component->getState (&compState)),
	             compState.getCursor ());
	if (controller)
		std::printf ("  controller->getState -> %s (%zu bytes)\n",
		             tres (controller->getState (&ctrlState)), ctrlState.getCursor ());

	std::vector<uint8> saved ((const uint8*)compState.getData (),
	                          (const uint8*)compState.getData () + compState.getCursor ());

	// Nudge a parameter so the restore has something to undo.
	if (controller && controller->getParameterCount () > 0)
	{
		ParameterInfo pi {};
		controller->getParameterInfo (0, pi);
		double before = controller->getParamNormalized (pi.id);
		double target = before > 0.5 ? 0.1 : 0.9;
		controller->setParamNormalized (pi.id, target);
		std::printf ("  param[0] id=%u %.3f -> %.3f\n", pi.id, before,
		             controller->getParamNormalized (pi.id));
	}

	compState.rewind ();
	std::printf ("  component->setState(saved) -> %s\n", tres (component->setState (&compState)));
	compState.rewind ();
	if (controller)
	{
		std::printf ("  controller->setComponentState(saved) -> %s\n",
		             tres (controller->setComponentState (&compState)));
		ctrlState.rewind ();
		std::printf ("  controller->setState(saved) -> %s\n",
		             tres (controller->setState (&ctrlState)));
	}

	ResizableMemoryIBStream roundTrip (4096);
	component->getState (&roundTrip);
	std::vector<uint8> again ((const uint8*)roundTrip.getData (),
	                          (const uint8*)roundTrip.getData () + roundTrip.getCursor ());
	std::printf ("  state round-trip byte-identical: %s (%zu vs %zu bytes)\n",
	             saved == again ? "YES" : "NO", saved.size (), again.size ());

	processor->setProcessing (false);
	component->setActive (false);
	data.unprepare ();
	provider = nullptr;
	std::printf ("  torn down cleanly\n");
	return 0;
}

// --- out-of-process scanning ---------------------------------------------------

int runScanChild (const std::string& path, const std::string& misbehave)
{
	if (misbehave == "crash")
	{
		std::printf ("  child: deliberately crashing\n");
		std::fflush (stdout);
		std::raise (SIGSEGV);
	}
	if (misbehave == "hang")
	{
		std::printf ("  child: deliberately hanging\n");
		std::fflush (stdout);
		for (;;)
			std::this_thread::sleep_for (std::chrono::seconds (60));
	}
	auto mod = loadModule (path);
	if (!mod)
		return 2;
	printFactory (mod);
	return 0;
}

struct ScanOutcome
{
	int exitCode = -1;
	int signalNo = 0;
	bool timedOut = false;
	double seconds = 0.0;
};

ScanOutcome scanInChild (const char* self, const std::string& path, const std::string& misbehave,
                         double timeoutSec)
{
	ScanOutcome out;
	std::vector<std::string> argStore {self, "--scan-one", path};
	if (!misbehave.empty ())
	{
		argStore.emplace_back ("--misbehave");
		argStore.emplace_back (misbehave);
	}
	std::vector<char*> argv;
	for (auto& a : argStore)
		argv.push_back (const_cast<char*> (a.c_str ()));
	argv.push_back (nullptr);

	pid_t pid = 0;
	auto start = std::chrono::steady_clock::now ();
	if (posix_spawn (&pid, self, nullptr, nullptr, argv.data (), environ) != 0)
	{
		std::perror ("posix_spawn");
		return out;
	}

	int status = 0;
	for (;;)
	{
		pid_t r = waitpid (pid, &status, WNOHANG);
		auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now () - start);
		out.seconds = elapsed.count ();
		if (r == pid)
			break;
		if (elapsed.count () > timeoutSec)
		{
			out.timedOut = true;
			kill (pid, SIGKILL);
			waitpid (pid, &status, 0);
			break;
		}
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	if (WIFEXITED (status))
		out.exitCode = WEXITSTATUS (status);
	if (WIFSIGNALED (status))
		out.signalNo = WTERMSIG (status);
	return out;
}

int runScan (const char* self, const std::vector<std::string>& paths, double timeoutSec)
{
	for (auto& spec : paths)
	{
		std::string path = spec, misbehave;
		auto at = spec.find ('@');
		if (at != std::string::npos)
		{
			path = spec.substr (0, at);
			misbehave = spec.substr (at + 1);
		}
		std::printf ("== scan (child process): %s%s%s\n", path.c_str (),
		             misbehave.empty () ? "" : "  misbehave=", misbehave.c_str ());
		std::fflush (stdout);
		auto o = scanInChild (self, path, misbehave, timeoutSec);
		if (o.timedOut)
			std::printf ("  DIAGNOSTIC: scan TIMED OUT after %.2fs (limit %.2fs); child killed "
			             "with SIGKILL. Parent survives.\n",
			             o.seconds, timeoutSec);
		else if (o.signalNo)
			std::printf ("  DIAGNOSTIC: scan CRASHED with signal %d (%s) after %.2fs. Parent "
			             "survives.\n",
			             o.signalNo, strsignal (o.signalNo), o.seconds);
		else
			std::printf ("  scan OK, child exit=%d in %.2fs\n", o.exitCode, o.seconds);
		std::fflush (stdout);
	}
	std::printf ("== scanner parent still alive, exiting normally\n");
	return 0;
}

void usage (const char* self)
{
	std::printf (
	    "usage:\n"
	    "  %s --host <bundle.vst3> [--class <name-substring>]\n"
	    "  %s --scan <bundle.vst3>[@crash|@hang] ... [--timeout <sec>]\n"
	    "  %s --scan-one <bundle.vst3> [--misbehave crash|hang]   (child; used by --scan)\n"
	    "  %s --editor <bundle.vst3> [--class <name-substring>]   (opens a window)\n",
	    self, self, self, self);
}

} // namespace

int main (int argc, char* argv[])
{
	std::string mode, classFilter, misbehave;
	std::vector<std::string> paths;
	double timeoutSec = 5.0;

	for (int i = 1; i < argc; ++i)
	{
		std::string a = argv[i];
		auto next = [&] { return (i + 1 < argc) ? std::string (argv[++i]) : std::string (); };
		if (a == "--host" || a == "--scan-one" || a == "--editor")
		{
			mode = a.substr (2);
			paths.push_back (next ());
		}
		else if (a == "--scan")
		{
			mode = "scan";
			while (i + 1 < argc && argv[i + 1][0] != '-')
				paths.emplace_back (argv[++i]);
		}
		else if (a == "--class")
			classFilter = next ();
		else if (a == "--misbehave")
			misbehave = next ();
		else if (a == "--timeout")
			timeoutSec = std::atof (next ().c_str ());
		else
		{
			usage (argv[0]);
			return 1;
		}
	}

	if (mode == "host")
		return runHost (paths.at (0), classFilter);
	if (mode == "scan-one")
		return runScanChild (paths.at (0), misbehave);
	if (mode == "scan")
		return runScan (argv[0], paths, timeoutSec);
	if (mode == "editor")
		return runEditor (paths.at (0), classFilter);

	usage (argv[0]);
	return 1;
}
