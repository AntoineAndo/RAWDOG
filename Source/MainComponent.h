#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginScanner.h"
#include "RawImage.h"
#include "WaveformView.h"
#include "ZoomableImageView.h"

class MainComponent : public juce::Component,
                      private juce::ScrollBar::Listener,
                      public juce::MenuBarModel
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    enum MenuItemIDs
    {
        loadImageMenuItem = 1,
        exportImageMenuItem,
        resetMenuItem,
        rescanPluginsMenuItem,
        undoMenuItem
    };

    void refreshPluginList();
    void loadImageClicked();
    void exportImageClicked();
    void loadAndOpenPlugin(int row);
    void openEditorClicked();
    void applyClicked();
    void resetClicked();
    void undoClicked();
    void pushUndoState();
    void updatePreview(bool resetView = false);
    void updateWaveform(bool resetView = false);
    void updatePluginListEnablement();
    void syncScrollBarToView();
    void setStatus(const juce::String& text);

    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

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

    class PluginListModel : public juce::ListBoxModel
    {
    public:
        explicit PluginListModel(juce::KnownPluginList& list) : knownPluginList(list) {}

        void refresh() { cachedTypes = knownPluginList.getTypes(); }
        const juce::PluginDescription* getType(int index) const
        {
            return juce::isPositiveAndBelow(index, cachedTypes.size()) ? &cachedTypes.getReference(index) : nullptr;
        }

        int getNumRows() override { return cachedTypes.size(); }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
        {
            if (enabled && onDoubleClick != nullptr)
                onDoubleClick(row);
        }

        void setEnabled(bool shouldBeEnabled) { enabled = shouldBeEnabled; }

        std::function<void(int)> onDoubleClick;

    private:
        juce::KnownPluginList& knownPluginList;
        juce::Array<juce::PluginDescription> cachedTypes;
        bool enabled = false;
    };

    PluginListModel listModel { scanner.getKnownPluginList() };

    // Wraps a plugin's editor with an "Apply to Whole Buffer" button underneath,
    // so tweaking parameters and applying them don't require closing the editor first.
    class EditorWithApplyButton : public juce::Component
    {
    public:
        EditorWithApplyButton(std::unique_ptr<juce::AudioProcessorEditor> editorIn, std::function<void()> onApply)
            : editor(std::move(editorIn))
        {
            addAndMakeVisible(*editor);
            addAndMakeVisible(applyButton);
            applyButton.onClick = std::move(onApply);
            setSize(editor->getWidth(), editor->getHeight() + 40);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            applyButton.setBounds(area.removeFromBottom(40).reduced(8));
            editor->setBounds(area);
        }

    private:
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        juce::TextButton applyButton { "Apply" };
    };

    class PluginWindow : public juce::DocumentWindow
    {
    public:
        PluginWindow(juce::AudioProcessorEditor* editor, std::function<void()> onApply)
            : DocumentWindow(editor->getName(), juce::Colours::darkgrey, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new EditorWithApplyButton(std::unique_ptr<juce::AudioProcessorEditor>(editor), std::move(onApply)), true);
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override { setVisible(false); }
    };

    std::unique_ptr<RawImage> originalImage;
    std::unique_ptr<RawImage> workingImage;
    std::unique_ptr<juce::AudioPluginInstance> currentPlugin;
    std::unique_ptr<PluginWindow> pluginWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<juce::MemoryBlock> undoStack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
