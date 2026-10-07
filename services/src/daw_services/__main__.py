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
    # A check's copilot (S25): answers from a table, no model, no key.
    copilot.add_argument(
        "--table", type=Path, default=None, help="Answer from this table instead of a model."
    )

    # The corpus pipeline: the user's MIDI files in, the style model out. Both
    # stay outside the repository: the corpus is published work.
    corpus = subcommands.add_parser("corpus", help="Build the style model from a folder of MIDI files.")
    corpus_steps = corpus.add_subparsers(dest="step", required=True)
    build = corpus_steps.add_parser("build", help="Clean, annotate and count the corpus.")
    build.add_argument("--raw", type=Path, default=Path.home() / "DAW IA corpus" / "raw")
    build.add_argument("--csv", type=Path, default=None, help="Annotations (default: next to --raw).")
    build.add_argument("--out", type=Path, default=_default_model())

    # The stem separator (S22): one file, one process, progress on stdout.
    # The DAW cancels by ending the process.
    separate = subcommands.add_parser("separate", help="Separate an audio file into four stems.")
    separate.add_argument("--model", choices=["best", "fast", "fake"], default="best")
    separate.add_argument("--input", type=Path)
    separate.add_argument("--output", type=Path)
    separate.add_argument(
        "--signature", action="store_true", help="Print the model's signature, load nothing, and stop."
    )
    separate.add_argument("--models", type=Path, default=None, help="Where the weights are kept.")

    # The push-to-talk's transcriber (S25): one process kept alive between
    # two phrases, a JSON object per line each way. The DAW ends it.
    voix = subcommands.add_parser("voix", help="Transcribe the phrases the DAW sends, one per line.")
    voix.add_argument("--models", type=Path, default=None, help="Where the weights are kept.")
    voix.add_argument("--replay", type=Path, default=None, help="Replay what the model heard (the CI).")
    voix.add_argument("--install", action="store_true", help="Download the weights, and stop.")
    voix.add_argument("--port", type=int, default=None, help="The DAW's port; stdin and stdout without it.")

    arguments = parser.parse_args(argv)

    if arguments.service == "voix":
        from daw_services.voice import service

        if arguments.install:
            return service.install(arguments.models)
        if arguments.port is not None:
            return service.serve(arguments.port, arguments.models, arguments.replay)
        return service.run(arguments.models, arguments.replay)

    if arguments.service == "separate":
        from daw_services.stems import run as separate_file
        from daw_services.stems import signature_of

        if arguments.signature:
            print(signature_of(arguments.model), flush=True)
            return 0
        if arguments.input is None or arguments.output is None:
            parser.error("separate: --input and --output are required")
        return separate_file(arguments.model, arguments.input, arguments.output, arguments.models)

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
