"""Entry point: `python -m daw_services` or `daw-services`."""

from __future__ import annotations

import argparse

from daw_services import __version__


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="daw-services")
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    subcommands = parser.add_subparsers(dest="service")

    # The copilot is launched by the DAW, which passes the port it listens on.
    # Nothing else is configured here: the key travels by environment, because
    # a command line is readable by every process on the machine.
    copilot = subcommands.add_parser("copilot", help="Connect to a running DAW and answer its requests.")
    copilot.add_argument("--port", type=int, required=True)

    arguments = parser.parse_args(argv)

    if arguments.service == "copilot":
        from daw_services.copilot import run

        return run(arguments.port)

    print(f"daw-services {__version__}: no service asked for.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
