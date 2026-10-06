#include "Widgets.h"

namespace aa
{
using namespace juce;

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& state, const String& id, const String& labelText)
    : paramID (id), label (labelText)
{
    setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (240);
    setScrollWheelEnabled (true);
    setVelocityBasedMode (false);
    setRepaintsOnMouseActivity (false);

    auto* p = state.getParameter (id);
    jassert (p != nullptr); // unknown parameter id!

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, id, *this);

    if (p != nullptr)
    {
        setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        setTooltip (p->getName (64));
    }
}

Knob::~Knob() = default;

float Knob::getProportion() const
{
    return (float) getNormalisableRange().convertTo0to1 (jlimit (getMinimum(), getMaximum(), getValue()));
}

Rectangle<float> Knob::getDialBounds() const
{
    auto b = getLocalBounds().toFloat();
    if (captionVisible)
        b.removeFromBottom (jlimit (12.0f, 18.0f, b.getHeight() * 0.2f));
    const float s = jmin (b.getWidth(), b.getHeight());
    return b.withSizeKeepingCentre (s, s);
}

void Knob::paint (Graphics& g)
{
    auto* lnf = findLookAndFeel (*this);
    if (lnf == nullptr)
        return;

    auto b = getLocalBounds().toFloat();
    Rectangle<float> caption;
    if (captionVisible)
        caption = b.removeFromBottom (jlimit (12.0f, 18.0f, b.getHeight() * 0.2f));

    const auto acc = getAccent();
    lnf->drawKnob (g, getDialBounds(), getProportion(), bipolar, hovered, dragging, acc, *this);

    if (captionVisible)
    {
        const bool showValue = hovered || dragging;
        lnf->drawKnobCaption (g, caption, showValue ? getTextFromValue (getValue()) : label, showValue, acc, *this);
    }
}

void Knob::mouseEnter (const MouseEvent& e)
{
    hovered = true;
    repaint();
    Slider::mouseEnter (e);
}

void Knob::mouseExit (const MouseEvent& e)
{
    hovered = false;
    repaint();
    Slider::mouseExit (e);
}

//==============================================================================
Toggle::Toggle (AudioProcessorValueTreeState& state, const String& id, const String& labelText, Style s)
    : label (labelText), style (s)
{
    attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, id, *this);
    if (auto* p = state.getParameter (id))
        setTooltip (p->getName (64));
    setClickingTogglesState (true);
}

Toggle::~Toggle() = default;

