#include "Fonts.h"
#include <AAUiFonts.h>

namespace aa
{
struct Fonts::Store
{
    juce::Typeface::Ptr uiRegular, uiBold, display, accent;

    Store()
    {
        uiRegular = juce::Typeface::createSystemTypefaceFor (AAUiFonts::PoppinsMedium_ttf,
                                                             (size_t) AAUiFonts::PoppinsMedium_ttfSize);
        uiBold = juce::Typeface::createSystemTypefaceFor (AAUiFonts::PoppinsBold_ttf,
                                                          (size_t) AAUiFonts::PoppinsBold_ttfSize);
        current() = this;
    }

    ~Store()
    {
        if (current() == this)
            current() = nullptr;
    }

    static Store*& current()
    {
        static Store* instance = nullptr;
        return instance;
    }
};

namespace
{
    juce::Font makeFont (const juce::Typeface::Ptr& face, float height)
    {
        if (face == nullptr)
            return juce::Font (juce::FontOptions (height, juce::Font::bold));

        return juce::Font (juce::FontOptions (face).withHeight (height));
    }

    juce::Typeface::Ptr pick (juce::Typeface::Ptr Fonts::Store::* member, juce::Typeface::Ptr Fonts::Store::* fallback)
    {
        if (auto* s = Fonts::Store::current())
            return (s->*member) != nullptr ? (s->*member) : (s->*fallback);

        return nullptr;
    }
} // namespace

Fonts::Holder::Holder() = default;
Fonts::Holder::~Holder() = default;

void Fonts::setDisplayTypeface (const void* data, size_t numBytes)
{
    if (auto* s = Store::current())
        if (s->display == nullptr && data != nullptr && numBytes > 0)
            s->display = juce::Typeface::createSystemTypefaceFor (data, numBytes);
}

void Fonts::setAccentTypeface (const void* data, size_t numBytes)
{
    if (auto* s = Store::current())
        if (s->accent == nullptr && data != nullptr && numBytes > 0)
            s->accent = juce::Typeface::createSystemTypefaceFor (data, numBytes);
}

juce::Font Fonts::ui (float height)      { return makeFont (pick (&Store::uiRegular, &Store::uiRegular), height); }
juce::Font Fonts::uiBold (float height)  { return makeFont (pick (&Store::uiBold, &Store::uiBold), height); }
juce::Font Fonts::display (float height) { return makeFont (pick (&Store::display, &Store::uiBold), height); }
juce::Font Fonts::accent (float height)  { return makeFont (pick (&Store::accent, &Store::uiBold), height); }

float Fonts::textWidth (const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (font, text);
}
} // namespace aa
