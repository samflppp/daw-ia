#include "SongExporter.h"

#include <utility>

namespace daw::app
{
namespace
{

// What a first export is offered: a WAV, the master a mastering engineer asks
// for. The dialog remembers nothing between two exports yet.
constexpr int defaultFormatId = 1;

// The second quality of every list: 24 bits for WAV and FLAC, 192 kbit/s in
// MP3, 128 kbit/s in AAC.
constexpr int defaultQualityId = 2;

juce::String formatName(engine::ExportFormat format)
{
    switch (format)
    {
    case engine::ExportFormat::wav:
        return "WAV";
    case engine::ExportFormat::flac:
        return "FLAC";
    case engine::ExportFormat::mp3:
        return "MP3";
    case engine::ExportFormat::aac:
        return "AAC (.m4a)";
    }
    return "WAV";
}

// The bit depths of WAV and FLAC, in the order the quality menu lists them.
std::vector<int> depthsFor(engine::ExportFormat format)
{
    if (format == engine::ExportFormat::wav)
        return {16, 24, 32};
    if (format == engine::ExportFormat::flac)
        return {16, 24};
    return {};
}

} // namespace

SongExporter::SongExporter(Wiring wiring)
    : wiring_(std::move(wiring))
{
}

SongExporter::~SongExporter() = default;

engine::ExportFormat SongExporter::formatAt(int id)
{
    switch (id)
    {
    case 2:
        return engine::ExportFormat::flac;
    case 3:
        return engine::ExportFormat::mp3;
    case 4:
        return engine::ExportFormat::aac;
    default:
        return engine::ExportFormat::wav;
    }
}

void SongExporter::start()
{
    auto* dialog = new juce::AlertWindow(u8"Exporter le morceau",
                                         u8"Toute la playlist, telle qu'elle joue.",
                                         juce::MessageBoxIconType::NoIcon);

    const juce::StringArray formats{formatName(engine::ExportFormat::wav),
                                    formatName(engine::ExportFormat::flac),
                                    formatName(engine::ExportFormat::mp3),
                                    formatName(engine::ExportFormat::aac)};
    dialog->addComboBox(formatField, formats, "Format");
    dialog->addComboBox(qualityField, {}, u8"Qualité");
    dialog->getComboBoxComponent(formatField)->setSelectedId(defaultFormatId, juce::dontSendNotification);
    fillQualities(*dialog);

    // The quality menu follows the format: bits for WAV and FLAC, a bit rate
    // for MP3 and AAC.
    dialog->getComboBoxComponent(formatField)->onChange = [this, dialog] { fillQualities(*dialog); };

    dialog->addButton("Exporter", exportButton, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::WeakReference<SongExporter> self{this};
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [self, dialog](int result)
                                {
                                    if (self == nullptr || result != exportButton)
                                        return;
                                    self->chooseTarget(self->settingsOf(*dialog));
                                }),
                            true);
}

void SongExporter::fillQualities(juce::AlertWindow& dialog) const
{
    auto* format = dialog.getComboBoxComponent(formatField);
    auto* quality = dialog.getComboBoxComponent(qualityField);
    if (format == nullptr || quality == nullptr)
        return;

    const auto chosen = formatAt(format->getSelectedId());
    quality->clear(juce::dontSendNotification);

    int id = 1;
    for (const auto depth : depthsFor(chosen))
        quality->addItem(depth == 32 ? juce::String{u8"32 bits flottant"} : juce::String(depth) + " bits",
                         id++);
    for (const auto rate : engine::bitRatesFor(chosen))
        quality->addItem(juce::String(rate) + " kbit/s", id++);

    quality->setSelectedId(defaultQualityId, juce::dontSendNotification);
}

engine::ExportSettings SongExporter::settingsOf(juce::AlertWindow& dialog) const
{
    engine::ExportSettings settings;
    settings.format = formatAt(dialog.getComboBoxComponent(formatField)->getSelectedId());

    const auto index = dialog.getComboBoxComponent(qualityField)->getSelectedId() - 1;
    const auto depths = depthsFor(settings.format);
    const auto rates = engine::bitRatesFor(settings.format);
    if (index >= 0 && index < static_cast<int>(depths.size()))
        settings.bitDepth = depths[static_cast<std::size_t>(index)];
    if (index >= 0 && index < static_cast<int>(rates.size()))
        settings.kilobitsPerSecond = rates[static_cast<std::size_t>(index)];
    return settings;
}

void SongExporter::chooseTarget(const engine::ExportSettings& settings)
{
    const auto extension = engine::extensionFor(settings.format);
    const auto name = wiring_.projectName().isNotEmpty() ? wiring_.projectName() : juce::String{"Export"};

    if (fixedFolder_.has_value())
    {
        static_cast<void>(fixedFolder_->createDirectory());
        write(fixedFolder_->getChildFile(name + extension), settings);
        return;
    }

    chooser_ = std::make_unique<juce::FileChooser>(
        "Exporter", wiring_.defaultFolder().getChildFile(name + extension), "*" + extension);

    juce::WeakReference<SongExporter> self{this};
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [self, settings, extension](const juce::FileChooser& chooser)
                          {
                              if (self == nullptr || chooser.getResult() == juce::File{})
                                  return;
                              self->write(chooser.getResult().withFileExtension(extension), settings);
                          });
}

void SongExporter::write(const juce::File& target, const engine::ExportSettings& settings)
{
    wiring_.status(juce::String{u8"export en cours…"}, true);
    lastError_ = engine::exportSong(wiring_.edit, target, settings);

    if (lastError_.isNotEmpty())
    {
        lastExport_ = juce::File{};
        wiring_.status({}, false);
        wiring_.tell("Exporter", lastError_);
        return;
    }

    lastExport_ = target;
    wiring_.status(juce::String{u8"exporté : "} + target.getFileName(), false);
}

} // namespace daw::app
