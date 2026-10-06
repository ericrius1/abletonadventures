#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include "PluginBase.h"

namespace aa
{
/** Base editor for every plugin in the pack.

    - Designs are authored at a fixed "base" size; the whole UI scales smoothly (vector) when the
      window is resized via the corner handle. The chosen zoom is remembered in the plugin state.
    - Add child components to `content` and lay them out in base coordinates in layoutContent().
    - onFrame() is called on every display refresh for animations. */
class EditorBase : public juce::AudioProcessorEditor
{
public:
    EditorBase (PluginBase& processor, int baseWidth, int baseHeight, const Theme& theme);
    ~EditorBase() override;

    void resized() override;
    void paint (juce::Graphics&) override;

protected:
    /** Paint the background of the design (base coordinates). */
    virtual void paintContent (juce::Graphics&) {}
    /** Paint on top of child components (base coordinates). */
    virtual void paintContentOver (juce::Graphics&) {}
    /** Position child components (base coordinates). */
    virtual void layoutContent() {}
    /** Animation tick, called at display refresh rate. */
    virtual void onFrame (double /*timeSeconds*/, double /*deltaSeconds*/) {}

    LookAndFeel& lnf() noexcept { return *lookAndFeel; }
    const Theme& theme() const noexcept { return lookAndFeel->theme(); }
    juce::Rectangle<int> baseBounds() const noexcept { return { 0, 0, baseW, baseH }; }

    /** Current zoom of the design (1 = base size). */
    float getZoom() const noexcept { return zoom; }

    /** Standard header: title on the left, preset selector centre, brand on the right. */
    void paintStandardHeader (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title,
                              const juce::String& subtitle, juce::Colour titleColour);

    PluginBase& pluginBase;
    std::unique_ptr<LookAndFeel> lookAndFeel; // first so it outlives children

    class Content : public juce::Component
    {
    public:
        explicit Content (EditorBase& o) : owner (o) { setOpaque (true); }
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void paintOverChildren (juce::Graphics& g) override { owner.paintContentOver (g); }
        void resized() override { owner.layoutContent(); }
    private:
        EditorBase& owner;
    };

    Content content { *this };
    juce::TooltipWindow tooltips { this, 700 };

    /** Call at the end of the subclass constructor once children are created. */
    void finishSetup();

    /** Swap in a plugin-specific look & feel subclass (call first thing in the subclass constructor). */
    void useLookAndFeel (std::unique_ptr<LookAndFeel> newLnf);

private:
    int baseW, baseH;
    float zoom = 1.0f;
    double lastFrameTime = 0.0;
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorBase)
};

/** Replace the editor's look & feel with a plugin-specific subclass. */
template <typename LnF>
std::unique_ptr<LookAndFeel> makeLookAndFeel (const Theme& t) { return std::make_unique<LnF> (t); }
} // namespace aa
