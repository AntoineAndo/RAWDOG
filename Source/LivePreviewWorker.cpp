#include "LivePreviewWorker.h"
#include "PluginHost.h"
#include "PluginParameterWatcher.h"

namespace
{
    // Runs one plain plugin slot over a single float buffer -- skips a bypassed/empty
    // slot, resets the plugin's DSP state before its own pass (so repeated live-preview
    // passes against the same source bytes stay deterministic for stateful plugins), and
    // builds a beforeBlock closure from the slot's own ramps -- see
    // PluginHost::processWholeBuffer's beforeBlock hook and ChainSlot::ramps.
    //
    // ScopedSelfWriteSuppression: a ramp's setValueNotifyingHost() runs on this
    // background thread, so a thread_local suppression flag is what lets
    // PluginParameterWatcher tell "my own automated write" apart from a real concurrent
    // knob-drag on the message thread. This remains correct regardless of which slot (if
    // any) currently has a listener attached on the message thread -- the flag is
    // thread-local, not per-AudioProcessor, so it suppresses every ramp write here
    // equally.
    void runSlot(const LivePreviewWorker::ChainSlotRequest& slot, juce::AudioBuffer<float>& buffer,
                 double totalScopeMs, double sampleRate, int blockSize)
    {
        if (slot.bypassed || slot.plugin == nullptr)
            return;

        auto& plugin = *slot.plugin;
        plugin.reset();

        const auto& ramps = slot.ramps;
        auto beforeBlock = [&plugin, &ramps, totalScopeMs, sampleRate](int blockStartSample)
        {
            if (ramps.empty())
                return;

            const double timeMs = (blockStartSample / sampleRate) * 1000.0;

            PluginParameterWatcher::ScopedSelfWriteSuppression suppress;
            for (auto& ramp : ramps)
                if (auto* param = plugin.getParameters()[ramp.parameterIndex]) // bounds-checked, nullptr if stale
                    param->setValueNotifyingHost(ramp.evaluateAt(timeMs, totalScopeMs));
        };

        PluginHost::processWholeBuffer(plugin, buffer, blockSize, beforeBlock);
    }

    // A flat chain of plain plugin slots, in order -- used both for the top-level
    // chain's plain-slot entries and for a ConditionalChainSlotRequest's own two
    // branches. Branches can only ever hold this (std::vector<ChainSlotRequest>, not
    // ChainEntryRequest) -- nesting a conditional slot inside a branch is impossible at
    // the type level, not just disallowed by convention.
    void runFlatChain(const std::vector<LivePreviewWorker::ChainSlotRequest>& chain,
                       juce::AudioBuffer<float>& buffer, double totalScopeMs, double sampleRate, int blockSize)
    {
        for (const auto& slot : chain)
            runSlot(slot, buffer, totalScopeMs, sampleRate, blockSize);
    }

