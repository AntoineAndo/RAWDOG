#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <optional>
#include "BusySpinner.h"
#include "FavouritePluginsStore.h"
#include "GrippedResizerBar.h"
#include "HeaderEditorPanel.h"
#include "LeftColumnPanel.h"
#include "LivePreviewWorker.h"
#include "MainMenuModel.h"
#include "ParameterAutomation.h"
#include "PluginEditorPanel.h"
#include "PluginListModel.h"
#include "PluginParameterWatcher.h"
#include "PluginPresetsStore.h"
#include "PluginScanner.h"
#include "RawImage.h"
#include "RightColumnPanel.h"
#include "WaveformSectionPanel.h"
#include "WaveformView.h"
#include "ZoomableImageView.h"

class MainComponent : public juce::Component,
                      private juce::ScrollBar::Listener,
                      private juce::AsyncUpdater,
                      public juce::ApplicationCommandTarget
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

    ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

    // Called by PixelBenderApplication before actually quitting (from
    // MainWindow::closeButtonPressed()'s systemRequestedQuit(), or the app
    // menu/Cmd+Q, both of which funnel through the same override) -- routes
    // through the same discard-changes confirmation as Load Image/Reset
    // rather than silently losing an unsaved edit on quit.
    void confirmQuit(std::function<void()> proceedToQuit);

