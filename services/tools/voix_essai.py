"""The push-to-talk's test set, measured (S25).

    uv run --extra voix --group voix-essai python tools/voix_essai.py prepare
    uv run --extra voix --group voix-essai python tools/voix_essai.py record
    uv run --extra voix --group voix-essai python tools/voix_essai.py report

prepare  makes the phrases without a voice (silence, breath) next to those
         scripts/voix/synthese.ps1 had Windows speak.
record   runs Parakeet on every phrase, clean and with a song behind at three
         levels, and keeps what it heard in tests/voix/parakeet-entendu.json:
         the table the CI replays (no weights there), and the times.
report   the error rate by category and condition, with and without the
         project's names, and what the doubt rules catch; from the table
         alone, so thresholds are tuned without running the model again.

The song behind is built here (a kick, a snare, hats, a bass, chords at
120 BPM), deterministic: not a real record. It is mixed at a speech-to-song
ratio, the speech at -20 dBFS: 0 dB is a song as loud as the voice in the
microphone, the case of laptop speakers near it; +20 dB is that song lowered
by 20 dB while the key is held (the ducking decided on 7 October 2026).
The voices are Windows' synthetic ones: the rates are optimistic, never the
founder's.
"""

from __future__ import annotations

import json
import re
import sys
import time
import unicodedata
import wave
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent.parent
SET = HERE / "tests" / "voix"
AUDIO = SET / "synthese"
TABLE = SET / "parakeet-entendu.json"
RATE = 16000
CONDITIONS = {"propre": None, "morceau 0 dB": 0.0, "morceau 10 dB": 10.0, "morceau baissé, 20 dB": 20.0}


def phrases() -> list[dict]:
    out = []
    for line in (SET / "phrases.tsv").read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            ident, category, voice, text = line.split("\t", 3)
            out.append({"id": ident, "category": category, "voice": voice, "text": text})
    return out


def names() -> list[str]:
    raw = json.loads((SET / "noms.json").read_text(encoding="utf-8"))
    return [n for group in raw.values() for n in group]


