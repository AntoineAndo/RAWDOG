#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <optional>
#include "BusySpinner.h"
#include "ChainSlot.h"
#include "EffectChainPanel.h"
#include "ExportSettingsStore.h"
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
                      public juce::ApplicationCommandTarget,
                      public juce::FileDragAndDropTarget
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

    ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

    // Called by RawdogApplication before actually quitting (from
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
        resetCommand, // Cmd+Shift+R — same command-item treatment as
                      // loadImageCommand above, via populateFileMenuResetItem.
        exportImageCommand // Cmd+S — same command-item treatment again, via
                           // populateFileMenuExportItem.
    };

    void refreshPluginList();
    void loadImageClicked();

    // Shared tail of loadImageClicked()'s file-chooser callback and
    // filesDropped() below -- both just need to hand a resolved juce::File
    // off to the same imageLoaderPool dispatch. Guarded the same way
    // loadImageClicked() is: a no-op while imageLoadInProgress.
    void loadImageFile(const juce::File& file);

    // juce::FileDragAndDropTarget overrides, letting the whole window act as
    // a drop target for opening an image -- interested only while there's no
    // image loaded/loading/being edited (same guard loadImageClicked() itself
    // already applies via imageLoadInProgress, plus workingImage == nullptr
    // so a drag can't clobber a session that's already open) and the
    // dragged file's extension matches an accepted image type.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void exportImageClicked();

    // Double-clicking a plugin row always appends to pluginChain (never
    // replaces it) and selects the new slot -- see selectChainSlot() below.
    // Validates/instantiates the new plugin before touching pluginChain at
    // all, so a failed load never affects any already-loaded slot.
    void addPluginToChain(int row);

    // Mounts pluginChain[index]'s native editor (+ its own Automation tab) in
    // the left column. If a different slot is currently selected,
    // first writes its live ramps back into ChainSlot::ramps (see
    // refreshLivePreview()'s doc comment for why that matters) and tears its
    // panel down before mounting the new one. A no-op if index is already
    // selected.
    void selectChainSlot(int index);

    // Wired to PluginEditorPanel's OK button: stops showing the currently
    // selected slot's editor (selectedChainSlot -> -1, pluginEditorPanel ->
    // nullptr) WITHOUT touching pluginChain at all -- the slot stays in the
    // chain, still live-previewed, exactly as it was. This is what makes
    // "a chain session is open" (pluginChain non-empty) and "a specific
    // slot's editor is mounted" (pluginEditorPanel non-null) two genuinely
    // independent facts elsewhere in this class -- see the guards in
    // refreshLivePreview()/applyLivePreviewResult() and every
    // getCommandInfo() case, all of which key off pluginChain.empty() now,
    // not pluginEditorPanel, so Undo/Redo/Load/Reset stay correctly disabled
    // even with nothing selected. A no-op if nothing is currently selected.
    void deselectChainSlot();

    // Removes pluginChain[index]. Delegates to endLivePreviewSession(false)
    // (the same teardown Cancel uses) if this is the last slot, since an
    // empty chain is defined as "no session open." Otherwise fixes up
    // selectedChainSlot (three cases -- see PROJECT.md/the implementation
    // plan) and only swaps the mounted editor if the removed slot was the
    // selected one.
    void removeChainSlot(int index);

    // Moves pluginChain[from] to land at index `to` in the resulting array
    // (erase-then-insert, not a swap -- an arbitrary drag-to-reorder target can
    // land anywhere, not just an adjacent slot), shifting everything strictly
    // between the two positions by one, and fixes up selectedChainSlot to
    // follow whichever slot it pointed at. Safe without flushing the
    // live-preview worker: this only relocates the ChainSlot/unique_ptr value,
    // never the pointee, so any raw AudioPluginInstance* already captured in
    // an in-flight request stays valid.
    void moveChainSlot(int from, int to);

    void toggleChainSlotBypass(int index);

    // Called after every chain mutation/selection/bypass change to re-derive
    // the rack UI's rows from the current pluginChain/selectedChainSlot state.
    void refreshEffectChainPanel();

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

    // Refreshes imageSizeLabel from the given image's current dimensions and
    // pixel-data byte size -- called wherever the displayed image's identity
    // can change (load, reset, undo/redo, apply, and the BMP header editor's
    // width/height fields), not on every live-preview pass (those don't
    // change dimensions).
    void updateImageSizeLabel(const RawImage& image);
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

    // The callback LivePreviewWorker::onResultReady is wired to, applying the
    // same image/waveform-update logic as refreshLivePreview()'s tail whenever
    // a background pass completes.
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

    // The effect chain "rack" -- lives under the left column's "Effect Chain"
    // tab (see LeftColumnPanel), threaded into its constructor exactly like
    // pluginListBox is. Its callbacks are wired in the constructor to
    // selectChainSlot()/removeChainSlot()/moveChainSlot()/toggleChainSlotBypass(),
    // plus onAddEffectClicked (no slot involved) to leftColumn.showPluginsTab().
    EffectChainPanel effectChainPanel;

    juce::Label sampleModeLabel { {}, "Sample Mode:" };
    juce::ComboBox sampleModeCombo;

    // Shown top-right of the Sample Mode strip -- "1920 x 1080 - 4.2 MB",
    // refreshed by updateImageSizeLabel() wherever the displayed image's
    // dimensions/pixel data can change (load, reset, undo/redo, apply, and
    // the BMP header editor's width/height fields).
    juce::Label imageSizeLabel;
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
        [this] { return ! pluginChain.empty() || headerEditorPanel != nullptr || imageLoadInProgress; },
        [this] { refreshPluginList(); },
        [this](juce::PopupMenu& menu)
        {
            menu.addCommandItem(&commandManager, undoCommand);
            menu.addCommandItem(&commandManager, redoCommand);
        },
        [this] { return workingImage != nullptr && workingImage->getFormat() == RawImage::Format::bmp; },
        [this] { openHeaderEditorClicked(); },
        [this](juce::PopupMenu& menu) { menu.addCommandItem(&commandManager, loadImageCommand); },
        [this](juce::PopupMenu& menu) { menu.addCommandItem(&commandManager, resetCommand); },
        [this](juce::PopupMenu& menu) { menu.addCommandItem(&commandManager, exportImageCommand); }
    } };

    // Parents the effect-chain rack + plugin list (+ optionally the
    // currently-open PluginEditorPanel); parents the image preview, waveform
    // section, and status label respectively. Constructed after the raw
    // controls above (declaration order == construction order) so the
    // references/pointers they parent are already valid.
    LeftColumnPanel leftColumn { pluginListBox, effectChainPanel };
    RightColumnPanel rightColumn { imagePreview, statusLabel, waveformView, horizontalScrollBar,
                                    channelWaveformViews[0], channelWaveformViews[1], channelWaveformViews[2],
                                    channelWaveformViews[3],
                                    splitModeToggle,
                                    sampleModeLabel, sampleModeCombo, imageSizeLabel, previewBusySpinner };

    juce::StretchableLayoutManager outerLayout;
    GrippedResizerBar outerResizerBar { &outerLayout, 1, true /*vertical bar, dragged left/right*/ };

    // Declared before pluginChain so it destructs (and detaches) first —
    // member destruction order is the reverse of declaration order. Stays a
    // single instance, re-attachTo()'d to whichever chain slot is currently
    // selected/mounted -- only the selected slot ever has a live native
    // editor a user could actually be tweaking.
    PluginParameterWatcher pluginParamWatcher;

    std::unique_ptr<RawImage> originalImage;
    std::unique_ptr<RawImage> workingImage;

    // Set from the loaded file's name (sans extension) once loadImageFile()'s
    // async job installs successfully; used by exportImageClicked() to name
    // the exported PNG "<loadedImageBaseName>_modified.png". Left as-is on a
    // failed load (workingImage stays whatever it was before, so this should
    // too) and on Reset to Original (still the same source file).
    juce::String loadedImageBaseName;

    ExportSettingsStore exportSettingsStore;

    // The effect chain, in DSP order (index 0 processed first). Empty means
    // "no plugin session open". Only ever grown by addPluginToChain() (after a
    // successful PluginHost::createInstance()) and fully cleared by
    // endLivePreviewSession() (Apply and Cancel both tear down the whole
    // chain) or by removeChainSlot() shrinking it down to zero.
    std::vector<ChainSlot> pluginChain;

    // Index into pluginChain of whichever slot's editor is currently mounted
    // in pluginEditorPanel, or -1 if nothing is selected -- which can happen
    // even with pluginChain non-empty (see deselectChainSlot()), so this is
    // NOT the same fact as "is a chain session open" (pluginChain.empty()).
    // -1 whenever pluginChain itself is empty, but not only then.
    int selectedChainSlot = -1;

    // Declared after pluginChain as a matter of style (matching
    // pluginParamWatcher's ordering rationale above), but the real safety
    // guarantee is the explicit livePreviewWorker.stopThread() call in
    // ~MainComponent(), *before* releasing any chain slot's plugin -- not
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
    // and waveform float buffer built from these bytes aren't cached here:
    // LivePreviewWorker::Result carries them directly from the worker thread
    // to applyLivePreviewResult(), which just hands them to
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

    // A frozen, re-instantiable copy of one ChainSlot -- unlike ChainSlot
    // itself, holds no live plugin instance (unique_ptr<AudioPluginInstance>
    // can't sit in an undo/redo stack: not copyable, and keeping N chains'
    // worth of dormant native instances alive indefinitely is wasteful).
    // description + pluginState (via getStateInformation(), same call
    // savePresetClicked() already makes) are enough to recreate an
    // equivalent, fully-configured instance later via
    // PluginHost::createInstance() + setStateInformation() -- the same
    // create-then-restore-state order addPluginToChain() already uses for a
    // saved preset row.
    struct ChainSlotSnapshot
    {
        juce::PluginDescription description;
        juce::MemoryBlock pluginState;
        std::vector<ParameterAutomation> ramps;
        bool bypassed = false;
    };

    struct EditorSnapshot
    {
        juce::MemoryBlock headerBytes;
        juce::MemoryBlock pixelBytes;
        std::optional<RawImage::Channel> selectionChannel;
        juce::Range<int> selection;

        // Empty for every undo entry except the one pushed by applyClicked()
        // (every other pushUndoState() call site only ever fires while
        // pluginChain.empty() already holds) -- lets Undo restore not just
        // the pre-Apply image bytes but the exact chain that produced them,
        // instead of leaving the rack empty.
        std::vector<ChainSlotSnapshot> chain;
    };

    std::vector<EditorSnapshot> undoStack;
    std::vector<EditorSnapshot> redoStack;

    // Captures the current pluginChain as re-instantiable snapshots (see
    // ChainSlotSnapshot above) -- called by pushUndoState() so an undo entry
    // can later restore the chain, not just the image bytes it produced.
    std::vector<ChainSlotSnapshot> captureChainSnapshot() const;

    // The inverse of captureChainSnapshot(): tears down whatever's currently
    // in pluginChain (defensive -- always empty here in practice, since
    // Undo/Redo are only ever active while pluginChain.empty() already
    // holds) and recreates each slot via PluginHost::createInstance() +
    // setStateInformation(). A slot whose plugin can no longer be
    // instantiated (uninstalled since, etc.) is skipped rather than aborting
    // the whole restore, reported via setStatus().
    void restoreChainFromSnapshot(const std::vector<ChainSlotSnapshot>& snapshot);

    juce::ApplicationCommandManager commandManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
