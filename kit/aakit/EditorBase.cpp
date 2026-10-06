#include "EditorBase.h"
#include "Fonts.h"

namespace aa
{
using namespace juce;

EditorBase::EditorBase (PluginBase& p, int baseWidth, int baseHeight, const Theme& t)
    : AudioProcessorEditor (p),
      pluginBase (p),
      lookAndFeel (std::make_unique<LookAndFeel> (t)),
      baseW (baseWidth),
      baseH (baseHeight),
      vblank (this, [this]
      {
          const double now = Time::getMillisecondCounterHiRes() * 0.001;
          const double dt = lastFrameTime > 0.0 ? jlimit (0.0, 0.1, now - lastFrameTime) : 1.0 / 60.0;
          lastFrameTime = now;
          onFrame (now, dt);
      })
{
    setLookAndFeel (lookAndFeel.get());
    addAndMakeVisible (content);
    content.setBounds (0, 0, baseW, baseH);
    tooltips.setLookAndFeel (lookAndFeel.get());
}

EditorBase::~EditorBase()
{
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void EditorBase::useLookAndFeel (std::unique_ptr<LookAndFeel> newLnf)
{
    if (newLnf == nullptr)
        return;

    auto old = std::move (lookAndFeel);
    lookAndFeel = std::move (newLnf);
    setLookAndFeel (lookAndFeel.get());
    tooltips.setLookAndFeel (lookAndFeel.get());
}

void EditorBase::finishSetup()
{
    // Apply the look & feel again in case the subclass swapped it out.
    setLookAndFeel (lookAndFeel.get());
    tooltips.setLookAndFeel (lookAndFeel.get());
    sendLookAndFeelChange();

    setResizable (true, true);
    const double ratio = (double) baseW / (double) baseH;
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio (ratio);
        c->setSizeLimits (roundToInt (baseW * 0.6), roundToInt (baseH * 0.6),
                          roundToInt (baseW * 2.5), roundToInt (baseH * 2.5));
    }

    const float s = jlimit (0.6f, 2.5f, pluginBase.uiScale);
    setSize (roundToInt ((float) baseW * s), roundToInt ((float) baseH * s));
    content.resized();
}

void EditorBase::resized()
{
    zoom = (float) getWidth() / (float) baseW;
    content.setTransform (AffineTransform::scale (zoom));
    pluginBase.uiScale = zoom;
}

void EditorBase::paint (Graphics& g)
{
    g.fillAll (theme().background);
}

void EditorBase::paintStandardHeader (Graphics& g, Rectangle<float> area, const String& title,
                                      const String& subtitle, Colour titleColour)
{
    const auto& t = theme();
    auto left = area.removeFromLeft (area.getWidth() * 0.36f);

    g.setColour (titleColour);
    g.setFont (Fonts::display (area.getHeight() * 0.62f));
    g.drawText (title, left.removeFromTop (area.getHeight() * 0.72f).toNearestInt(), Justification::bottomLeft, false);

    g.setColour (t.textDim);
    g.setFont (Fonts::ui (11.0f).withExtraKerningFactor (0.08f));
    g.drawText (subtitle.toUpperCase(), left.toNearestInt(), Justification::topLeft, false);

    auto right = area.removeFromRight (area.getWidth() * 0.36f);
    g.setColour (t.textDim.withMultipliedAlpha (0.8f));
    g.setFont (Fonts::uiBold (11.0f).withExtraKerningFactor (0.2f));
    g.drawText ("ADVENTURE AUDIO", right.toNearestInt(), Justification::centredRight, false);
}
} // namespace aa
