#include "SynthEditor.h"
#include <PluginAssets.h>

using namespace juce;
using namespace ers;
using namespace ers::ui;

namespace
{
    constexpr int baseWidth = 1240, baseHeight = 800;
    constexpr int headerH = 58;
    constexpr int gap = 8;

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = bgTop;
        t.backgroundAlt = bgBottom;
        t.panel = panelFill;
        t.panelOutline = panelEdge;
        t.text = textBright;
        t.textDim = textDim;
        t.accent = oscAccent;
        t.accent2 = lfoAccent;
        t.knobBody = Colour (0xff23284a);
        t.knobTrack = Colours::black.withAlpha (0.35f);
        t.shadow = Colours::black.withAlpha (0.6f);
        t.popupBackground = Colour (0xf50d1020);
        t.pill = Colours::black.withAlpha (0.35f);
        t.cornerRadius = 10.0f;
        t.glow = true;
        return t;
    }
}

//==============================================================================
/** SYNTH / MATRIX / FX page buttons in the header. */
class SynthEditor::TabStrip : public Component
{
public:
    explicit TabStrip (SynthEditor& e) : editor (e) {}

    void paint (Graphics& g) override
    {
        static const char* names[] = { "SYNTH", "MATRIX", "FX" };
        const float w = (float) getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            auto r = Rectangle<float> (w * (float) i, 0.0f, w, (float) getHeight()).reduced (3.0f, 6.0f);
            const bool sel = (int) editor.page == i;
            g.setColour (sel ? oscAccent.withAlpha (0.16f) : Colours::white.withAlpha (0.03f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (sel ? oscAccent : panelEdge);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            g.setFont (aa::Fonts::display (12.5f));
            g.setColour (sel ? textBright : textDim);
            g.drawText (names[i], r.toNearestInt(), Justification::centred);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        const int i = jlimit (0, 2, e.x * 3 / jmax (1, getWidth()));
        editor.showPage ((Page) i);
    }

private:
    SynthEditor& editor;
};

//==============================================================================
SynthEditor::SynthEditor (SynthProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      subNoise (p),
      filter1 (p, 0), filter2 (p, 1),
      filterRouting (p.apvts, "filterRouting", {}, aa::ChoiceBox::Style::segmented),
      modulation (p), macros (p), voicing (p),
      matrixPage (p), fxPage (p),
      scope (p), presets (p),
      volume (p, "volume", "Volume", textBright)
{
    aa::Fonts::setDisplayTypeface (PluginAssets::AudiowideRegular_ttf, PluginAssets::AudiowideRegular_ttfSize);

    for (int o = 0; o < numOscs; ++o)
    {
        oscPanels[(size_t) o] = std::make_unique<OscPanel> (p, o);
        content.addAndMakeVisible (*oscPanels[(size_t) o]);
        oscPanels[(size_t) o]->collectKnobs (knobs);
    }

    tabs = std::make_unique<TabStrip> (*this);
    for (Component* c : std::initializer_list<Component*> { &subNoise, &filter1, &filter2, &filterRouting, &modulation, &macros,
                                                            &voicing, &scope, &presets, &volume, tabs.get() })
        content.addAndMakeVisible (c);
    content.addChildComponent (matrixPage);
    content.addChildComponent (fxPage);

    filterRouting.setAccent (filterAccent);
    filterRouting.setShortNames ({ "SERIAL", "PARALLEL" });
    filterRouting.setTooltip ("Filter routing: Filter 1 into Filter 2, or both side by side");
    presets.setAccent (oscAccent);

    subNoise.collectKnobs (knobs);
    filter1.collectKnobs (knobs);
    filter2.collectKnobs (knobs);
    macros.collectKnobs (knobs);
    voicing.collectKnobs (knobs);
    knobs.push_back (&volume);

    finishSetup();
}

SynthEditor::~SynthEditor() = default;

void SynthEditor::showPage (Page p)
{
    page = p;
    const bool synth = p == Page::synth;
    for (auto& o : oscPanels)
        o->setVisible (synth);
    for (Component* c : std::initializer_list<Component*> { &subNoise, &filter1, &filter2, &filterRouting })
        c->setVisible (synth);
    matrixPage.setVisible (p == Page::matrix);
    fxPage.setVisible (p == Page::fx);
    tabs->repaint();
}

void SynthEditor::paintContent (Graphics& g)
{
    auto r = baseBounds().toFloat();
    g.setGradientFill (ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, r.getBottom(), false));
    g.fillRect (r);

    // a faint aurora across the top
    g.setGradientFill (ColourGradient (oscAccent.withAlpha (0.10f), r.getWidth() * 0.15f, 0.0f,
                                       Colours::transparentBlack, r.getWidth() * 0.15f, 220.0f, true));
    g.fillRect (r.withHeight (260.0f));
    g.setGradientFill (ColourGradient (lfoAccent.withAlpha (0.10f), r.getWidth() * 0.8f, 0.0f,
                                       Colours::transparentBlack, r.getWidth() * 0.8f, 240.0f, true));
    g.fillRect (r.withHeight (260.0f));

    // header
    auto header = r.removeFromTop ((float) headerH);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRect (header);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.fillRect (header.removeFromBottom (1.0f));

    // logo: "ericrius" + "Synth"
    auto logo = Rectangle<float> (18.0f, 8.0f, 260.0f, 42.0f);
    const auto f1 = aa::Fonts::display (25.0f);
    const float w1 = aa::Fonts::textWidth (f1, "ericrius");
    {
        GlyphArrangement ga;
        ga.addLineOfText (f1, "ericrius", logo.getX(), logo.getCentreY() + 9.0f);
        Path p;
        ga.createPath (p);
        g.setGradientFill (ColourGradient (oscAccent, logo.getX(), 0.0f, lfoAccent, logo.getX() + w1, 0.0f, false));
        g.fillPath (p);
    }
    g.setFont (aa::Fonts::ui (24.0f));
    g.setColour (textBright);
    g.drawText ("Synth", Rectangle<float> (logo.getX() + w1 + 3.0f, logo.getY(), 100.0f, logo.getHeight()).toNearestInt(),
                Justification::centredLeft);

    g.setFont (aa::Fonts::ui (10.0f));
    g.setColour (textDim);
    const int voices = proc.engine.ui.activeVoices.load();
    g.drawText (String (voices) + (voices == 1 ? " VOICE" : " VOICES"),
                Rectangle<int> (baseWidth - 380, 0, 70, headerH), Justification::centredRight);
}

void SynthEditor::layoutContent()
{
    auto r = baseBounds();
    auto header = r.removeFromTop (headerH);
    tabs->setBounds (header.getX() + 250, 8, 270, headerH - 16);
    presets.setBounds (header.getCentreX() - 70, 14, 320, 30);
    volume.setBounds (baseWidth - 74, 2, 56, headerH - 4);
    scope.setBounds (baseWidth - 300, 10, 210, headerH - 20);

    r = r.reduced (10, 0);
    r.removeFromTop (gap);

    // bottom row is shared by every page
    auto bottom = r.removeFromBottom (232);
    r.removeFromBottom (gap);
    bottom.removeFromBottom (10);
    voicing.setBounds (bottom.removeFromRight (236));
    bottom.removeFromRight (gap);
    macros.setBounds (bottom.removeFromRight (250));
    bottom.removeFromRight (gap);
    modulation.setBounds (bottom);

    matrixPage.setBounds (r);
    fxPage.setBounds (r);

    auto oscRow = r.removeFromTop (318);
    const int oscW = (oscRow.getWidth() - 2 * gap) / 3;
    for (int o = 0; o < numOscs; ++o)
    {
        oscPanels[(size_t) o]->setBounds (oscRow.removeFromLeft (oscW));
        oscRow.removeFromLeft (gap);
    }

    r.removeFromTop (gap);
    auto filterRow = r;
    subNoise.setBounds (filterRow.removeFromLeft (360));
    filterRow.removeFromLeft (gap);
    const int fw = (filterRow.getWidth() - gap) / 2;
    filter1.setBounds (filterRow.removeFromLeft (fw));
    filterRow.removeFromLeft (gap);
    filter2.setBounds (filterRow);
    filterRouting.setBounds (filter2.getX() + 98, filter2.getY() + 3, 150, 20);   // in filter 2's title strip
    filterRouting.toFront (false);
}

void SynthEditor::onFrame (double, double)
{
    for (auto* k : knobs)
        k->refreshModulation();
    for (auto& o : oscPanels)
        if (o->isVisible())
            o->refresh();
    if (filter1.isVisible())
    {
        filter1.refresh();
        filter2.refresh();
    }
    modulation.refresh();
    if (matrixPage.isVisible())
        matrixPage.refresh();
    if (fxPage.isVisible())
        fxPage.refresh();
    scope.refresh();

    const int voices = proc.engine.ui.activeVoices.load();
    if (voices != voicesShown)
    {
        voicesShown = voices;
        content.repaint (baseWidth - 380, 0, 70, headerH);
    }
}