void Toggle::paintButton (Graphics& g, bool highlighted, bool down)
{
    const auto& t = themeFor (*this);
    const auto acc = accent.isTransparent() ? t.accent : accent;
    const bool on = getToggleState();
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    if (style == Style::pad)
    {
        if (on)
        {
            g.setColour (acc.withAlpha (0.25f));
            g.fillRoundedRectangle (b.expanded (1.0f), 8.0f);
        }
        g.setGradientFill (ColourGradient (on ? acc.brighter (0.2f) : t.knobBody.brighter (highlighted ? 0.3f : 0.15f),
                                           b.getX(), b.getY(),
                                           on ? acc.darker (0.35f) : t.knobBody.darker (0.3f), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b.reduced (down ? 1.5f : 0.0f), 7.0f);
        g.setColour (Colours::white.withAlpha (on ? 0.35f : 0.1f));
        g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (on ? Colours::black.withAlpha (0.8f) : t.text.withAlpha (0.85f));
        g.setFont (Fonts::uiBold (jlimit (10.0f, 14.0f, b.getHeight() * 0.4f)));
        g.drawFittedText (label.toUpperCase(), b.reduced (4.0f, 2.0f).toNearestInt(), Justification::centred, 2);
        return;
    }

    // Pill switch on the left, label on the right
    const float h = jmin (b.getHeight(), 18.0f);
    auto pill = b.removeFromLeft (h * 1.8f).withSizeKeepingCentre (h * 1.8f, h);
    g.setColour (on ? acc.withAlpha (0.85f) : t.knobTrack);
    g.fillRoundedRectangle (pill, h * 0.5f);
    if (on && t.glow)
    {
        g.setColour (acc.withAlpha (0.2f));
        g.fillRoundedRectangle (pill.expanded (2.5f), h * 0.5f + 2.5f);
    }
    const float knobD = h - 4.0f;
    auto knob = Rectangle<float> (knobD, knobD).withCentre ({ on ? pill.getRight() - h * 0.5f : pill.getX() + h * 0.5f,
                                                              pill.getCentreY() });
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillEllipse (knob.translated (0.0f, 1.0f));
    g.setColour (highlighted ? t.text : t.text.withAlpha (0.9f));
    g.fillEllipse (knob);

    b.removeFromLeft (7.0f);
    g.setColour (on ? t.text : t.textDim);
    g.setFont (Fonts::ui (jlimit (10.0f, 13.0f, h * 0.75f)));
    g.drawFittedText (label.toUpperCase(), b.toNearestInt(), Justification::centredLeft, 1);
}

//==============================================================================
ChoiceBox::ChoiceBox (AudioProcessorValueTreeState& state, const String& id, const String& labelText, Style s)
    : label (labelText), style (s)
{
    choiceParam = dynamic_cast<AudioParameterChoice*> (state.getParameter (id));
    jassert (choiceParam != nullptr); // must be an AudioParameterChoice

    if (choiceParam != nullptr)
    {
        attachment = std::make_unique<ParameterAttachment> (*choiceParam, [this] (float v)
        {
            index = roundToInt (v);
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
        setTooltip (choiceParam->getName (64));
    }
}

ChoiceBox::~ChoiceBox() = default;

String ChoiceBox::nameFor (int i) const
{
    if (isPositiveAndBelow (i, shortNames.size()))
        return shortNames[i];
    if (choiceParam != nullptr && isPositiveAndBelow (i, choiceParam->choices.size()))
        return choiceParam->choices[i];
    return {};
}

Rectangle<float> ChoiceBox::getBoxArea() const
{
    auto b = getLocalBounds().toFloat();
    if (label.isNotEmpty())
        b.removeFromTop (jlimit (12.0f, 16.0f, b.getHeight() * 0.38f));
    return b.reduced (1.0f);
}

void ChoiceBox::setIndex (int newIndex)
{
    if (choiceParam == nullptr || attachment == nullptr)
        return;

    const int n = choiceParam->choices.size();
    newIndex = jlimit (0, n - 1, newIndex);
    if (newIndex != index)
        attachment->setValueAsCompleteGesture ((float) newIndex);
}

void ChoiceBox::paint (Graphics& g)
{
    if (choiceParam == nullptr)
        return;

    const auto& t = themeFor (*this);
    const auto acc = accent.isTransparent() ? t.accent : accent;
    auto b = getLocalBounds().toFloat();

    if (label.isNotEmpty())
    {
        auto labelArea = b.removeFromTop (jlimit (12.0f, 16.0f, b.getHeight() * 0.38f));
        g.setColour (t.textDim);
        g.setFont (Fonts::ui (jlimit (9.0f, 12.0f, labelArea.getHeight() * 0.85f)));
        g.drawText (label.toUpperCase(), labelArea, style == Style::segmented ? Justification::centredLeft : Justification::centred);
    }

    auto box = b.reduced (1.0f);
    const float radius = jmin (8.0f, box.getHeight() * 0.5f);
    const bool hover = isMouseOver (true);

    g.setColour (t.knobTrack);
    g.fillRoundedRectangle (box, radius);
    g.setColour (t.panelOutline);
    g.drawRoundedRectangle (box.reduced (0.5f), radius, 1.0f);

    if (style == Style::segmented)
    {
        const int n = choiceParam->choices.size();
        const float w = box.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto seg = Rectangle<float> (box.getX() + w * (float) i, box.getY(), w, box.getHeight()).reduced (2.0f);
            const bool sel = i == index;
            if (sel)
            {
                g.setColour (acc.withAlpha (0.22f));
                g.fillRoundedRectangle (seg.expanded (1.0f), radius - 1.0f);
                g.setGradientFill (ColourGradient (acc.brighter (0.15f), seg.getX(), seg.getY(), acc.darker (0.25f),
                                                   seg.getX(), seg.getBottom(), false));
                g.fillRoundedRectangle (seg, radius - 2.0f);
            }

            if (segmentPainter)
                segmentPainter (g, seg, i, sel);
            else
            {
                g.setColour (sel ? Colours::black.withAlpha (0.85f) : t.text.withAlpha (hover ? 0.85f : 0.65f));
                g.setFont (Fonts::uiBold (jlimit (9.0f, 13.0f, seg.getHeight() * 0.5f)));
                g.drawFittedText (nameFor (i), seg.toNearestInt(), Justification::centred, 1, 0.7f);
            }
        }
        return;
    }

    // Stepper
    auto arrows = box.reduced (6.0f, 0.0f);
    auto left = arrows.removeFromLeft (10.0f);
    auto right = arrows.removeFromRight (10.0f);
    g.setColour (hover ? acc : t.textDim);

    auto drawArrow = [&] (Rectangle<float> r, bool pointRight)
    {
        auto a = r.withSizeKeepingCentre (5.0f, 8.0f);
        Path p;
        if (pointRight)
        {
            p.startNewSubPath (a.getX(), a.getY());
            p.lineTo (a.getRight(), a.getCentreY());
            p.lineTo (a.getX(), a.getBottom());
        }
        else
        {
            p.startNewSubPath (a.getRight(), a.getY());
            p.lineTo (a.getX(), a.getCentreY());
            p.lineTo (a.getRight(), a.getBottom());
        }
        g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    };
    drawArrow (left, false);
    drawArrow (right, true);

    g.setColour (t.text);
    g.setFont (Fonts::uiBold (jlimit (10.0f, 14.0f, box.getHeight() * 0.5f)));
    g.drawFittedText (nameFor (index), arrows.toNearestInt(), Justification::centred, 1, 0.7f);
}

void ChoiceBox::mouseDown (const MouseEvent& e)
{
    if (choiceParam == nullptr)
        return;

    auto box = getBoxArea();
    const int n = choiceParam->choices.size();

    if (style == Style::segmented)
    {
        const float w = box.getWidth() / (float) n;
        setIndex ((int) ((e.position.x - box.getX()) / w));
        return;
    }

    if (e.position.x < box.getX() + 20.0f)
    {
        setIndex ((index - 1 + n) % n);
        return;
    }
    if (e.position.x > box.getRight() - 20.0f)
    {
        setIndex ((index + 1) % n);
        return;
    }

    PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    for (int i = 0; i < n; ++i)
        menu.addItem (i + 1, choiceParam->choices[i], true, i == index);

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe = Component::SafePointer<ChoiceBox> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->setIndex (result - 1);
                        });
}

