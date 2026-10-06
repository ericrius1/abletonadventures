#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>

namespace aa
{
/** Embedded fonts shared by every plugin (Poppins for UI text), plus one "display" face
    per plugin which gives each product its own personality.

    The typefaces live in a shared, reference-counted store that's kept alive by Fonts::Holder
    objects (every aa::LookAndFeel owns one), so nothing outlives JUCE's own shutdown. */
struct Fonts
{
    struct Store;

    class Holder
    {
    public:
        Holder();
        ~Holder();
    private:
        juce::SharedResourcePointer<Store> store;
    };

    /** Call from the editor constructor with the plugin's display font data. */
    static void setDisplayTypeface (const void* data, size_t numBytes);

    /** Optional second display face (e.g. handwriting on a cassette label). */
    static void setAccentTypeface (const void* data, size_t numBytes);

    static juce::Font ui (float height);
    static juce::Font uiBold (float height);
    static juce::Font display (float height);
    static juce::Font accent (float height);

    static float textWidth (const juce::Font& font, const juce::String& text);
};
} // namespace aa
