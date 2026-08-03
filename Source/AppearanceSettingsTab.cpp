#include "AppearanceSettingsTab.h"
#include "RawdogLookAndFeel.h"

namespace
{
    void drawSunIcon(juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto centre = area.getCentre();
        const float coreRadius = juce::jmin(area.getWidth(), area.getHeight()) * 0.22f;

        g.fillEllipse(juce::Rectangle<float>(coreRadius * 2.0f, coreRadius * 2.0f).withCentre(centre));

        const float rayInner = coreRadius * 1.6f;
        const float rayOuter = coreRadius * 2.2f;
        for (int i = 0; i < 8; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
            g.drawLine({ centre.getPointOnCircumference(rayInner, angle),
                         centre.getPointOnCircumference(rayOuter, angle) },
                       1.5f);
        }
    }

    void drawMoonIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour iconColour, juce::Colour eraseColour)
    {
        const float r = juce::jmin(area.getWidth(), area.getHeight()) * 0.4f;
        const auto centre = area.getCentre();

        g.setColour(iconColour);
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(centre));

        g.setColour(eraseColour);
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(centre.translated(r * 0.6f, 0.0f)));
    }
}

void AppearanceSettingsTab::ThemeButton::paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    const auto backgroundColour = findColour(getToggleState() ? juce::TextButton::buttonOnColourId
                                                                : juce::TextButton::buttonColourId);
    getLookAndFeel().drawButtonBackground(g, *this, backgroundColour, isMouseOverButton, isButtonDown);

    auto area = getLocalBounds().toFloat().reduced(10.0f, 0.0f);

    const float iconSize = juce::jmin(16.0f, area.getHeight() - 6.0f);
    auto iconArea = area.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize);
    area.removeFromLeft(8.0f); // gap between icon and label, so they don't read as touching

    const auto iconColour = findColour(getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (icon == Icon::sun)
    {
        g.setColour(iconColour);
        drawSunIcon(g, iconArea);
    }
    else
    {
        drawMoonIcon(g, iconArea, iconColour, backgroundColour);
    }

    g.setColour(iconColour);
    g.setFont(RawdogLookAndFeel::chromeFont(11.0f));
    g.drawText(getButtonText(), area.toNearestInt(), juce::Justification::centredLeft);
}

AppearanceSettingsTab::AppearanceSettingsTab(AppearanceSettingsStore& storeIn, std::function<void()> onAppearanceChangedIn)
    : store(storeIn), onAppearanceChanged(std::move(onAppearanceChangedIn))
{
    appearanceLabel.setFont(RawdogLookAndFeel::chromeFont(11.0f));
    addAndMakeVisible(appearanceLabel);

    constexpr int themeRadioGroupId = 1;

    for (auto* button : { &lightButton, &darkButton })
    {
        button->setRadioGroupId(themeRadioGroupId);
        button->setClickingTogglesState(true);
        addAndMakeVisible(button);
    }

    const bool darkModeEnabled = store.isDarkModeEnabled();
    lightButton.setToggleState(! darkModeEnabled, juce::dontSendNotification);
    darkButton.setToggleState(darkModeEnabled, juce::dontSendNotification);

    lightButton.onClick = [this] { setDarkModeEnabled(false); };
    darkButton.onClick = [this] { setDarkModeEnabled(true); };
}

void AppearanceSettingsTab::setDarkModeEnabled(bool enabled)
{
    store.setDarkModeEnabled(enabled);

    // The store above only persists the choice for the next launch --
    // Palette::get() itself is a separate in-memory flag, so without this the
    // running app would keep showing the old theme until restarted.
    RawdogLookAndFeel::Palette::setDarkModeEnabled(enabled);

    if (onAppearanceChanged)
        onAppearanceChanged();
}

void AppearanceSettingsTab::resized()
{
    auto area = getLocalBounds().reduced(12);

    appearanceLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);

    auto row = area.removeFromTop(32);
    lightButton.setBounds(row.removeFromLeft(120));
    row.removeFromLeft(8);
    darkButton.setBounds(row.removeFromLeft(120));
}