    // Evaluates a ConditionalChainSlotRequest's condition per-sample -- always against
    // the image's real, pre-chain pixel data (see PixelCondition.h's
    // computePixelBrightness()), never against `buffer` itself -- and composites its two
    // branches' outputs according to `cond.mode`:
    //
    // Masked: both branches process a full copy of `buffer` independently (so each
    // branch's own DSP -- delay, reverb, whatever -- sees real, continuous, unmodified
    // neighbor data, exactly as if there were no condition at all), then the result is
    // assembled by picking each sample from whichever branch matches. Correct, seamless
    // per-branch DSP; a hard boundary between regions (nothing bleeds across it).
    //
    // Compacted: only the samples that actually match a branch are packed into their own
    // contiguous buffer, processed, then scattered back to their original positions. Real
    // bleed happens, but onto whichever sample is adjacent in the *compacted* buffer, not
    // the true spatial neighbor -- and each branch's own ramps scale to its packed
    // buffer's own length, not the outer scope, since that's the only length meaningful
    // to a branch that never sees the unpacked positions.
    void runConditionalSlot(const LivePreviewWorker::ConditionalChainSlotRequest& cond,
                             juce::AudioBuffer<float>& buffer, double sampleRate, int blockSize,
                             const RawImage& image, std::optional<RawImage::Channel> channel, int scopeStartByte)
    {
        if (cond.bypassed)
            return;

        const int numSamples = buffer.getNumSamples();
        const int imageChannelCount = image.getChannelCount();

        std::vector<bool> matches((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const int byteIndex = scopeStartByte + i;
            const int pixelIndex = channel.has_value() ? byteIndex : byteIndex / imageChannelCount;
            matches[(size_t) i] = evaluatePixelCondition(cond.condition, image, pixelIndex);
        }

        if (cond.mode == CompositingMode::masked)
        {
            juce::AudioBuffer<float> bufferA(buffer), bufferB(buffer); // two full independent copies
            const double totalScopeMs = (numSamples / sampleRate) * 1000.0;
            runFlatChain(cond.branchA, bufferA, totalScopeMs, sampleRate, blockSize);
            runFlatChain(cond.branchB, bufferB, totalScopeMs, sampleRate, blockSize);

            auto* out = buffer.getWritePointer(0);
            const auto* inA = bufferA.getReadPointer(0);
            const auto* inB = bufferB.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i)
                out[i] = matches[(size_t) i] ? inA[i] : inB[i];
        }
        else // Compacted
        {
            int countA = 0;
            for (bool m : matches)
                if (m)
                    ++countA;
            const int countB = numSamples - countA;

            juce::AudioBuffer<float> packedA(1, countA), packedB(1, countB);

            {
                const auto* srcAll = buffer.getReadPointer(0);
                auto* outA = packedA.getWritePointer(0);
                auto* outB = packedB.getWritePointer(0);
                int ai = 0, bi = 0;
                for (int i = 0; i < numSamples; ++i)
                {
                    if (matches[(size_t) i])
                        outA[ai++] = srcAll[i];
                    else
                        outB[bi++] = srcAll[i];
                }
            }

            runFlatChain(cond.branchA, packedA, (countA / sampleRate) * 1000.0, sampleRate, blockSize);
            runFlatChain(cond.branchB, packedB, (countB / sampleRate) * 1000.0, sampleRate, blockSize);

            auto* out = buffer.getWritePointer(0);
            const auto* inA = packedA.getReadPointer(0);
            const auto* inB = packedB.getReadPointer(0);
            int ai = 0, bi = 0;
            for (int i = 0; i < numSamples; ++i)
                out[i] = matches[(size_t) i] ? inA[ai++] : inB[bi++];
        }
    }

    // Runs the whole top-level effect chain, in order, over a single float buffer --
    // dispatches each entry to a plain plugin pass (runSlot) or, for a conditional
    // entry, to runConditionalSlot(). `image`/`channel`/`scopeStartByte` are only ever
    // read when an entry actually is a ConditionalChainSlotRequest -- scopeStartByte is
    // the byte position (in channel-plane space if channel-scoped, else in
    // getVisualOrderedPixelBytes() space) that buffer sample 0 corresponds to.
    void runChain(const std::vector<LivePreviewWorker::ChainEntryRequest>& chain,
                  juce::AudioBuffer<float>& buffer, double totalScopeMs, double sampleRate, int blockSize,
                  const RawImage& image, std::optional<RawImage::Channel> channel, int scopeStartByte)
    {
        for (const auto& entry : chain)
        {
            if (const auto* slot = std::get_if<LivePreviewWorker::ChainSlotRequest>(&entry))
                runSlot(*slot, buffer, totalScopeMs, sampleRate, blockSize);
            else
                runConditionalSlot(std::get<LivePreviewWorker::ConditionalChainSlotRequest>(entry), buffer,
                                    sampleRate, blockSize, image, channel, scopeStartByte);
        }
    }

