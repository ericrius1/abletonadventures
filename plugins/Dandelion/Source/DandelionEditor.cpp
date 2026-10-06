#include "DandelionEditor.h"
#include <PluginAssets.h>

using namespace juce;

namespace
{
    constexpr int baseWidth = 920, baseHeight = 620;

    const Colour night { 0xff101a2e };
    const Colour dusk { 0xff2a1f48 };
    const Colour cream { 0xfffff4dc };
    const Colour gold { 0xffffd25e };
    const Colour peach { 0xffff9a76 };
    const Colour lavender { 0xffb9a7ff };
    const Colour leaf { 0xff6fcf97 };

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = night;
        t.backgroundAlt = dusk;
        t.panel = Colours::white.withAlpha (0.055f);
        t.panelOutline = Colours::white.withAlpha (0.10f);
        t.text = cream;
        t.textDim = cream.withAlpha (0.58f);
        t.accent = gold;
        t.accent2 = peach;
        t.knobBody = Colour (0xff2d3352);
        t.knobTrack = Colours::black.withAlpha (0.38f);
        t.shadow = Colours::black.withAlpha (0.55f);
        t.popupBackground = Colour (0xf61a2038);
        t.pill = Colours::black.withAlpha (0.3f);
        t.cornerRadius = 14.0f;
        return t;
    }

    Colour seedColour (int octave)
    {
        return octave > 0 ? gold : (octave < 0 ? lavender : cream);
    }

    void drawPappus (Graphics& g, Point<float> c, float size, float spin, Colour colour, float alpha)
    {
        // a seed: tiny body, a stalk and a starburst of fluff
        const float stalk = size * 0.9f;
        const Point<float> top = c + Point<float> (std::sin (spin), -std::cos (spin)) * stalk;
        g.setColour (colour.withAlpha (alpha * 0.55f));
        g.drawLine ({ c, top }, jmax (0.6f, size * 0.09f));
        g.setColour (colour.withAlpha (alpha * 0.85f));
        for (int i = 0; i < 7; ++i)
        {
            const float a = spin + (float) i / 7.0f * MathConstants<float>::twoPi;
            g.drawLine ({ top, top + Point<float> (std::sin (a), -std::cos (a)) * size * 0.55f }, jmax (0.5f, size * 0.06f));
        }
        g.setColour (colour.withAlpha (alpha * 0.25f));
        g.fillEllipse (Rectangle<float> (size * 1.4f, size * 1.4f).withCentre (top));
        g.setColour (Colour (0xffc9a66b).withAlpha (alpha));
        g.fillEllipse (Rectangle<float> (size * 0.28f, size * 0.42f).withCentre (c));
    }
} // namespace

//==============================================================================
MeadowScene::MeadowScene (DandelionProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (false, false);
    Random r (11);
    for (int i = 0; i < 70; ++i)
        stars.push_back ({ r.nextFloat(), r.nextFloat() * 0.6f });
}

Point<float> MeadowScene::headCentre() const
{
    const auto b = getLocalBounds().toFloat();
    return { b.getCentreX() - 8.0f + sway * 16.0f, b.getHeight() * 0.36f + std::abs (sway) * 3.0f };
}

void MeadowScene::addSeed (const DandelionProcessor::GrainEvent& e)
{
    if (seeds.size() > 220)
        seeds.erase (seeds.begin(), seeds.begin() + 20);

    const auto c = headCentre();
    // grains from early in the sample leave from the left of the head, late ones from the right
    const float a = (e.position * 1.6f - 0.8f) * MathConstants<float>::pi * 0.75f + random.nextFloat() * 0.5f - 0.25f;
    const float r = 50.0f * headScale;
    Seed s;
    s.pos = c + Point<float> (std::sin (a), -std::cos (a)) * r;
    s.vel = { 14.0f + e.pan * 18.0f + random.nextFloat() * 10.0f, -18.0f - random.nextFloat() * 22.0f };
    s.size = jlimit (4.0f, 11.0f, 5.0f + e.amp * 14.0f);
    s.maxLife = s.life = 2.5f + random.nextFloat() * 2.5f;
    s.spin = random.nextFloat() * MathConstants<float>::twoPi;
    s.spinRate = (random.nextFloat() - 0.5f) * 1.5f;
    s.octave = e.octave;
    seeds.push_back (s);
    glow = jmin (1.0f, glow + 0.06f);
}