void ChoiceBox::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wheel)
{
    if (choiceParam == nullptr)
        return;
    const float d = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (d != 0.0f)
        setIndex (index + (d < 0.0f ? 1 : -1));
}

//==============================================================================
PresetSelector::PresetSelector (PluginBase& p) : processor (p)
{
    processor.presetChanged.addChangeListener (this);
    setTooltip ("Presets: click for the list, arrows to step through");
}

PresetSelector::~PresetSelector()
{
    processor.presetChanged.removeChangeListener (this);
}

void PresetSelector::paint (Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto acc = accent.isTransparent() ? t.accent : accent;
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = b.getHeight() * 0.5f;
    const bool hover = isMouseOver (true);

    g.setColour (t.pill);
    g.fillRoundedRectangle (b, radius);
    g.setColour (hover ? acc.withAlpha (0.6f) : t.panelOutline.withMultipliedAlpha (1.5f));
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);

    auto inner = b.reduced (radius * 0.6f, 0.0f);
    auto left = inner.removeFromLeft (14.0f);
    auto right = inner.removeFromRight (14.0f);

    g.setColour (hover ? acc : t.textDim);
    for (auto [r, pointRight] : { std::pair { left, false }, std::pair { right, true } })
    {
        auto a = r.withSizeKeepingCentre (5.0f, 9.0f);
        Path p;
        p.startNewSubPath (pointRight ? a.getX() : a.getRight(), a.getY());
        p.lineTo (pointRight ? a.getRight() : a.getX(), a.getCentreY());
        p.lineTo (pointRight ? a.getX() : a.getRight(), a.getBottom());
        g.strokePath (p, PathStrokeType (1.7f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    g.setColour (t.text);
    g.setFont (Fonts::uiBold (jlimit (11.0f, 15.0f, b.getHeight() * 0.48f)));
    g.drawFittedText (processor.getCurrentPresetName(), inner.toNearestInt(), Justification::centred, 1, 0.7f);
}

void PresetSelector::mouseDown (const MouseEvent& e)
{
    const float edge = getHeight() * 0.5f + 16.0f;
    if (e.position.x < edge)
        step (-1);
    else if (e.position.x > (float) getWidth() - edge)
        step (1);
    else
        showMenu();
}

void PresetSelector::step (int delta)
{
    const int n = (int) processor.getFactoryPresets().size();
    if (n == 0)
        return;
    int idx = processor.getCurrentFactoryPresetIndex();
    idx = idx < 0 ? 0 : (idx + delta + n) % n;
    processor.loadFactoryPreset (idx);
}

void PresetSelector::showMenu()
{
    PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader ("Factory");

    const auto& presets = processor.getFactoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        menu.addItem (1 + i, presets[(size_t) i].name, true, i == processor.getCurrentFactoryPresetIndex());

    const auto userFiles = processor.getUserPresetFiles();
    if (! userFiles.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Yours");
        for (int i = 0; i < userFiles.size(); ++i)
            menu.addItem (1000 + i, userFiles[i].getFileNameWithoutExtension(), true,
                          processor.getCurrentFactoryPresetIndex() < 0
                              && userFiles[i].getFileNameWithoutExtension() == processor.getCurrentPresetName());
    }

    menu.addSeparator();
    menu.addItem (5000, "Save preset...");
    menu.addItem (5001, "Show preset folder");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe = Component::SafePointer<PresetSelector> (this), userFiles] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;

                            auto& proc = safe->processor;
                            if (result < 1000)
                                proc.loadFactoryPreset (result - 1);
                            else if (result < 5000)
                                proc.loadPresetFile (userFiles[result - 1000]);
                            else if (result == 5000)
                                safe->saveUserPreset();
                            else if (result == 5001)
                            {
                                auto folder = proc.getUserPresetFolder();
                                folder.createDirectory();
                                folder.revealToUser();
                            }
                        });
}