    // Runs the whole effect chain over the source bytes for one live-preview
    // pass. Runs entirely on LivePreviewWorker's background thread: touches
    // only request.chain's plugin pointers and plain juce::MemoryBlock/
    // AudioBuffer values, never workingImage/RawImage -- RawImage's lazy
    // render/plane caches are not thread-safe, so nothing here may touch them.
    juce::MemoryBlock processRequest(const LivePreviewWorker::Request& request)
    {
        const juce::MemoryBlock& source = *request.source;
        const auto& selection = request.selection;

        // A ramp segment's start/endFraction are relative to the scope actually
        // being processed (the selection sub-range, or the whole buffer when
        // there's no selection) -- computed once here so they scale with whatever
        // the user has selected, rather than a fixed absolute duration. Shared by
        // every slot's ramps in the chain.
        const int totalScopeSamples = selection.isEmpty() ? (int) source.getSize() : selection.getLength();
        const double totalScopeMs = (totalScopeSamples / request.sampleRate) * 1000.0;

        // Bytes outside the selection are provably untouched (see PROJECT.md's "Apply
        // scoping"), so start from a plain byte copy of the source and only pay the
        // float round-trip for the selected sub-range - on a large image with a small
        // selection, converting the whole buffer on every live-preview tick would be
        // pure waste. The float round-trip happens exactly once total regardless of chain
        // length -- every enabled slot processes the same in-memory buffer in place,
        // one after another, with no intermediate byte conversion between slots.
        if (! selection.isEmpty())
        {
            const int start = selection.getStart();
            const int length = selection.getLength();

            juce::MemoryBlock selectionBytes(static_cast<const char*>(source.getData()) + start, (size_t) length);

            auto selectedBuffer = SampleFormat::bytesToBuffer(selectionBytes, request.sampleMode);
            runChain(request.chain, selectedBuffer, totalScopeMs, request.sampleRate, request.blockSize,
                     *request.image, request.channel, start);
            SampleFormat::bufferToBytes(selectedBuffer, selectionBytes, request.sampleMode);

            juce::MemoryBlock result(source);
            result.copyFrom(selectionBytes.getData(), start, (size_t) length);
            return result;
        }

        auto buffer = SampleFormat::bytesToBuffer(source, request.sampleMode);
        runChain(request.chain, buffer, totalScopeMs, request.sampleRate, request.blockSize,
                 *request.image, request.channel, 0);

        juce::MemoryBlock result;
        result.setSize(source.getSize());
        SampleFormat::bufferToBytes(buffer, result, request.sampleMode);
        return result;
    }

    // Renders the image + waveform float buffer for a just-computed result.
    // Runs entirely on LivePreviewWorker's background thread: safe because
    // every RawImage method called here is `const`, and the one
    // shared mutable cache any of them touch (cachedPlainImage/plainImageDirty,
    // inside toJuceImageFromBytes()/toJuceImageFromBytesScoped()) is, for the
    // whole lifetime of a live-preview session, only ever touched from this
    // thread -- see Request::image's doc comment in LivePreviewWorker.h for
    // the invariant this relies on.
    void renderResult(const LivePreviewWorker::Request& request, LivePreviewWorker::Result& result)
    {
        // juce::Image construction/pixel-writing on macOS goes through
        // CoreGraphicsPixelData, which only touches a private in-memory
        // CGBitmapContextCreate bitmap -- no window/AppKit/main-thread
        // dependency (confirmed against JUCE's own source; see PROJECT.md's
        // live-preview performance note). A raw juce::Thread has no implicit
        // autorelease pool the way the message thread's run loop provides
        // one, so this is cheap insurance for any Objective-C-adjacent call
        // in this path, matching JUCE's own internal convention (see
        // CoreGraphicsPixelData::applyFilterInArea()'s identical comment).
        JUCE_AUTORELEASEPOOL
        {
            const RawImage& image = *request.image;
            const auto& selection = request.selection;

            const juce::MemoryBlock full = request.channel.has_value()
                ? image.previewWithChannelBytes(*request.channel, result.processedBytes)
                : image.previewWithVisualOrderedBytes(result.processedBytes);

            if (selection.isEmpty())
            {
                // No selection -- the whole buffer was just processed, so
                // there's no "unchanged outside a sub-range" guarantee to
                // exploit; a full render is the only correct option here.
                result.renderedImage = image.toJuceImageFromBytes(full);
            }
            else
            {
                // Only the selected sub-range of `full` actually changed --
                // reuse the same row range the highlight overlay already
                // needs, so only those rows get re-rendered.
                const auto overlay = request.channel.has_value()
                    ? image.computeChannelHighlightOverlay(selection)
                    : image.computeHighlightOverlay(selection);
                result.renderedImage = image.toJuceImageFromBytesScoped(full, overlay->topRow, overlay->bottomRow);
            }

            // Waveform lane needs the post-plugin float samples too -- computed
            // here (worker-side) from whichever sub-range actually changed,
            // rather than making the message thread reconvert the same bytes
            // a second time.
            if (! selection.isEmpty())
            {
                juce::MemoryBlock selectionBytes(static_cast<const char*>(result.processedBytes.getData()) + selection.getStart(),
                                                  (size_t) selection.getLength());
                result.waveformSamples = SampleFormat::bytesToBuffer(selectionBytes, request.sampleMode);
            }
            else
            {
                result.waveformSamples = SampleFormat::bytesToBuffer(result.processedBytes, request.sampleMode);
            }

            // Bucket peaks for the range just converted -- strictly less work
            // than the bytesToBuffer() pass above over the same samples, and
            // paying it here is what keeps WaveformView's per-delivery peak
            // maintenance off the message thread (see Result::waveformPeaks).
            if (const int numWaveformSamples = result.waveformSamples.getNumSamples(); numWaveformSamples > 0)
            {
                const int rangeStart = selection.isEmpty() ? 0 : selection.getStart();
                result.waveformPeaks = WaveformPeaks::computePartial(result.waveformSamples.getReadPointer(0),
                                                                     rangeStart, rangeStart + numWaveformSamples);
            }
        }
    }
}