void MeadowScene::tick (double dt)
{
    const float d = (float) dt;
    time += d;
    const float windValue = processor.windNow.load();
    const float windParam = processor.apvts.getRawParameterValue ("wind")->load() / 100.0f;

    // the stem is a damped spring pushed around by the wind
    const float target = windValue * 0.9f + std::sin (time * 0.7f) * 0.08f * (0.3f + windParam);
    swayVel += ((target - sway) * 9.0f - swayVel * 2.5f) * d;
    sway += swayVel * d;
    glow = jmax (0.0f, glow - d * 0.6f);

    for (auto& s : seeds)
    {
        s.vel.x += (windValue * 60.0f + 6.0f) * d;
        s.vel.y += (-6.0f + std::sin (time * 1.3f + s.spin) * 10.0f) * d;
        s.vel *= 1.0f - 0.25f * d;
        s.pos += s.vel * d;
        s.spin += s.spinRate * d;
        s.life -= d;
    }
    seeds.erase (std::remove_if (seeds.begin(), seeds.end(), [this] (const Seed& s)
                                 { return s.life <= 0.0f || s.pos.x > (float) getWidth() + 20.0f || s.pos.y < -20.0f; }),
                 seeds.end());
    repaint();
}

void MeadowScene::renderBackdrop (float scale)
{
    const int w = jmax (1, roundToInt ((float) getWidth() * scale)), h = jmax (1, roundToInt ((float) getHeight() * scale));
    backdrop = Image (Image::ARGB, w, h, true);
    Graphics g (backdrop);
    g.addTransform (AffineTransform::scale (scale));
    auto b = getLocalBounds().toFloat();

    Path clip;
    clip.addRoundedRectangle (b, 16.0f);
    g.reduceClipRegion (clip);

    ColourGradient sky (Colour (0xff0c1730), 0.0f, 0.0f, Colour (0xffe58b5c), 0.0f, b.getHeight() * 0.86f, false);
    sky.addColour (0.45, Colour (0xff2b2152));
    sky.addColour (0.72, Colour (0xff7a3f6e));
    g.setGradientFill (sky);
    g.fillRect (b);

    // moon
    const Point<float> moon (b.getRight() - 58.0f, 62.0f);
    for (int i = 5; i > 0; --i)
    {
        g.setColour (Colour (0xfffff1c9).withAlpha (0.05f));
        g.fillEllipse (Rectangle<float> (34.0f + (float) i * 18.0f, 34.0f + (float) i * 18.0f).withCentre (moon));
    }
    g.setColour (Colour (0xfffff3d6));
    g.fillEllipse (Rectangle<float> (34.0f, 34.0f).withCentre (moon));
    g.setColour (Colour (0xffe9d9b4).withAlpha (0.6f));
    g.fillEllipse (Rectangle<float> (7.0f, 6.0f).withCentre (moon.translated (-6.0f, -4.0f)));
    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (moon.translated (6.0f, 7.0f)));

    // far hills
    auto hills = [&] (float baseY, float amp, float freq, float phase, Colour c)
    {
        Path p;
        p.startNewSubPath (0.0f, b.getBottom());
        for (float x = 0.0f; x <= b.getWidth() + 8.0f; x += 8.0f)
            p.lineTo (x, baseY - amp * (0.6f * std::sin (x * freq + phase) + 0.4f * std::sin (x * freq * 2.3f + phase * 1.7f)));
        p.lineTo (b.getWidth(), b.getBottom());
        p.closeSubPath();
        g.setColour (c);
        g.fillPath (p);
    };
    hills (b.getHeight() * 0.80f, 18.0f, 0.018f, 0.4f, Colour (0xff3a2a55).withAlpha (0.9f));
    hills (b.getHeight() * 0.86f, 12.0f, 0.027f, 2.0f, Colour (0xff1d2440));

    // ground
    g.setGradientFill (ColourGradient (Colour (0xff12261f), 0.0f, b.getHeight() * 0.88f, Colour (0xff08130f), 0.0f, b.getBottom(), false));
    g.fillRect (b.withTop (b.getHeight() * 0.9f));
}

