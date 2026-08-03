#include "AboutWindow.h"
#include "RawdogLookAndFeel.h"
#include "BinaryData.h"

AboutWindow::Content::Content(std::function<void()> onOkClicked)
{
    iconComponent.setImage(juce::ImageCache::getFromMemory(BinaryData::icon_png, BinaryData::icon_pngSize));
    addAndMakeVisible(iconComponent);

    titleLabel.setFont(RawdogLookAndFeel::chromeFont(22.0f));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    versionLabel.setText("Version " + juce::JUCEApplication::getInstance()->getApplicationVersion(),
                          juce::dontSendNotification);
    versionLabel.setFont(RawdogLookAndFeel::chromeFont(11.0f));
    versionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(versionLabel);

    authorLabel.setFont(RawdogLookAndFeel::chromeFont(11.0f));
    authorLabel.setJustificationType(juce::Justification::centred);
    authorLabel.setColour(juce::Label::textColourId, RawdogLookAndFeel::Palette::get().inkMuted);
    addAndMakeVisible(authorLabel);

    for (auto* link : { &emailLink, &githubLink })
    {
        link->setFont(RawdogLookAndFeel::chromeFont(11.0f), false, juce::Justification::centred);
        addAndMakeVisible(link);
    }

    RawdogLookAndFeel::setEmphasized(okButton);
    okButton.onClick = std::move(onOkClicked);
    addAndMakeVisible(okButton);
}

void AboutWindow::Content::resized()
{
    auto area = getLocalBounds().reduced(20);

    iconComponent.setBounds(area.removeFromTop(64).withSizeKeepingCentre(64, 64));
    area.removeFromTop(10);
    titleLabel.setBounds(area.removeFromTop(32));
    area.removeFromTop(4);
    versionLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(14);
    authorLabel.setBounds(area.removeFromTop(18));
    emailLink.setBounds(area.removeFromTop(18));
    githubLink.setBounds(area.removeFromTop(18));

    okButton.setBounds(area.removeFromBottom(28).withSizeKeepingCentre(90, 28));
}

AboutWindow::AboutWindow()
    : DocumentWindow("About", RawdogLookAndFeel::Palette::get().windowBg, DocumentWindow::closeButton),
      content([this] { setVisible(false); })
{
    setUsingNativeTitleBar(true);
    setResizable(false, false);

    // content is a member, not heap-allocated -- setContentOwned() would have
    // DocumentWindow's destructor try to delete it, double-freeing.
    setContentNonOwned(&content, true);

    centreWithSize(320, 280);
}

void AboutWindow::lookAndFeelChanged()
{
    setBackgroundColour(RawdogLookAndFeel::Palette::get().windowBg);
}