void PresetSelector::saveUserPreset()
{
    auto folder = processor.getUserPresetFolder();
    folder.createDirectory();

    chooser = std::make_unique<FileChooser> ("Save preset", folder.getChildFile ("My Preset.aapreset"), "*.aapreset");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [safe = Component::SafePointer<PresetSelector> (this)] (const FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;
                              auto file = fc.getResult();
                              if (file != File())
                                  safe->processor.savePresetFile (file.withFileExtension ("aapreset"));
                              safe->repaint();
                          });
}

//==============================================================================
void Caption::paint (Graphics& g)
{
    const auto& t = themeFor (*this);
    g.setColour (colour.isTransparent() ? t.textDim : colour);
    g.setFont (isBold ? Fonts::uiBold (fontHeight) : Fonts::ui (fontHeight));
    g.drawFittedText (text, getLocalBounds(), just, 1, 0.8f);
}

//==============================================================================
void layoutRow (Rectangle<int> area, std::initializer_list<Component*> comps, int gap)
{
    const int n = (int) comps.size();
    if (n == 0)
        return;
    const int w = (area.getWidth() - gap * (n - 1)) / n;
    int x = area.getX();
    for (auto* c : comps)
    {
        if (c != nullptr)
            c->setBounds (x, area.getY(), w, area.getHeight());
        x += w + gap;
    }
}

void layoutGrid (Rectangle<int> area, const std::vector<Component*>& comps, int columns, int gapX, int gapY)
{
    if (comps.empty() || columns <= 0)
        return;
    const int rows = ((int) comps.size() + columns - 1) / columns;
    const int w = (area.getWidth() - gapX * (columns - 1)) / columns;
    const int h = (area.getHeight() - gapY * (rows - 1)) / rows;
    for (size_t i = 0; i < comps.size(); ++i)
    {
        const int c = (int) i % columns, r = (int) i / columns;
        if (comps[i] != nullptr)
            comps[i]->setBounds (area.getX() + c * (w + gapX), area.getY() + r * (h + gapY), w, h);
    }
}
} // namespace aa