void MeadowScene::renderHead (float scale)
{
    headScale = 1.0f;
    const float r = 60.0f;
    const int px = roundToInt (r * 2.6f * scale);
    head = Image (Image::ARGB, px, px, true);
    Graphics g (head);
    g.addTransform (AffineTransform::scale (scale));
    const Point<float> c (r * 1.3f, r * 1.3f);

    // soft halo
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), c.x, c.y, Colours::transparentWhite, c.x + r * 1.25f, c.y, true));
    g.fillEllipse (Rectangle<float> (r * 2.5f, r * 2.5f).withCentre (c));

    Random rnd (5);
    // seeds on a sphere: golden-angle spiral gives an even, natural look
    const int n = 110;
    for (int i = 0; i < n; ++i)
    {
        const float t = ((float) i + 0.5f) / (float) n;
        const float a = (float) i * 2.39996f;
        const float radial = std::sqrt (t);
        const Point<float> dir (std::cos (a), std::sin (a));
        const float len = r * (0.35f + 0.65f * radial) * (0.92f + 0.08f * rnd.nextFloat());
        const auto tip = c + dir * len;
        g.setColour (Colours::white.withAlpha (0.18f + 0.2f * radial));
        g.drawLine ({ c + dir * 6.0f, tip }, 0.7f);
        for (int k = 0; k < 6; ++k)
        {
            const float b = a + ((float) k - 2.5f) * 0.28f;
            g.setColour (Colours::white.withAlpha (0.35f + 0.35f * radial));
            g.drawLine ({ tip, tip + Point<float> (std::cos (b), std::sin (b)) * (7.0f + 3.0f * radial) }, 0.7f);
        }
    }
    g.setGradientFill (ColourGradient (Colour (0xffe8cf96), c.x - 3.0f, c.y - 3.0f, Colour (0xff9c7c4a), c.x + 6.0f, c.y + 6.0f, true));
    g.fillEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (c));
}

