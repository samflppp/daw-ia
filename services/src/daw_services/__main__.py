"""Entry point: `python -m daw_services` or `daw-services`."""

from __future__ import annotations

import argparse

from daw_services import __version__


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="daw-services")
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    parser.parse_args(argv)
    print(f"daw-services {__version__}: no service wired yet (S1).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
