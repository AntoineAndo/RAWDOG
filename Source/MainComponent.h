#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <optional>
#include "FavouritePluginsStore.h"
#include "HeaderEditorPanel.h"
#include "LeftColumnPanel.h"
#include "MainMenuModel.h"
#include "PluginEditorPanel.h"
#include "PluginListModel.h"
#include "PluginParameterWatcher.h"
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

private:
    enum CommandIDs
    {
        undoCommand = 1000,
        redoCommand
    };

    void refreshPluginList();
    void loadImageClicked();
    void exportImageClicked();
    void loadAndOpenPlugin(int row);
    void openEditorClicked();
    void applyClicked();
    void cancelEditorClicked();
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

    juce::MemoryBlock computeProcessedPixelBytes(juce::AudioPluginInstance& plugin,
                                                  const juce::Range<int>& selection,
                                                  std::optional<RawImage::Channel> channel = std::nullopt);
    void refreshLivePreview();
    void endLivePreviewSession(bool commitToWorkingImage);

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

    // Restores a captured SelectionScope (from an undo/redo entry): clears
    // every lane's selection first, switches split mode on/off to match
    // whether the entry has a channel (so the restored selection is actually
    // visible), then sets just the target lane's range.
    void restoreSelectionScope(std::optional<RawImage::Channel> channel, juce::Range<int> range);

    // Enables/disables split-channel display. Entering split mode lazily
    // (re)computes the 3 channel planes (cheap if already up to date, per
    // RawImage's own dirty-flag caching) and populates all 3 lanes; leaving
    // it clears the per-channel selection-tracking state. Does not touch
    // pixelBytes/headerBytes — purely a view/selection-tracking concern.
    void setSplitMode(bool enabled);

    // Repopulates all 3 channel lanes' buffers from workingImage's current
    // per-channel planes. No-op if there's no image or it isn't a 3-channel one.
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
    static constexpr int blockSize = 512;

    PluginScanner scanner;

    juce::Slider waveformZoomSlider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label waveformZoomLabel { {}, "Zoom" };
    juce::Slider horizontalZoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ScrollBar horizontalScrollBar { false };

    ZoomableImageView imagePreview;
    WaveformView waveformView;
    std::array<WaveformView, 3> channelWaveformViews; // indexed by (int) RawImage::Channel
    juce::TextButton splitModeToggle { "Split Channels" };
    juce::Label sampleModeLabel { {}, "Sample Mode:" };
    juce::ComboBox sampleModeCombo;
    juce::ListBox pluginListBox;
    juce::Label statusLabel;

    FavouritePluginsStore favouritePluginsStore;
    PluginListModel listModel { scanner.getKnownPluginList(), favouritePluginsStore };

    MainMenuModel menuModel { MainMenuModel::Callbacks {
        [this] { return scanner.isScanning(); },
        [this] { return pluginEditorPanel != nullptr || headerEditorPanel != nullptr; },
        [this] { return workingImage != nullptr; },
        [this] { return originalImage != nullptr; },
        [this] { loadImageClicked(); },
        [this] { exportImageClicked(); },
        [this] { resetClicked(); },
        [this] { refreshPluginList(); },
        [this](juce::PopupMenu& menu)
        {
            menu.addCommandItem(&commandManager, undoCommand);
            menu.addCommandItem(&commandManager, redoCommand);
        },
        [this] { return workingImage != nullptr && workingImage->getFormat() == RawImage::Format::bmp; },
        [this] { openHeaderEditorClicked(); }
    } };

    // Parents the plugin list and (optionally) the currently-open PluginEditorPanel;
    // parents the image preview, waveform section, and status label respectively.
    // Constructed after the raw controls above (declaration order == construction
    // order) so the references/pointers they parent are already valid.
    LeftColumnPanel leftColumn { pluginListBox };
    RightColumnPanel rightColumn { imagePreview, statusLabel, waveformView, waveformZoomSlider,
                                    waveformZoomLabel, horizontalZoomSlider, horizontalScrollBar,
                                    channelWaveformViews[0], channelWaveformViews[1], channelWaveformViews[2],
                                    splitModeToggle, sampleModeLabel, sampleModeCombo };

    juce::StretchableLayoutManager outerLayout;
    juce::StretchableLayoutResizerBar outerResizerBar { &outerLayout, 1, true /*vertical bar, dragged left/right*/ };

    // Declared before currentPlugin so it destructs (and detaches) first —
    // member destruction order is the reverse of declaration order.
    PluginParameterWatcher pluginParamWatcher;

    std::unique_ptr<RawImage> originalImage;
    std::unique_ptr<RawImage> workingImage;
    std::unique_ptr<juce::AudioPluginInstance> currentPlugin;
    std::unique_ptr<PluginEditorPanel> pluginEditorPanel;

    // While headerEditorPanel is open, headerEditScratch is a scratch copy of
    // workingImage that every field edit is applied to for live preview —
    // workingImage itself is only touched by applyHeaderEditClicked().
    std::unique_ptr<RawImage> headerEditScratch;
    std::unique_ptr<HeaderEditorPanel> headerEditorPanel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Caches the last live-preview result while pluginEditorPanel is open; Apply
    // reuses it directly instead of recomputing, so the committed bytes are
    // guaranteed identical to what was just previewed. livePreviewBytes is
    // always the full interleaved buffer in pixelBytes' own real (possibly
    // bottom-up/padded) layout — what's actually rendered for display, via
    // previewWithChannelBytes() or previewWithVisualOrderedBytes() depending on
    // scope. livePreviewChannel tracks which of the two "edited source" fields
    // below is the current one: livePreviewChannelPlaneBytes (an edited
    // channel plane, when scoped) or livePreviewVisualOrderBytes (the edited
    // whole visual-order buffer, otherwise) — whichever applies is what
    // endLivePreviewSession() splices back via applyChannelBytes() or
    // applyVisualOrderedBytes() respectively, instead of a full setPixelBytes().
    juce::MemoryBlock livePreviewBytes;
    std::optional<RawImage::Channel> livePreviewChannel;
    juce::MemoryBlock livePreviewChannelPlaneBytes;
    juce::MemoryBlock livePreviewVisualOrderBytes;

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
