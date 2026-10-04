"""The real separators: HTDemucs models (Meta), through the demucs package.

  fast  htdemucs      one model, about the length of the song on an
                      i5-8365U (177 s for 3 minutes, 1.6 GB)
  best  htdemucs_ft   a bag of four fine-tuned models, one per stem
                      (688 s for 3 minutes, 2.6 GB)

Their weights are not under the code's MIT licence: their author provides
them "for research purpose" (demucs issues #267, #327, #508) and removed
the MIT tag from their Hugging Face card on 2026-08-31. The founder chose
to build the prototype on them and to seek a licence; they are downloaded
from Hugging Face on first use into the DAW's models folder, never shipped,
never in the repository.

Nothing here is imported by asking for a signature: torch loads only when a
separation runs.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from daw_services.stems.audio import Audio

# The weights each name runs, by their content: a cache keyed on these is
# invalidated the day the weights change.
MODELS = {
    "fast": ("htdemucs", "955717e8"),
    "best": ("htdemucs_ft", "f7e0c4bc-d12395a8-92cfc3b6-04573f0d"),
}


def signature_of(name: str) -> str:
    """`htdemucs-955717e8`: names a separation's model without loading it."""
    model, weights = MODELS[name]
    return f"{model}-{weights}"


def default_folder() -> Path:
    base = os.environ.get("LOCALAPPDATA") or str(Path.home() / ".cache")
    return Path(base) / "DAW IA" / "models"


@dataclass
class DemucsSeparator:
    name: str
    folder: Path

    def signature(self) -> str:
        return signature_of(self.name)

    def separate(self, audio: Audio, progress) -> dict[str, Audio]:
        # Hugging Face keeps what it downloads under HF_HOME: the DAW's
        # folder, not the person's cache, so removing the DAW removes them.
        os.environ["HF_HOME"] = str(self.folder / "huggingface")

        import torch
        from demucs.apply import apply_model
        from demucs.audio import convert_audio
        from demucs.pretrained import get_model

        progress(0.0)
        model = get_model(MODELS[self.name][0])
        model.eval()

        samples = torch.from_numpy(audio.samples)
        mix = convert_audio(samples, audio.rate, model.samplerate, model.audio_channels)
        length = mix.shape[-1]

        # Reference loudness, as demucs's own command line does: the model
        # sees a signal of unit spread, whatever the level of the song.
        reference = mix.mean(0)
        mean, spread = reference.mean(), reference.std() + 1e-8
        normalised = (mix - mean) / spread

        def told(state: dict) -> None:
            if state.get("state") != "end":
                return
            models = max(1, int(state.get("models", 1)))
            done = state.get("model_idx_in_bag", 0) + min(1.0, (state.get("segment_offset", 0) + 1) / length)
            progress(0.98 * done / models)

        with torch.no_grad():
            out = apply_model(
                model, normalised[None], split=True, overlap=0.25, progress=False, device="cpu", callback=told
            )[0]
        # The mean taken off the mix goes back once, shared by the stems:
        # demucs's command line adds it to each, and the four then add up to
        # the mix plus three times its offset.
        out = out * spread + mean / out.shape[0]

        stems = {}
        for index, source in enumerate(model.sources):
            back = convert_audio(out[index], model.samplerate, audio.rate, audio.samples.shape[0])
            stems[source] = Audio(back.numpy().astype("float32"), audio.rate)
        progress(1.0)
        return stems


def separator(name: str, folder: Path | None = None) -> DemucsSeparator:
    if name not in MODELS:
        raise ValueError(f"modèle inconnu : {name}")
    return DemucsSeparator(name, folder or default_folder())