private:
    enum CommandIDs
    {
        undoCommand = 1000,
        redoCommand,
        cancelEditorCommand, // Escape — cancels whichever of pluginEditorPanel/
                            // headerEditorPanel is currently open, not shown
                            // in any menu (keyboard-only, mirroring each
                            // panel's own Cancel button)
        loadImageCommand, // Cmd+O — File > Load Image... is itself this command
                          // item (added via menu.addCommandItem() in
                          // menuModel's populateFileMenuLoadImageItem callback
                          // below), same treatment as undoCommand/redoCommand,
                          // so the shortcut appears next to the menu item too.
        resetCommand // Cmd+Shift+R — same command-item treatment as
                     // loadImageCommand above, via populateFileMenuResetItem.
    };

    void refreshPluginList();
    void loadImageClicked();
    void exportImageClicked();
    void loadAndOpenPlugin(int row);
    void openEditorClicked();
    void applyClicked();
    void cancelEditorClicked();
    void savePresetClicked();
    void openHeaderEditorClicked();
    void applyHeaderEditClicked();
    void cancelHeaderEditClicked();
    void endHeaderEditSession();
    void refreshHeaderLivePreview(const RawImage::BmpEditableHeaderFields& candidate);
    void resetClicked();
    void undoClicked();
    void redoClicked();
    void pushUndoState();
    void updatePreview(bool resetView = false);
    void updateWaveform(bool resetView = false);
    void updatePluginListEnablement();
    void sampleModeChanged();
    void syncScrollBarToView();
    void setStatus(const juce::String& text);

    // Guards a destructive action (Load Image, Reset, Quit) behind the same
    // generic discard-changes confirmation whenever undoStack is non-empty
    // (i.e. the current image has edits since it was loaded/reset that would
    // otherwise be silently lost) -- runs proceed() immediately if there's
    // nothing to lose.
    void confirmDiscardChangesIfNeeded(std::function<void()> proceed);

    // Lazily builds (and caches for the rest of this live-preview session --
    // see cachedWholeBufferSource/cachedChannelSource below) an immutable
    // snapshot of the buffer LivePreviewWorker should process for the given
    // scope. Safe to hand to another thread via shared_ptr because
    // workingImage->pixelBytes is provably immutable for as long as the
    // plugin panel stays open (Load Image/Reset/Undo/Redo are disabled then
    // -- see updatePluginListEnablement()), and nothing mutates the returned
    // snapshot after construction.
    std::shared_ptr<const juce::MemoryBlock> getOrBuildLivePreviewSource(std::optional<RawImage::Channel> channel);

    void refreshLivePreview();

    // The callback LivePreviewWorker::onResultReady is wired to -- the same
    // image/waveform-update logic refreshLivePreview()'s tail always ran
    // synchronously, now applied whenever a background pass completes.
    // Discards a result whose epoch doesn't match livePreviewEpoch (the
    // session it was computed for already ended -- Apply/Cancel/plugin swap).
    void applyLivePreviewResult(LivePreviewWorker::Result result);

    void endLivePreviewSession(bool commitToWorkingImage);

    // Wired to imagePreview.onClick -- deselects whichever waveform view
    // currently owns the selection (the plain interleaved view, or the active
    // split-mode lane), same as dragging out a new selection would, just
    // without one. No-op with no image loaded.
    void clearCurrentSelection();

    // Which selection is "the" current one for Apply/undo/highlight purposes:
    // a channel-scoped lane's selection when split mode is on and a lane has
    // an active selection, or the plain interleaved waveform's selection
    // otherwise. channel == nullopt means "whole interleaved buffer" either way.
    struct SelectionScope
    {
        std::optional<RawImage::Channel> channel;
        juce::Range<int> range;
    };

    SelectionScope getCurrentSelectionScope() const;

    // Refreshes just the image preview's selection highlight (two lines, no
    // pixel data touched) against the given image/scope -- far cheaper than
    // updatePreview()/refreshLivePreview(), which rebuild the whole image.
    // Takes the image explicitly (rather than always workingImage) so it
    // works equally for the header editor's separate headerEditScratch.
    void updateHighlightOverlay(const RawImage& image, const SelectionScope& scope);

    // Restores a captured SelectionScope (from an undo/redo entry): clears
    // every lane's selection first, switches split mode on/off to match
    // whether the entry has a channel (so the restored selection is actually
    // visible), then sets just the target lane's range.
    void restoreSelectionScope(std::optional<RawImage::Channel> channel, juce::Range<int> range);

    // Enables/disables split-channel display. Entering split mode lazily
    // (re)computes the channel planes (cheap if already up to date, per
    // RawImage's own dirty-flag caching) and populates the 3 or 4 lanes that
    // apply; leaving it clears the per-channel selection-tracking state.
    // Does not touch pixelBytes/headerBytes — purely a view/selection-
    // tracking concern.
    void setSplitMode(bool enabled);

    // Repopulates the channel lanes' buffers from workingImage's current
    // per-channel planes, and shows/hides the 4th (alpha) lane to match
    // whether the loaded image actually has an alpha channel. No-op if
    // there's no image or it isn't a 3/4-channel one.
    void refreshChannelWaveforms(bool resetView);

    // Whichever waveform view currently drives the shared horizontal
    // scrollbar/zoom sync: channelWaveformViews[0] in split mode (arbitrary
    // but consistent — all 3 lanes share the same sample count), waveformView
    // otherwise.
    WaveformView& primaryWaveformView();

    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

    // Coalesces WaveformView::onSelectionChanged, which fires on every mouse-move
    // frame of a selection drag — without this, dragging a selection while a
    // plugin panel is open would re-run the plugin's (potentially multi-second)
    // processing once per mouse-move event instead of once per event-loop turn.
    // Same debounce idiom PluginParameterWatcher already uses for parameter bursts.
    void handleAsyncUpdate() override;

    static constexpr double sampleRate = 44100.0;

    // Bumped from 512 (see PROJECT.md's live-preview performance note):
    // processWholeBuffer()'s cost is dominated by per-block overhead, not
    // per-sample DSP, so a larger block cuts total block-boundary crossings
    // ~8x for the same buffer. Tradeoff: ParameterAutomation ramps evaluate
    // once per block, so this also coarsens ramp resolution ~8x -- still
    // thousands of steps across a typical selection, imperceptible for pixel
    // data.
    static constexpr int blockSize = 4096;

    PluginScanner scanner;

    juce::ScrollBar horizontalScrollBar { false };

    ZoomableImageView imagePreview;
    WaveformView waveformView;
    std::array<WaveformView, 4> channelWaveformViews; // indexed by (int) RawImage::Channel; lane 3 (alpha) is only ever shown for a loaded PNG with a real alpha channel
    juce::TextButton splitModeToggle { "Split Channels" };
    juce::Label sampleModeLabel { {}, "Sample Mode:" };
    juce::ComboBox sampleModeCombo;
    juce::ListBox pluginListBox;
    juce::Label statusLabel;

    // Sits just left of statusLabel in the status strip; shows itself while
    // livePreviewWorker has a pass in flight (wired to isBusy() in the
    // constructor) and hides itself when idle -- see BusySpinner.h.
    BusySpinner previewBusySpinner;

    FavouritePluginsStore favouritePluginsStore;
    PluginPresetsStore pluginPresetsStore;
    PluginListModel listModel { scanner.getKnownPluginList(), favouritePluginsStore, pluginPresetsStore };

    MainMenuModel menuModel { MainMenuModel::Callbacks {
        [this] { return scanner.isScanning(); },
        [this] { return pluginEditorPanel != nullptr || headerEditorPanel != nullptr || imageLoadInProgress; },
        [this] { return workingImage != nullptr; },
        [this] { exportImageClicked(); },
        [this] { refreshPluginList(); },
        [this](juce::PopupMenu& menu)
        {
            menu.addCommandItem(&commandManager, undoCommand);
            menu.addCommandItem(&commandManager, redoCommand);
        },
        [this] { return workingImage != nullptr && workingImage->getFormat() == RawImage::Format::bmp; },
        [this] { openHeaderEditorClicked(); },
        [this](juce::PopupMenu& menu) { menu.addCommandItem(&commandManager, loadImageCommand); },
        [this](juce::PopupMenu& menu) { menu.addCommandItem(&commandManager, resetCommand); }
    } };

    // Parents the plugin list and (optionally) the currently-open PluginEditorPanel;
    // parents the image preview, waveform section, and status label respectively.
    // Constructed after the raw controls above (declaration order == construction
    // order) so the references/pointers they parent are already valid.
    LeftColumnPanel leftColumn { pluginListBox };
    RightColumnPanel rightColumn { imagePreview, statusLabel, waveformView, horizontalScrollBar,
                                    channelWaveformViews[0], channelWaveformViews[1], channelWaveformViews[2],
                                    channelWaveformViews[3],
                                    splitModeToggle, sampleModeLabel, sampleModeCombo, previewBusySpinner };

    juce::StretchableLayoutManager outerLayout;
    GrippedResizerBar outerResizerBar { &outerLayout, 1, true /*vertical bar, dragged left/right*/ };

    // Declared before currentPlugin so it destructs (and detaches) first —
    // member destruction order is the reverse of declaration order.
    PluginParameterWatcher pluginParamWatcher;

    std::unique_ptr<RawImage> originalImage;
    std::unique_ptr<RawImage> workingImage;
    std::unique_ptr<juce::AudioPluginInstance> currentPlugin;

    // Declared after currentPlugin as a matter of style (matching
    // pluginParamWatcher's ordering rationale above), but the real safety
    // guarantee is the explicit livePreviewWorker.stopThread() call in
    // ~MainComponent(), *before* currentPlugin->releaseResources() -- not
    // implicit destruction order alone. See LivePreviewWorker's own comments
    // for why processing runs here instead of on the message thread.
    LivePreviewWorker livePreviewWorker;

    std::unique_ptr<PluginEditorPanel> pluginEditorPanel;

    // While headerEditorPanel is open, headerEditScratch is a scratch copy of
    // workingImage that every field edit is applied to for live preview —
    // workingImage itself is only touched by applyHeaderEditClicked().
    std::unique_ptr<RawImage> headerEditScratch;
    std::unique_ptr<HeaderEditorPanel> headerEditorPanel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Image loading runs on this single-thread pool, not the message thread --
    // RAW camera conversion (ImageIO decode + BMP write) plus the whole-buffer
    // waveform conversion took ~2s of measured message-thread stall on a
    // ~78MB image, freezing the UI (and the busy spinner) for the duration.
    // One-shot serialized jobs with no state between them, so a ThreadPool
    // (not another LivePreviewWorker-style mailbox thread) is the right
    // primitive. imageLoadInProgress is message-thread-only state: set when a
    // load is dispatched, cleared in finishImageLoad(); while set, every
    // image-mutating entry point (Load/Reset/Undo/Redo/Export/header edit/
    // plugin-session open/split toggle) is disabled, so the install can never
    // land into a live session or clobber a concurrent edit.
    juce::ThreadPool imageLoaderPool { 1 };
    bool imageLoadInProgress = false;

    // Liveness token for the load-completion callback. Component::SafePointer
    // alone is not enough at shutdown: it only nulls in ~Component, which runs
    // AFTER members are destroyed -- so a completion already queued when
    // ~MainComponent starts could still dispatch into partially-destroyed
    // members if a plugin's destructor pumps the run loop (the same hazard
    // ~MainComponent already defends against for previewBusySpinner). The
    // destructor resets this token first; the completion holds a weak_ptr and
    // bails if it can't lock.
    std::shared_ptr<void> imageLoadAliveToken = std::make_shared<int>(0);

    // Shared tail of the async image-load completion (success and failure
    // paths both funnel here): clears imageLoadInProgress, re-enables
    // everything the load disabled, and shows the outcome in the status bar.
    void finishImageLoad(const juce::String& statusText);

    // Caches the last live-preview result while pluginEditorPanel is open; Apply
    // reuses it directly instead of recomputing, so the committed bytes are
    // guaranteed identical to what was just previewed. livePreviewChannel
    // tracks which of the two "edited source" fields below is the current
    // one: livePreviewChannelPlaneBytes (an edited channel plane, when
    // scoped) or livePreviewVisualOrderBytes (the edited whole visual-order
    // buffer, otherwise) — whichever applies is what endLivePreviewSession()
    // splices back via applyChannelBytes() or applyVisualOrderedBytes()
    // respectively, instead of a full setPixelBytes(). (The rendered juce::Image
    // and waveform float buffer built from these bytes are no longer cached
    // here at all -- LivePreviewWorker::Result carries them directly from the
    // worker thread to applyLivePreviewResult(), which just hands them to
    // imagePreview/waveformView without needing to keep its own copy.)
    std::optional<RawImage::Channel> livePreviewChannel;
    juce::MemoryBlock livePreviewChannelPlaneBytes;
    juce::MemoryBlock livePreviewVisualOrderBytes;

    // Bumped by endLivePreviewSession() (both the commit and discard
    // branches) -- echoed on every LivePreviewWorker::Request and checked
    // against on every Result, so a background pass that outlives its
    // session (Apply/Cancel/plugin swap already happened) is recognized as
    // stale and dropped by applyLivePreviewResult() instead of misapplied.
    uint64_t livePreviewEpoch = 0;

    // Per-session cache backing getOrBuildLivePreviewSource() -- cleared in
    // endLivePreviewSession() since workingImage->pixelBytes may change
    // (Apply commits new bytes) once the session ends.
    std::shared_ptr<const juce::MemoryBlock> cachedWholeBufferSource;
    std::array<std::shared_ptr<const juce::MemoryBlock>, 4> cachedChannelSource;

    // Tracks which channel lane (if any) currently owns the live selection
    // while split mode is on — only one lane has an active selection at a
    // time; starting a new one in a different lane clears the others.
    std::optional<RawImage::Channel> activeSelectionChannel;

    struct EditorSnapshot
    {
        juce::MemoryBlock headerBytes;
        juce::MemoryBlock pixelBytes;
        std::optional<RawImage::Channel> selectionChannel;
        juce::Range<int> selection;
    };

    std::vector<EditorSnapshot> undoStack;
    std::vector<EditorSnapshot> redoStack;
    juce::ApplicationCommandManager commandManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
