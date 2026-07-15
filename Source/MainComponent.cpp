#include "MainComponent.h"

void MainComponent::PluginListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                        int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);

    if (rowNumber >= cachedTypes.size())
        return;

    const auto& desc = cachedTypes.getReference(rowNumber);

    g.setColour(juce::Colours::black);
    g.drawText(desc.name + "  —  " + desc.manufacturerName + "  (" + desc.pluginFormatName + ")",
                4, 0, width - 8, height, juce::Justification::centredLeft);
}

MainComponent::MainComponent()
{
    addAndMakeVisible(rescanButton);
    rescanButton.onClick = [this] { refreshList(); };

    pluginListBox.setModel(&listModel);
    addAndMakeVisible(pluginListBox);

    setSize(700, 500);

    refreshList();
}

void MainComponent::refreshList()
{
    scanner.scanAll();
    listModel.refresh();
    pluginListBox.updateContent();
    pluginListBox.repaint();
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    rescanButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);
    pluginListBox.setBounds(area);
}