def read(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as file:
        assert file.getframerate() == RATE and file.getnchannels() == 1 and file.getsampwidth() == 2, path
        return np.frombuffer(file.readframes(file.getnframes()), dtype="<i2").astype(np.float32) / 32768.0


def write(path: Path, samples: np.ndarray) -> None:
    pcm = np.clip(np.round(samples * 32768.0), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes(pcm.tobytes())


def song(seconds: float) -> np.ndarray:
    """Two bars at 120 BPM, looped: kick, snare, hats, bass, chords."""
    rng = np.random.default_rng(7)
    beat = RATE // 2
    bar = np.zeros(beat * 4, np.float32)
    t = np.arange(beat) / RATE
    kick = np.sin(2 * np.pi * (50 + 120 * np.exp(-t * 30)) * t) * np.exp(-t * 8)
    snare = rng.standard_normal(beat) * np.exp(-t * 18) * 0.5
    hat = rng.standard_normal(beat // 4) * np.exp(-np.arange(beat // 4) / RATE * 60) * 0.25
    for b in range(4):
        bar[b * beat : (b + 1) * beat] += kick if b % 2 == 0 else snare
        for h in range(2):
            start = b * beat + h * beat // 2
            bar[start : start + len(hat)] += hat
    tb = np.arange(len(bar)) / RATE
    bass = 0.3 * np.sign(np.sin(2 * np.pi * 55 * tb)) * 0.5
    chords = sum(0.08 * np.sin(2 * np.pi * f * tb) for f in (220.0, 261.6, 329.6))
    bar = bar + bass + chords
    loops = int(np.ceil(seconds * RATE / len(bar)))
    return np.tile(bar, loops)[: int(seconds * RATE)].astype(np.float32)


def rms_db(samples: np.ndarray) -> float:
    return 20 * np.log10(np.sqrt(np.mean(samples**2)) + 1e-12)


def mixed(speech: np.ndarray, ratio_db: float | None) -> np.ndarray:
    if ratio_db is None:
        return speech
    voiced = speech[np.abs(speech) > 1e-4]
    level = rms_db(voiced) if len(voiced) else -20.0
    gain = 10 ** ((-20.0 - level) / 20)
    speech = speech * gain
    behind = song(len(speech) / RATE)
    behind *= 10 ** ((-20.0 - ratio_db - rms_db(behind)) / 20)
    return np.clip(speech + behind, -1.0, 1.0).astype(np.float32)


def prepare() -> None:
    rng = np.random.default_rng(25)
    write(AUDIO / "d01.wav", np.zeros(int(1.5 * RATE), np.float32))
    write(AUDIO / "d02.wav", (rng.standard_normal(2 * RATE) * 10 ** (-30 / 20)).astype(np.float32))
    print("d01 (silence) et d02 (souffle) écrits")


def record() -> None:
    sys.path.insert(0, str(HERE / "src"))
    from daw_services.voice import digest
    from daw_services.voice.parakeet import ParakeetTranscriber

    started = time.perf_counter()
    model = ParakeetTranscriber()
    load = time.perf_counter() - started
    heard: dict[str, dict] = {}
    runs: dict[str, dict] = {}
    times = []
    for phrase in phrases():
        clean = read(AUDIO / f"{phrase['id']}.wav")
        for condition, ratio in CONDITIONS.items():
            samples = mixed(clean, ratio)
            began = time.perf_counter()
            result = model.transcribe(samples)
            elapsed = time.perf_counter() - began
            times.append((len(samples) / RATE, elapsed))
            key = digest(samples)
            heard[key] = {
                "text": result.text,
                "words": [[w.text, w.confidence, w.start] for w in result.words],
            }
            runs.setdefault(phrase["id"], {})[condition] = key
            print(f"{phrase['id']} {condition:24} {elapsed * 1000:5.0f} ms  {result.text}")
    TABLE.write_text(
        json.dumps(
            {
                "transcriber": "parakeet-tdt-0.6b-v3-int8",
                "load_seconds": round(load, 2),
                "times": [[round(a, 2), round(b, 3)] for a, b in times],
                "runs": runs,
                "heard": heard,
            },
            ensure_ascii=False,
            indent=1,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"écrit : {TABLE}")


def norm(text: str) -> str:
    from text_to_num import alpha2digit

    text = unicodedata.normalize("NFC", text.lower()).replace("’", "'")
    text = re.sub(r"\(.*?\)", " ", text)
    # Written either way, the same words: « 40% » and « quarante pour cent ».
    text = text.replace("%", " pour cent").replace("-", " ")
    text = alpha2digit(text, "fr", threshold=0)
    text = re.sub(r"\bhz\b", "hertz", text)
    text = re.sub(r"\bdb\b", "décibels", text)
    text = text.replace("-", " ")
    text = re.sub(r"[^\w' ]+", " ", text)
    return " ".join(text.split())


def aided(words: list[str], found) -> str:
    """The phrase with each name proposed put in place of what was heard: what
    the person gets by accepting the proposal."""
    out = list(words)
    for named in sorted(found, key=lambda n: -n.at):
        if not named.exact and named.name:
            length = len(named.heard.split())
            out[named.at : named.at + length] = [named.name]
    return " ".join(out)


def report() -> None:
    sys.path.insert(0, str(HERE / "src"))
    import jiwer

    from daw_services.voice import Heard, Word, homophones
    from daw_services.voice.doubt import judge

    table = json.loads(TABLE.read_text(encoding="utf-8"))
    project = names()
    rows = []
    for phrase in phrases():
        clean = read(AUDIO / f"{phrase['id']}.wav")
        for condition, ratio in CONDITIONS.items():
            entry = table["heard"][table["runs"][phrase["id"]][condition]]
            heard = homophones(Heard(entry["text"], [Word(*w) for w in entry["words"]]))
            verdict = judge(heard, mixed(clean, ratio), project)
            reference = norm(phrase["text"]) if phrase["voice"] != "aucune" else ""
            plain = norm(heard.text)
            with_names = norm(aided([w.text for w in heard.words], verdict.names))

            def errors(hypothesis: str, reference: str = reference) -> tuple[int, int]:
                if not reference:
                    return (len(hypothesis.split()), 0)
                out = jiwer.process_words(reference, hypothesis)
                return (out.substitutions + out.deletions + out.insertions, len(reference.split()))

            rows.append(
                {
                    "id": phrase["id"],
                    "category": phrase["category"],
                    "condition": condition,
                    "plain": errors(plain),
                    "aided": errors(with_names),
                    "doubtful": verdict.doubtful,
                    "reasons": verdict.reasons,
                    "text": heard.text,
                    "right": errors(plain)[0] == 0 and phrase["category"] != "douteuse",
                }
            )

    def rate(selected, which: str) -> str:
        wrong = sum(r[which][0] for r in selected)
        total = sum(r[which][1] for r in selected)
        return f"{100 * wrong / total:5.1f} %" if total else "    —  "

    categories = ["commande", "tonalite", "nombre", "anglais", "nom", "autre"]
    print("Taux d'erreur par mot (sans l'aide des noms / avec)")
    print(f"{'':12}" + "".join(f"{c:>26}" for c in CONDITIONS))
    for category in categories + ["toutes"]:
        line = f"{category:12}"
        for condition in CONDITIONS:
            chosen = [
                r
                for r in rows
                if r["condition"] == condition
                and (r["category"] == category or (category == "toutes" and r["category"] != "douteuse"))
            ]
            line += f"{rate(chosen, 'plain'):>13}{rate(chosen, 'aided'):>13}"
        print(line)

    print("\nLa garde : phrases dites sûres et fausses, phrases douteuses")
    for condition in CONDITIONS:
        chosen = [r for r in rows if r["condition"] == condition]
        sure = [r for r in chosen if not r["doubtful"]]
        sure_wrong = [r for r in sure if not r["right"]]
        built = [r for r in chosen if r["category"] == "douteuse"]
        caught = [r for r in built if r["doubtful"]]
        right_doubted = [r for r in chosen if r["right"] and r["doubtful"]]
        print(
            f"{condition:24} sûres {len(sure):2} dont fausses {len(sure_wrong):2} ; "
            f"douteuses construites prises {len(caught)}/{len(built)} ; "
            f"justes mais douteuses {len(right_doubted):2}/{sum(1 for r in chosen if r['right'])}"
        )
        for r in sure_wrong:
            print(f"    sûre et fausse : {r['id']} « {r['text']} »")
    times = np.array(table["times"])
    print(
        f"\nTemps : chargement {table['load_seconds']} s ; par phrase médiane "
        f"{np.median(times[:, 1]) * 1000:.0f} ms, "
        f"95e centile {np.percentile(times[:, 1], 95) * 1000:.0f} ms, "
        f"pour {np.median(times[:, 0]):.1f} s de parole en médiane"
    )
    if "--details" in sys.argv:
        for r in rows:
            mark = "D" if r["doubtful"] else "S"
            print(f"{r['id']} {r['condition']:24} {mark} e={r['plain'][0]} « {r['text']} » {r['reasons']}")


if __name__ == "__main__":
    {"prepare": prepare, "record": record, "report": report}[sys.argv[1]]()