void MeadowScene::paint (Graphics& g)
{
    const float physScale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (backdrop.isNull() || std::abs ((float) backdrop.getWidth() - (float) getWidth() * physScale) > 2.0f)
    {
        renderBackdrop (physScale);
        renderHead (physScale);
    }

    auto b = getLocalBounds().toFloat();
    g.drawImage (backdrop, b);

    Path clip;
    clip.addRoundedRectangle (b, 16.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    // twinkling stars
    for (size_t i = 0; i < stars.size(); ++i)
    {
        const float tw = 0.35f + 0.35f * std::sin (time * (0.8f + (float) (i % 5) * 0.37f) + (float) i);
        g.setColour (Colours::white.withAlpha (tw * (1.0f - stars[i].y)));
        const float s = (i % 7 == 0) ? 2.2f : 1.3f;
        g.fillEllipse (stars[i].x * b.getWidth(), stars[i].y * b.getHeight(), s, s);
    }

    // stem: a curve from the ground up to the swaying head
    const auto hc = headCentre();
    const Point<float> base (b.getCentreX() - 18.0f, b.getBottom() - 30.0f);
    Path stem;
    stem.startNewSubPath (base);
    stem.cubicTo (base.translated (6.0f, -110.0f), hc.translated (-sway * 30.0f - 10.0f, 120.0f), hc.translated (0.0f, 10.0f));
    g.setColour (Colour (0xff3f7a55));
    g.strokePath (stem, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (leaf.withAlpha (0.45f));
    g.strokePath (stem, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));

    // a couple of leaves
    auto leafAt = [&] (Point<float> p, float angle, float len)
    {
        Path l;
        l.startNewSubPath (0.0f, 0.0f);
        l.quadraticTo (len * 0.5f, -len * 0.28f, len, 0.0f);
        l.quadraticTo (len * 0.5f, len * 0.18f, 0.0f, 0.0f);
        l.applyTransform (AffineTransform::rotation (angle + sway * 0.15f).translated (p));
        g.setColour (Colour (0xff2f6b48));
        g.fillPath (l);
    };
    leafAt (base.translated (0.0f, -6.0f), -2.5f, 54.0f);
    leafAt (base.translated (2.0f, -4.0f), -0.55f, 62.0f);
    leafAt (base.translated (-1.0f, -2.0f), -1.9f, 40.0f);

    // seed head (cached), rotating slightly with the wind
    if (head.isValid())
    {
        const float hw = (float) head.getWidth() / physScale, hh = (float) head.getHeight() / physScale;
        const float pulse = 1.0f + glow * 0.04f;
        g.drawImageTransformed (head, AffineTransform::scale (1.0f / physScale)
                                          .translated (-hw * 0.5f, -hh * 0.5f)
                                          .scaled (pulse)
                                          .rotated (sway * 0.25f)
                                          .translated (hc));
    }

    // floating seeds
    for (auto& s : seeds)
    {
        const float alpha = jlimit (0.0f, 1.0f, s.life / s.maxLife * 1.6f) * jlimit (0.0f, 1.0f, (s.maxLife - s.life) * 6.0f);
        drawPappus (g, s.pos, s.size, s.spin, seedColour (s.octave), alpha);
    }

    // grass blades in front
    Random gr (3);
    for (int i = 0; i < 46; ++i)
    {
        const float x = gr.nextFloat() * b.getWidth();
        const float h = 18.0f + gr.nextFloat() * 34.0f;
        const float lean = (gr.nextFloat() - 0.5f) * 10.0f + sway * 14.0f * (h / 50.0f) + std::sin (time * 1.5f + x * 0.05f) * 1.5f;
        Path blade;
        blade.startNewSubPath (x - 2.5f, b.getBottom());
        blade.quadraticTo (x + lean * 0.3f, b.getBottom() - h * 0.6f, x + lean, b.getBottom() - h);
        blade.quadraticTo (x + lean * 0.3f + 1.0f, b.getBottom() - h * 0.55f, x + 2.5f, b.getBottom());
        blade.closeSubPath();
        g.setColour ((i % 3 == 0 ? Colour (0xff1f4a35) : Colour (0xff163828)).withAlpha (0.95f));
        g.fillPath (blade);
    }

    g.restoreState();
    g.setColour (Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (b.reduced (0.5f), 16.0f, 1.0f);
}

//==============================================================================
WaveView::WaveView (DandelionProcessor& p) : processor (p)
{
    setTooltip ("The current source. Click or drag to set the grain position. Drop any audio file to plant it.");
    if (auto* param = processor.apvts.getParameter ("position"))
    {
        positionAttachment = std::make_unique<ParameterAttachment> (*param, [this] (float v) { positionValue = v; repaint(); },
                                                                    processor.apvts.undoManager);
        positionAttachment->sendInitialUpdate();
    }
}

void WaveView::setPositionFromX (float x)
{
    auto area = getLocalBounds().toFloat().reduced (12.0f, 0.0f);
    const float v = jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth()) * 100.0f;
    positionAttachment->setValueAsPartOfGesture (v);
}

void WaveView::mouseDown (const MouseEvent& e)
{
    positionAttachment->beginGesture();
    setPositionFromX (e.position.x);
}

void WaveView::mouseDrag (const MouseEvent& e) { setPositionFromX (e.position.x); }
void WaveView::mouseUp (const MouseEvent&) { positionAttachment->endGesture(); }

void WaveView::addSpark (const DandelionProcessor::GrainEvent& e)
{
    if (sparks.size() > 300)
        sparks.erase (sparks.begin(), sparks.begin() + 30);
    sparks.push_back ({ e.position, 0.2f + random.nextFloat() * 0.6f, 1.0f, (float) e.octave });
}

void WaveView::tick (double dt)
{
    for (auto& s : sparks)
        s.life -= (float) dt * 1.8f;
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0.0f; }), sparks.end());
    repaint();
}

