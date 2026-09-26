"""Entry point: `python -m daw_services` or `daw-services`."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

from daw_services import __version__


def _default_model() -> Path:
    appdata = os.environ.get("APPDATA")
    base = Path(appdata) if appdata else Path.home() / ".config"
    return base / "DAW IA" / "generation" / "markov.json"


def _build_corpus(raw: Path, annotations: Path, out: Path) -> int:
    from daw_services.harmony import corpus

    if not raw.is_dir():
        print(f"pas de corpus : {raw} n'existe pas")
        return 1

    model, report = corpus.build(raw, annotations)
    text = corpus.describe(report)
    print(text)
    if report.kept == 0:
        print("aucun fichier gardé : rien d'écrit, le DAW garde le repli")
        return 1

    corpus.write(model, out)
    out.with_name("markov-rapport.txt").write_text(text + "\n", encoding="utf-8")
    print(f"écrit : {out}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="daw-services")
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    subcommands = parser.add_subparsers(dest="service")

    # The copilot is launched by the DAW, which passes the port it listens on.
    # Nothing else is configured here: the key travels by environment, because
    # a command line is readable by every process on the machine.
    copilot = subcommands.add_parser("copilot", help="Connect to a running DAW and answer its requests.")
    copilot.add_argument("--port", type=int, required=True)

    # The corpus pipeline: the user's MIDI files in, the style model out. Both
    # stay outside the repository: the corpus is published work.
    corpus = subcommands.add_parser("corpus", help="Build the style model from a folder of MIDI files.")
    corpus_steps = corpus.add_subparsers(dest="step", required=True)
    build = corpus_steps.add_parser("build", help="Clean, annotate and count the corpus.")
    build.add_argument("--raw", type=Path, default=Path.home() / "DAW IA corpus" / "raw")
    build.add_argument("--csv", type=Path, default=None, help="Annotations (default: next to --raw).")
    build.add_argument("--out", type=Path, default=_default_model())

    arguments = parser.parse_args(argv)

    if arguments.service == "corpus":
        annotations = arguments.csv or arguments.raw.parent / "corpus.csv"
        return _build_corpus(arguments.raw, annotations, arguments.out)

    if arguments.service == "copilot":
        from daw_services.copilot import run

        return run(arguments.port)

    print(f"daw-services {__version__}: no service asked for.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
