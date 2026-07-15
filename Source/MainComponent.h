#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "FavouritePluginsStore.h"
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
    void resetClicked();
    void undoClicked();
    void redoClicked();
    void pushUndoState();
    void updatePreview(bool resetView = false);
    void updateWaveform(bool resetView = false);
    void updatePluginListEnablement();
    void syncScrollBarToView();
    void setStatus(const juce::String& text);

    juce::MemoryBlock computeProcessedPixelBytes(juce::AudioPluginInstance& plugin,
                                                  const juce::Range<int>& selection);
    void refreshLivePreview();
    void endLivePreviewSession(bool commitToWorkingImage);

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
    juce::ListBox pluginListBox;
    juce::Label statusLabel;

    FavouritePluginsStore favouritePluginsStore;
    PluginListModel listModel { scanner.getKnownPluginList(), favouritePluginsStore };

    MainMenuModel menuModel { MainMenuModel::Callbacks {
        [this] { return scanner.isScanning(); },
        [this] { return pluginEditorPanel != nullptr; },
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
        }
    } };

    // Parents the plugin list and (optionally) the currently-open PluginEditorPanel;
    // parents the image preview, waveform section, and status label respectively.
    // Constructed after the raw controls above (declaration order == construction
    // order) so the references/pointers they parent are already valid.
    LeftColumnPanel leftColumn { pluginListBox };
    RightColumnPanel rightColumn { imagePreview, statusLabel, waveformView, waveformZoomSlider,
                                    waveformZoomLabel, horizontalZoomSlider, horizontalScrollBar };

    juce::StretchableLayoutManager outerLayout;
    juce::StretchableLayoutResizerBar outerResizerBar { &outerLayout, 1, true /*vertical bar, dragged left/right*/ };

    // Declared before currentPlugin so it destructs (and detaches) first —
    // member destruction order is the reverse of declaration order.
    PluginParameterWatcher pluginParamWatcher;

    std::unique_ptr<RawImage> originalImage;
    std::unique_ptr<RawImage> workingImage;
    std::unique_ptr<juce::AudioPluginInstance> currentPlugin;
    std::unique_ptr<PluginEditorPanel> pluginEditorPanel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Caches the last live-preview result while pluginEditorPanel is open; Apply
    // reuses it directly instead of recomputing, so the committed bytes are
    // guaranteed identical to what was just previewed.
    juce::MemoryBlock livePreviewBytes;

    struct EditorSnapshot
    {
        juce::MemoryBlock pixelBytes;
        juce::Range<int> selection;
    };

    std::vector<EditorSnapshot> undoStack;
    std::vector<EditorSnapshot> redoStack;
    juce::ApplicationCommandManager commandManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