void WaveView::rebuildWave (float scale)
{
    const auto* src = processor.getDisplaySource();
    builtFor = src;
    builtVersion = processor.sourceVersion.load();

    auto area = getLocalBounds().toFloat().reduced (12.0f, 26.0f).withTrimmedBottom (-12.0f);
    const int w = jmax (1, roundToInt ((float) getWidth() * scale)), h = jmax (1, roundToInt ((float) getHeight() * scale));
    waveImage = Image (Image::ARGB, w, h, true);
    if (src == nullptr)
        return;

    Graphics g (waveImage);
    g.addTransform (AffineTransform::scale (scale));

    const auto& buf = src->buffer;
    const int n = buf.getNumSamples();
    const int columns = jmax (1, (int) area.getWidth());
    Path top, bottom;
    const float mid = area.getCentreY(), halfH = area.getHeight() * 0.5f;
    Path wavePath;
    wavePath.startNewSubPath (area.getX(), mid);
    std::vector<float> peaks ((size_t) columns);
    for (int c = 0; c < columns; ++c)
    {
        const int s0 = (int) ((int64) c * n / columns), s1 = jmax (s0 + 1, (int) ((int64) (c + 1) * n / columns));
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            peak = jmax (peak, buf.getMagnitude (ch, s0, s1 - s0));
        peaks[(size_t) c] = std::sqrt (jmin (1.0f, peak)); // perceptual-ish
    }
    for (int c = 0; c < columns; ++c)
        wavePath.lineTo (area.getX() + (float) c, mid - peaks[(size_t) c] * halfH);
    for (int c = columns - 1; c >= 0; --c)
        wavePath.lineTo (area.getX() + (float) c, mid + peaks[(size_t) c] * halfH);
    wavePath.closeSubPath();

    g.setGradientFill (ColourGradient (gold.withAlpha (0.85f), 0.0f, area.getY(), peach.withAlpha (0.55f), 0.0f, area.getBottom(), false));
    g.fillPath (wavePath);
    g.setColour (cream.withAlpha (0.35f));
    g.strokePath (wavePath, PathStrokeType (0.8f));
}

void WaveView::paint (Graphics& g)
{
    const float physScale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (waveImage.isNull() || builtVersion != processor.sourceVersion.load() || builtFor != processor.getDisplaySource()
        || std::abs ((float) waveImage.getWidth() - (float) getWidth() * physScale) > 2.0f)
        rebuildWave (physScale);

    auto b = getLocalBounds().toFloat();
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (b, 14.0f);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (b.reduced (0.5f), 14.0f, 1.0f);

    auto area = b.reduced (12.0f, 26.0f).withTrimmedBottom (-12.0f);
    const auto* src = processor.getDisplaySource();

    // spray band around the position
    const float pos = positionValue / 100.0f;
    const float sprayAmt = processor.apvts.getRawParameterValue ("spray")->load() / 100.0f;
    const float px = area.getX() + pos * area.getWidth();
    const float halfBand = sprayAmt * 0.5f * area.getWidth();
    g.setColour (gold.withAlpha (0.10f));
    g.fillRect (Rectangle<float> (px - halfBand, area.getY(), halfBand * 2.0f, area.getHeight()).getIntersection (area));
    if (px - halfBand < area.getX())
        g.fillRect (Rectangle<float> (area.getRight() - (area.getX() - (px - halfBand)), area.getY(), area.getX() - (px - halfBand), area.getHeight()));
    if (px + halfBand > area.getRight())
        g.fillRect (Rectangle<float> (area.getX(), area.getY(), px + halfBand - area.getRight(), area.getHeight()));

    g.setOpacity (1.0f); // drawImage uses the current colour's alpha
    g.drawImage (waveImage, b);

    // grain sparks
    for (auto& s : sparks)
    {
        const auto c = s.octave > 0 ? gold : (s.octave < 0 ? lavender : cream);
        const Point<float> p (area.getX() + s.x * area.getWidth(), area.getY() + s.y * area.getHeight());
        g.setColour (c.withAlpha (s.life * 0.25f));
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (p));
        g.setColour (c.withAlpha (s.life));
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (p));
    }

    // position marker
    g.setColour (gold);
    g.fillRect (Rectangle<float> (px - 1.0f, area.getY() - 4.0f, 2.0f, area.getHeight() + 8.0f));
    Path tri;
    tri.addTriangle (px - 6.0f, area.getY() - 10.0f, px + 6.0f, area.getY() - 10.0f, px, area.getY() - 3.0f);
    g.fillPath (tri);

    // voice playheads
    for (auto& vp : processor.voicePosition)
    {
        const float v = vp.load();
        if (v < 0.0f)
            continue;
        const float x = area.getX() + v * area.getWidth();
        g.setColour (cream.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (cream);
        g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ x, area.getBottom() + 5.0f }));
    }

    // labels
    g.setFont (aa::Fonts::uiBold (12.0f));
    g.setColour (cream.withAlpha (0.85f));
    const String name = src != nullptr ? src->name : String ("growing the seeds...");
    g.drawText (name.toUpperCase(), b.reduced (14.0f, 6.0f).removeFromTop (16.0f), Justification::centredLeft);
    if (src != nullptr)
    {
        g.setFont (aa::Fonts::ui (11.0f));
        g.setColour (cream.withAlpha (0.5f));
        g.drawText (String (src->buffer.getNumSamples() / src->sampleRate, 1) + " s  -  drop any audio file to plant it",
                    b.reduced (14.0f, 6.0f).removeFromTop (16.0f), Justification::centredRight);
    }
}