LivePreviewWorker::LivePreviewWorker() : juce::Thread("LivePreviewWorker")
{
    startThread();
    startTimerHz(deliveryRateHz);
}

LivePreviewWorker::~LivePreviewWorker()
{
    stopTimer();
    stopThread(5000);
}

void LivePreviewWorker::submit(Request request)
{
    {
        const juce::ScopedLock sl(mutex);
        pendingRequest = std::move(request);
    }

    notify(); // wake run()'s wait() if it's idle
}

void LivePreviewWorker::discardPending()
{
    const juce::ScopedLock sl(mutex);
    pendingRequest.reset();
}

void LivePreviewWorker::waitUntilIdle()
{
    for (;;)
    {
        std::optional<Result> resultToDeliver;
        bool idle = false;

        {
            const juce::ScopedLock sl(mutex);
            resultToDeliver = std::move(lastResult);
            lastResult.reset();
            idle = ! jobInFlight && ! pendingRequest.has_value();
        }

        if (resultToDeliver.has_value() && onResultReady != nullptr)
            onResultReady(std::move(*resultToDeliver));

        if (idle)
            return;

        stateChanged.wait(-1);
    }
}

void LivePreviewWorker::deliverPendingResult()
{
    std::optional<Result> result;

    {
        const juce::ScopedLock sl(mutex);
        result = std::move(lastResult);
        lastResult.reset();
    }

    if (result.has_value() && onResultReady != nullptr)
        onResultReady(std::move(*result));
}

void LivePreviewWorker::timerCallback()
{
    // Fires on the message thread at deliveryRateHz regardless of how often
    // the worker actually finishes a pass -- this is what caps render work to
    // a rate the message thread can always keep up with (see the class
    // comment). A no-op (just a lock + empty check) when nothing new has
    // completed since the last tick.
    deliverPendingResult();
}

void LivePreviewWorker::run()
{
    for (;;)
    {
        std::optional<Request> request;

        {
            const juce::ScopedLock sl(mutex);
            if (pendingRequest.has_value())
            {
                request = std::move(pendingRequest);
                pendingRequest.reset();
                jobInFlight = true;
            }
        }

        if (! request.has_value())
        {
            if (threadShouldExit())
                return;

            wait(-1); // woken by submit()'s notify(), or stopThread()'s
            continue; // loop back around to re-check pendingRequest/threadShouldExit()
        }

        Result result;
        result.processedBytes = processRequest(*request);
        result.channel = request->channel;
        result.selection = request->selection;
        result.epoch = request->epoch;

        renderResult(*request, result);

        {
            const juce::ScopedLock sl(mutex);
            jobInFlight = false;
            lastResult = std::move(result);
        }

        // Deliberately no per-job push to the message thread here -- see the
        // class comment: delivery is pulled by timerCallback() (and drained
        // directly by waitUntilIdle()) at a throttled rate instead, so the
        // worker computing faster than the display can show never turns into
        // message-thread render backlog.
        stateChanged.signal();

        if (threadShouldExit())
            return;
    }
}
