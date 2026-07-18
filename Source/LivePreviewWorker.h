#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "ParameterAutomation.h"
#include "RawImage.h"
#include "SampleFormat.h"
#include "WaveformPeaks.h"

// Runs a plugin's processBlock() pass off the message thread, so dragging the
// plugin's own knob never blocks on it -- see PROJECT.md's live-preview
// performance note for why this exists (the previous synchronous-on-the-
// message-thread version made every drag tick a full blocking recompute).
//
// A dedicated background juce::Thread with a one-slot "latest request wins"
// mailbox: submit() always overwrites whatever hadn't started yet rather than
// queuing, since only the most recent parameter/selection state is ever worth
// computing. A request already being processed is left to finish -- the
// plugin instance isn't safe to interrupt mid-processBlock() -- but nothing
// ever piles up behind it, so the worker is always at most one pass behind
// the latest tick.
//
// JUCE's own header comments (juce_AudioProcessor.h) describe processBlock()/
// reset() as callbacks the *audio thread* makes, explicitly distinct from the
// message thread, and recommend AsyncUpdater-style hand-off to reach the UI
// from inside them -- so running them here is more aligned with JUCE's own
// expected usage than the old message-thread-synchronous arrangement was.
//
// Decoupling the compute alone turned out to be only half the fix: applying a
// result used to still re-render the image on the message thread (RawImage::
// toJuceImageFromBytes()/toJuceImageFromBytesScoped() is real per-pixel work),
// and with compute no longer gating how often a fresh result becomes
// available, that render could end up running back-to-back with no idle time
// -- the exact same message-thread saturation as before, just moved from
// "compute" to "render." Two fixes address this, both still in place: (1)
// delivery itself is throttled to a fixed cadence (deliveryRateHz below) via
// a private juce::Timer, independent of how fast the worker can actually
// compute, so the message thread only ever pays render cost at a rate the
// display can actually show; (2) the render itself (image composition *and*
// the waveform's float conversion) now happens here, on this same worker
// thread, right after compute -- see processRequest() in the .cpp and
// Result::renderedImage/waveformSamples below -- so applyLivePreviewResult()
// on the message thread is just a cheap hand-off of already-finished data.
// This is safe because RawImage's render methods used here are all `const`
// and either allocate fresh state or -- for the one real shared mutable
// cache, RawImage::cachedPlainImage -- are only ever read from this thread
// while a session is open (see Request::image below and PROJECT.md's
// live-preview performance note for the call-site audit backing that claim).
class LivePreviewWorker : private juce::Thread,
                           private juce::Timer
{
public:
    struct Request
    {
        // Valid only because every call site that could invalidate this
        // (plugin swap, app shutdown) calls waitUntilIdle()/stopThread()
        // first -- see MainComponent::loadAndOpenPlugin()/~MainComponent().
        juce::AudioPluginInstance* plugin = nullptr;

        // The whole visual-order buffer, or one channel plane -- shared (not
        // copied per request) across an entire live-preview session, since
        // workingImage->pixelBytes is provably immutable for as long as the
        // plugin panel stays open (Load Image/Reset/Undo/Redo are all
        // disabled while it's open -- see updatePluginListEnablement()).
        // See MainComponent::getOrBuildLivePreviewSource().
        std::shared_ptr<const juce::MemoryBlock> source;

        // Non-owning; used only for its render methods (previewWithChannelBytes()/
        // previewWithVisualOrderedBytes()/toJuceImageFromBytes()/
        // toJuceImageFromBytesScoped()/computeHighlightOverlay()/
        // computeChannelHighlightOverlay(), all const). Valid only because the
        // same session invariant Request::plugin above already relies on holds
        // here too: workingImage is provably not reassigned and its pixelBytes
        // provably not mutated while the panel is open (Load/Reset/Undo/Redo,
        // Export, and toggling split mode are all disabled then -- see
        // MainComponent::updatePluginListEnablement() and MainMenuModel), and
        // MainComponent::endLivePreviewSession() calls waitUntilIdle() before
        // any of that can become possible again. RawImage's own render cache
        // (cachedPlainImage/plainImageDirty) is therefore only ever touched
        // from this worker thread for the lifetime of a session -- never
        // concurrently from the message thread.
        const RawImage* image = nullptr;

        std::optional<RawImage::Channel> channel;
        juce::Range<int> selection;
        SampleFormat::Mode sampleMode = SampleFormat::Mode::bipolar;
        std::vector<ParameterAutomation> ramps;

        // Fixed constants mirrored from MainComponent (see PROJECT.md: not
        // derived from anything real, chosen to match Audacity's raw-import
        // defaults) -- passed through rather than reached-for, so a Request is
        // a fully self-contained snapshot with no back-reference to MainComponent.
        double sampleRate = 44100.0;
        int blockSize = 4096;

        // Echoed back on Result. Bumped by MainComponent whenever a live-preview
        // session ends (Apply, Cancel, or a different plugin panel opens), so a
        // result arriving after its session ended is recognizably stale and can
        // be dropped instead of misapplied.
        uint64_t epoch = 0;
    };

    struct Result
    {
        juce::MemoryBlock processedBytes; // same shape/coordinate-space as Request::source
        std::optional<RawImage::Channel> channel;
        juce::Range<int> selection;
        uint64_t epoch = 0;

        // Fully-composed preview image, ready for imagePreview.setImage() --
        // rendered here (see processRequest() in the .cpp) from processedBytes
        // via the same RawImage methods applyLivePreviewResult() used to call
        // itself: previewWithChannelBytes()/previewWithVisualOrderedBytes() to
        // splice processedBytes into a full render-ready buffer, then
        // toJuceImageFromBytes() (no selection) or toJuceImageFromBytesScoped()
        // (selection, reusing the highlight overlay's row range) to render it.
        // processedBytes itself is kept too -- the commit path
        // (applyChannelBytes()/applyVisualOrderedBytes()) needs the raw bytes,
        // not an Image.
        juce::Image renderedImage;

        // Post-plugin float buffer for the waveform lane -- exactly the
        // selected sub-range when `selection` is non-empty, else the whole
        // buffer (same coordinate space as `selection`/`channel` above).
        // Computed once here on the worker thread from processedBytes, so
        // the message thread doesn't have to pay a second bytesToBuffer()
        // pass over the same bytes just to refresh the waveform view.
        juce::AudioBuffer<float> waveformSamples;

        // Bucket peaks over waveformSamples (absolute alignment from
        // `selection`'s start; every bucket fully contained in the changed
        // range -- see WaveformPeaks::computePartial()), also computed here on
        // the worker thread. Without this, WaveformView would rebuild peaks
        // for the whole delivered range on the message thread per delivery --
        // for a no-selection pass over a 77.9M-sample buffer that's an
        // O(numSamples) pass recurring every worker cycle, which is exactly
        // the shape of cost this class exists to keep off the message thread.
        WaveformPeaks::Partial waveformPeaks;
    };

    LivePreviewWorker();
    ~LivePreviewWorker() override;

    // Message-thread only, called explicitly before currentPlugin is torn
    // down (see MainComponent::~MainComponent()) -- juce::Thread::stopThread()
    // itself is private here (juce::Thread is a private base), so this is the
    // public entry point. Lets any in-flight pass finish naturally first
    // (run()'s loop only checks threadShouldExit() between jobs) before
    // force-killing as a last resort; see juce::Thread::stopThread()'s own
    // documentation for that fallback's caveats.
    void shutdown(int timeOutMilliseconds) { stopThread(timeOutMilliseconds); }

    // Message-thread only. Overwrites whatever request hadn't started yet;
    // never queues more than one.
    void submit(Request request);

    // Message-thread only. Clears the not-yet-started request, if any --
    // does NOT wait for one already in flight. Safe to call without waiting
    // because Cancel (the only caller) never touches the plugin instance
    // itself, so a still-in-flight pass finishing in the background is
    // harmless; its result is simply stale (see Request::epoch) by the time
    // it's delivered.
    void discardPending();

    // Any thread (juce::CriticalSection's enter/exit are const): true while a
    // pass is in flight or queued -- i.e. the current preview shown on screen
    // does not yet reflect the latest submitted state. Polled by the status
    // bar's BusySpinner at animation rate.
    bool isBusy() const
    {
        const juce::ScopedLock sl(mutex);
        return jobInFlight || pendingRequest.has_value();
    }

    // Message-thread only. Blocks until nothing is in flight or pending,
    // delivering (via onResultReady, same callback as the async path) every
    // result produced along the way -- so the live-preview state is
    // guaranteed fresh by the time this returns. Used before Apply commits,
    // before a plugin instance is torn down/swapped, and unconditionally at
    // the end of every live-preview session (see endLivePreviewSession()) so
    // no in-flight render can still be reading `Request::image` once
    // workingImage becomes mutable again.
    void waitUntilIdle();

    // Invoked on the message thread -- either from the throttled timer tick
    // (the normal async path, see deliveryRateHz) or directly from
    // waitUntilIdle() (which already runs on the message thread, and wants
    // every intermediate result delivered as it drains, not just the next
    // timer tick's worth).
    std::function<void(Result)> onResultReady;

private:
    void run() override;
    void timerCallback() override;
    void deliverPendingResult();

    // How often a fresh result is actually pulled and rendered -- see the
    // class comment above for why this exists. 60Hz is comfortably beyond
    // what a human dragging a knob can perceive as anything but instant, and
    // caps the message thread's render work to something it can always keep
    // up with regardless of how fast the worker itself computes.
    static constexpr int deliveryRateHz = 60;

    juce::CriticalSection mutex;
    std::optional<Request> pendingRequest;
    std::optional<Result> lastResult;
    bool jobInFlight = false;

    // Signalled by the worker whenever a job finishes (whether or not another
    // was already queued behind it) -- waitUntilIdle() blocks on this rather
    // than polling.
    juce::WaitableEvent stateChanged;
};