//==============================================================================
NoteBox::NoteBox (AudioProcessorValueTreeState& state, const String& paramID)
{
    param = state.getParameter (paramID);
    if (param != nullptr)
    {
        attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v) { value = roundToInt (v); repaint(); },
                                                            state.undoManager);
        attachment->sendInitialUpdate();
    }
    setTooltip ("Root key of your sample: the note at which it plays back at its original pitch. Drag or scroll.");
}

void NoteBox::paint (Graphics& g)
{
    const auto& t = aa::themeFor (*this);
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (t.pill);
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (isMouseOver() ? t.accent.withAlpha (0.6f) : t.panelOutline.withMultipliedAlpha (1.5f));
    g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
    g.setFont (aa::Fonts::ui (10.5f));
    g.setColour (t.textDim);
    auto r = b.reduced (12.0f, 0.0f);
    g.drawText ("ROOT", r.removeFromLeft (38.0f), Justification::centredLeft);
    g.setFont (aa::Fonts::uiBold (13.0f));
    g.setColour (t.text);
    g.drawText (MidiMessage::getMidiNoteName (value, true, true, 3), r, Justification::centredRight);
}

void NoteBox::mouseDown (const MouseEvent&)
{
    dragStartValue = value;
    if (attachment != nullptr)
        attachment->beginGesture();
}

void NoteBox::mouseDrag (const MouseEvent& e)
{
    if (attachment != nullptr)
        attachment->setValueAsPartOfGesture ((float) jlimit (24, 96, dragStartValue - e.getDistanceFromDragStartY() / 6));
}

void NoteBox::mouseUp (const MouseEvent&)
{
    if (attachment != nullptr)
        attachment->endGesture();
}

void NoteBox::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (attachment != nullptr && w.deltaY != 0.0f)
        attachment->setValueAsCompleteGesture ((float) jlimit (24, 96, value + (w.deltaY > 0 ? 1 : -1)));
}

void PillButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    const auto& t = aa::themeFor (*this);
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? t.accent.withAlpha (0.35f) : (highlighted ? t.accent.withAlpha (0.18f) : t.pill));
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (highlighted ? t.accent : t.panelOutline.withMultipliedAlpha (1.5f));
    g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
    g.setColour (t.text);
    g.setFont (aa::Fonts::uiBold (12.0f));
    g.drawText (getButtonText(), b, Justification::centred);
}

