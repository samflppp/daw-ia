#pragma once

#include "daw/engine/Export.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

namespace daw::app
{

// Fichier > Exporter...: a dialog for the format and its quality, then where
// the file goes, then the render.
//
//   format     WAV, FLAC, MP3 or AAC
//   quality    the bit depth of WAV and FLAC, the bit rate of MP3 and AAC
//
// The dialog is an AlertWindow, so the verification answers it the way a
// person does. The system's save dialog after it is the one step it cannot
// drive: in a verification run the file goes to a folder given in advance.
class SongExporter
{
public:
    struct Wiring
    {
        tracktion::Edit& edit;
        std::function<juce::String()> projectName;
        std::function<juce::File()> defaultFolder;

        // The title bar's word after the project name.
        std::function<void(const juce::String& status, bool lasting)> status;

        // A refusal said in a message box.
        std::function<void(const juce::String& title, const juce::String& message)> tell;
    };

    explicit SongExporter(Wiring wiring);
    ~SongExporter();

    SongExporter(const SongExporter&) = delete;
    SongExporter& operator=(const SongExporter&) = delete;

    // Opens the dialog.
    void start();

    // The verification's run: no save dialog, the files go into this folder
    // under the project's name.
    void writeInto(const juce::File& folder) { fixedFolder_ = folder; }

    [[nodiscard]] const juce::File& lastExport() const noexcept { return lastExport_; }
    [[nodiscard]] const juce::String& lastError() const noexcept { return lastError_; }

    // The dialog's fields and its answer, by name and by code.
    static constexpr const char* formatField = "format";
    static constexpr const char* qualityField = "quality";
    static constexpr int exportButton = 1;

    // The formats in the order the menu lists them, from 1.
    [[nodiscard]] static engine::ExportFormat formatAt(int id);

private:
    void fillQualities(juce::AlertWindow& dialog) const;
    [[nodiscard]] engine::ExportSettings settingsOf(juce::AlertWindow& dialog) const;
    void chooseTarget(const engine::ExportSettings& settings);
    void write(const juce::File& target, const engine::ExportSettings& settings);

    Wiring wiring_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::optional<juce::File> fixedFolder_;
    juce::File lastExport_;
    juce::String lastError_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(SongExporter)
};

} // namespace daw::app
