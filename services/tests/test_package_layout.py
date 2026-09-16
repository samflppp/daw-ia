import importlib

import pytest

import daw_services
from daw_services.__main__ import main

SUBPACKAGES = ["rpc", "mcp", "harmony", "conditioning", "ia_provider"]


@pytest.mark.parametrize("name", SUBPACKAGES)
def test_subpackage_imports(name: str) -> None:
    module = importlib.import_module(f"daw_services.{name}")
    assert module.__doc__


def test_version_is_set() -> None:
    assert daw_services.__version__


def test_main_runs() -> None:
    assert main([]) == 0