//==============================================================================
DandelionEditor::DandelionEditor (DandelionProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      position (p.apvts, "position", "Position"),
      spray (p.apvts, "spray", "Spray"),
      size (p.apvts, "size", "Size"),
      density (p.apvts, "density", "Density"),
      drift (p.apvts, "drift", "Drift"),
      wind (p.apvts, "wind", "Wind"),
      seeds (p.apvts, "seeds", "Seeds"),
      reverse (p.apvts, "reverse", "Reverse"),
      shapeKnob (p.apvts, "shape", "Shape"),
      shimmer (p.apvts, "shimmer", "Shimmer"),
      cutoff (p.apvts, "cutoff", "Cutoff"),
      width (p.apvts, "width", "Width"),
      attack (p.apvts, "attack", "Attack"),
      release (p.apvts, "release", "Release"),
      reverb (p.apvts, "reverb", "Reverb"),
      volume (p.apvts, "volume", "Volume"),
      source (p.apvts, "source", ""),
      root (p.apvts, "root"),
      presets (p),
      keyboard (p.keyboardState, MidiKeyboardComponent::horizontalKeyboard)
{
    aa::Fonts::setDisplayTypeface (PluginAssets::SnigletExtraBold_ttf, (size_t) PluginAssets::SnigletExtraBold_ttfSize);

    drift.setBipolar (true);
    for (auto* k : { &seeds, &shimmer })
        k->setAccent (gold);
    for (auto* k : { &wind, &drift, &width })
        k->setAccent (Colour (0xff8fd3ff));
    for (auto* k : { &reverse, &reverb })
        k->setAccent (lavender);
    for (auto* k : { &attack, &release, &volume, &cutoff })
        k->setAccent (peach);
    presets.setAccent (gold);

    position.setTooltip ("Where in the sound the grains are picked from (you can also click the waveform)");
    spray.setTooltip ("Random scatter around the position");
    size.setTooltip ("Length of each grain");
    density.setTooltip ("How many grains per second each note blows off");
    drift.setTooltip ("Moves the position through the sound while a note plays (100% = original speed, negative = backwards)");
    wind.setTooltip ("Gusts that push density, scatter and panning around. The mod wheel adds wind too");
    seeds.setTooltip ("Chance that a grain jumps an octave (gold seeds up, lavender seeds down)");
    reverse.setTooltip ("Chance that a grain plays backwards");
    shapeKnob.setTooltip ("Grain envelope: soft and smooth to plucky and percussive");
    shimmer.setTooltip ("Random detune per grain - a chorus of tiny voices");
    cutoff.setTooltip ("Low-pass filter");
    width.setTooltip ("Stereo scatter of the grains");
    attack.setTooltip ("Note fade-in");
    release.setTooltip ("Note fade-out after you let go");
    reverb.setTooltip ("Meadow-at-night reverb");
    volume.setTooltip ("Output level");
    source.setTooltip ("Built-in sources, or 'Your Sample' after you drop one in");
    loadButton.setTooltip ("Choose an audio file to plant (or just drag one onto the plugin)");
    loadButton.onClick = [this] { chooseFile(); };

    keyboard.setAvailableRange (36, 96);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xfff3e8d2));
    keyboard.setColour (MidiKeyboardComponent::blackNoteColourId, Colour (0xff1c2138));
    keyboard.setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colour (0x55303651));
    keyboard.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, gold.withAlpha (0.3f));
    keyboard.setColour (MidiKeyboardComponent::keyDownOverlayColourId, gold.withAlpha (0.85f));
    keyboard.setColour (MidiKeyboardComponent::shadowColourId, Colours::black.withAlpha (0.25f));
    keyboard.setColour (MidiKeyboardComponent::textLabelColourId, Colour (0x99303651));

    for (auto* c : std::initializer_list<Component*> { &scene, &wave, &position, &spray, &size, &density, &drift, &wind,
                                                       &seeds, &reverse, &shapeKnob, &shimmer, &cutoff, &width, &attack,
                                                       &release, &reverb, &volume, &source, &root, &loadButton, &presets,
                                                       &keyboard })
        content.addAndMakeVisible (c);

    finishSetup();
}

