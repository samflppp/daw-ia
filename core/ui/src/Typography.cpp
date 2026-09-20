#include "daw/ui/Typography.h"

#include <DawFontsData.h>

namespace daw::ui
{
namespace
{

juce::Typeface::Ptr load(const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor(data, static_cast<std::size_t>(size));
}

} // namespace

Typography::Typography(const Tokens& tokens)
    : tokens_(tokens)
    , sansRegular_(load(DawFontsData::InterRegular_ttf, DawFontsData::InterRegular_ttfSize))
    , sansMedium_(load(DawFontsData::InterMedium_ttf, DawFontsData::InterMedium_ttfSize))
    , sansSemiBold_(load(DawFontsData::InterSemiBold_ttf, DawFontsData::InterSemiBold_ttfSize))
    , monoRegular_(load(DawFontsData::JetBrainsMonoRegular_ttf, DawFontsData::JetBrainsMonoRegular_ttfSize))
    , monoMedium_(load(DawFontsData::JetBrainsMonoMedium_ttf, DawFontsData::JetBrainsMonoMedium_ttfSize))
{
}

juce::Font Typography::build(const juce::Typeface::Ptr& typeface,
                             juce::StringRef sizePath,
                             juce::StringRef trackingPath) const
{
    juce::Font font{juce::FontOptions{typeface}.withHeight(tokens_.number(sizePath))};
    font.setExtraKerningFactor(tokens_.number(trackingPath));
    return font;
}

const juce::Typeface::Ptr& Typography::sansFor(juce::StringRef weightPath) const
{
    const auto weight = tokens_.number(weightPath);
    if (weight >= tokens_.number("font.weight.semibold"))
        return sansSemiBold_;
    if (weight >= tokens_.number("font.weight.medium"))
        return sansMedium_;
    return sansRegular_;
}

const juce::Typeface::Ptr& Typography::monoFor(juce::StringRef weightPath) const
{
    return tokens_.number(weightPath) >= tokens_.number("font.weight.medium") ? monoMedium_ : monoRegular_;
}

juce::Font Typography::sans(juce::StringRef sizePath, juce::StringRef weightPath) const
{
    return build(sansFor(weightPath), sizePath, "font.tracking.normal");
}

juce::Font Typography::mono(juce::StringRef sizePath, juce::StringRef weightPath) const
{
    // Values are read, not skimmed: the mono face keeps its own spacing.
    return build(monoFor(weightPath), sizePath, "font.tracking.zero");
}

juce::Font Typography::caps(juce::StringRef sizePath) const
{
    return build(sansSemiBold_, sizePath, "font.tracking.caps");
}

} // namespace daw::ui