bool DandelionEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (proc.formatManager.findFormatForFileExtension (File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void DandelionEditor::filesDropped (const StringArray& files, int, int)
{
    dragHover = false;
    for (auto& f : files)
        if (proc.formatManager.findFormatForFileExtension (File (f).getFileExtension()) != nullptr)
        {
            loadFile (File (f));
            break;
        }
    content.repaint();
}

void DandelionEditor::loadFile (const File& f)
{
    String error;
    if (proc.loadSampleFile (f, error))
        statusText = "Planted \"" + f.getFileNameWithoutExtension() + "\"";
    else
        statusText = error;
    statusTimer = 3.0f;
}

void DandelionEditor::chooseFile()
{
    chooser = std::make_unique<FileChooser> ("Plant a sample", File::getSpecialLocation (File::userMusicDirectory),
                                             proc.formatManager.getWildcardForAllFormats());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [safe = Component::SafePointer<DandelionEditor> (this)] (const FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->loadFile (fc.getResult());
                          });
}

void DandelionEditor::layoutContent()
{
    presets.setBounds (Rectangle<int> (250, 34).withCentre ({ baseWidth / 2 + 40, 33 }));

    scene.setBounds (16, 66, 300, 472);

    const int rx = 328, rw = baseWidth - rx - 16;
    source.setBounds (rx, 68, 200, 30);
    root.setBounds (rx + 208, 68, 104, 30);
    loadButton.setBounds (rx + rw - 132, 68, 132, 30);
    wave.setBounds (rx, 104, rw, 132);

    const float pw = ((float) rw - 8.0f) * 0.5f;
    panels[0] = { (float) rx, 246.0f, pw, 142.0f };
    panels[1] = { (float) rx + pw + 8.0f, 246.0f, pw, 142.0f };
    panels[2] = { (float) rx, 396.0f, pw, 142.0f };
    panels[3] = { (float) rx + pw + 8.0f, 396.0f, pw, 142.0f };

    auto row = [] (Rectangle<float> panel, std::initializer_list<Component*> knobs)
    {
        aa::layoutRow (panel.reduced (8.0f, 10.0f).withTrimmedTop (20.0f).toNearestInt(), knobs, 2);
    };
    row (panels[0], { &position, &spray, &size, &density });
    row (panels[1], { &drift, &wind, &seeds, &reverse });
    row (panels[2], { &shapeKnob, &shimmer, &cutoff, &width });
    row (panels[3], { &attack, &release, &reverb, &volume });

    keyboard.setBounds (16, 548, baseWidth - 32, 58);
    keyboard.setKeyWidth ((float) (baseWidth - 32) / 36.0f);
}

void DandelionEditor::paintContent (Graphics& g)
{
    auto b = baseBounds().toFloat();
    g.setGradientFill (ColourGradient (night, 0.0f, 0.0f, dusk, 0.0f, b.getBottom(), false));
    g.fillAll();

    // faint fireflies in the background
    Random r (8);
    for (int i = 0; i < 40; ++i)
    {
        g.setColour (gold.withAlpha (0.05f + 0.06f * r.nextFloat()));
        const float s = 2.0f + r.nextFloat() * 3.0f;
        g.fillEllipse (r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight(), s, s);
    }

    // title
    g.setColour (cream);
    g.setFont (aa::Fonts::display (38.0f));
    g.drawText ("Dandelion", Rectangle<float> (22.0f, 6.0f, 260.0f, 50.0f), Justification::centredLeft);
    const float tw = aa::Fonts::textWidth (aa::Fonts::display (38.0f), "Dandelion");
    drawPappus (g, { 22.0f + tw + 14.0f, 40.0f }, 10.0f, 0.4f, gold, 0.95f);
    g.setColour (theme().textDim);
    g.setFont (aa::Fonts::ui (11.0f).withExtraKerningFactor (0.1f));
    g.drawText ("GRANULAR SEED SAMPLER", Rectangle<float> (24.0f, 44.0f, 260.0f, 16.0f), Justification::centredLeft);

    g.setFont (aa::Fonts::uiBold (11.0f).withExtraKerningFactor (0.2f));
    g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 230.0f, 18.0f, 210.0f, 30.0f), Justification::centredRight);

    const char* titles[] = { "Grains", "Motion", "Colour", "Voice" };
    for (size_t i = 0; i < panels.size(); ++i)
        lnf().drawPanel (g, panels[i], titles[i]);
}

void DandelionEditor::paintContentOver (Graphics& g)
{
    if (statusTimer > 0.0f && statusText.isNotEmpty())
    {
        auto r = Rectangle<float> (360.0f, 30.0f).withCentre ({ wave.getBounds().toFloat().getCentreX(), (float) wave.getBottom() - 22.0f });
        g.setColour (Colours::black.withAlpha (0.6f * jmin (1.0f, statusTimer)));
        g.fillRoundedRectangle (r, 15.0f);
        g.setColour (gold.withAlpha (jmin (1.0f, statusTimer)));
        g.setFont (aa::Fonts::uiBold (13.0f));
        g.drawText (statusText, r, Justification::centred);
    }

    if (dragHover)
    {
        auto b = baseBounds().toFloat().reduced (8.0f);
        g.setColour (night.withAlpha (0.72f));
        g.fillRoundedRectangle (b, 18.0f);
        g.setColour (gold);
        const float dashes[] = { 10.0f, 7.0f };
        Path outline;
        outline.addRoundedRectangle (b.reduced (6.0f), 16.0f);
        Path dashed;
        PathStrokeType (2.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.fillPath (dashed);
        g.setFont (aa::Fonts::display (40.0f));
        g.drawText ("Drop to plant this sound", b, Justification::centred);
    }
}

void DandelionEditor::onFrame (double, double dt)
{
    DandelionProcessor::GrainEvent e;
    int count = 0;
    while (proc.grainEvents.pop (e))
    {
        // show at most a few seeds per frame so dense clouds stay readable
        if (count++ < 6)
            scene.addSeed (e);
        wave.addSpark (e);
    }
    scene.tick (dt);
    wave.tick (dt);

    if (statusTimer > 0.0f)
    {
        statusTimer -= (float) dt;
        content.repaint (wave.getBounds().expanded (4));
    }
}
