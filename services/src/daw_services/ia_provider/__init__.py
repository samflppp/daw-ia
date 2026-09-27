"""IAProvider interface. External API first, local ONNX model later, same contract.

Why an interface before there are two implementations: the copilot must not
learn the shape of one vendor's HTTP body. What it needs is a turn — some text,
some tool calls, and what the turn cost — and that is what a local model will
have to give back too.

The key is read from the environment and never from a file in the repository,
never from a command line argument: arguments are readable by every process on
the machine.
"""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from collections.abc import Sequence
from dataclasses import dataclass, field
from typing import Any, Protocol

API_KEY_VARIABLE = "DAW_IA_ANTHROPIC_API_KEY"
DEFAULT_MODEL = "claude-sonnet-5"

# Room for one answer. A request that writes music used to run out of it at
# 2048: forty note.add calls, the last one cut in the middle of its arguments.
MAX_TOKENS = 8192
DEFAULT_ENDPOINT = "https://api.anthropic.com/v1/messages"
ANTHROPIC_VERSION = "2023-06-01"

# The API refuses a tool name that is not ^[a-zA-Z0-9_-]{1,128}$, and every
# command of the DAW is named with a dot: "track.add". The dot is the DAW's
# name and it stays the DAW's name — renaming the commands to please one
# vendor would put that vendor's rule in the domain. So the dot is swapped for
# a colon-free separator on the way out and swapped back on the way in, here,
# in the one place that knows which vendor is being talked to.
TOOL_NAME_SEPARATOR = "__"


def _safe_tool_name(name: str) -> str:
    return name.replace(".", TOOL_NAME_SEPARATOR)


def _original_tool_name(name: str, known: dict[str, str]) -> str:
    return known.get(name, name.replace(TOOL_NAME_SEPARATOR, "."))


class ProviderUnavailable(Exception):
    """No key, no network, or a model that refused. Said in French to the user."""


@dataclass(frozen=True)
class ToolCall:
    """One tool the model asked for."""

    call_id: str
    name: str
    arguments: dict[str, Any]


@dataclass(frozen=True)
class Usage:
    """What a request cost, in tokens. Summed per request, logged, and shown.

    The business plan needs a number per request, not a monthly bill, so it is
    counted here where the answer arrives and nowhere else.
    """

    input_tokens: int = 0
    output_tokens: int = 0
    cache_read_tokens: int = 0

    # Writing to the cache is billed above the plain input rate, so it is
    # counted apart. A cost that only showed the reads would flatter itself.
    cache_write_tokens: int = 0

    def plus(self, other: Usage) -> Usage:
        return Usage(
            self.input_tokens + other.input_tokens,
            self.output_tokens + other.output_tokens,
            self.cache_read_tokens + other.cache_read_tokens,
            self.cache_write_tokens + other.cache_write_tokens,
        )

    def as_dict(self) -> dict[str, int]:
        return {
            "inputTokens": self.input_tokens,
            "outputTokens": self.output_tokens,
            "cacheReadTokens": self.cache_read_tokens,
            "cacheWriteTokens": self.cache_write_tokens,
        }


@dataclass
class Turn:
    """What the model answered: text, tools it wants, and the cost."""

    text: str = ""
    tool_calls: list[ToolCall] = field(default_factory=list)
    usage: Usage = field(default_factory=Usage)
    stop_reason: str = "end_turn"

    # The assistant message as the provider wants it back on the next turn.
    raw_content: list[dict[str, Any]] = field(default_factory=list)


class IAProvider(Protocol):
    """What the copilot needs of a model, and nothing else."""

    @property
    def model(self) -> str: ...

    def converse(
        self,
        system: str,
        messages: Sequence[dict[str, Any]],
        tools: Sequence[dict[str, Any]],
    ) -> Turn: ...


class AnthropicProvider:
    """Claude through the Messages API.

    Native tool use with several calls in one turn, which is the shape of a
    request that becomes one history entry; French without conditioning; and a
    `usage` block on every answer, so the cost is measured instead of guessed.
    """

    def __init__(
        self, model: str = DEFAULT_MODEL, endpoint: str = DEFAULT_ENDPOINT, timeout: float = 60.0
    ) -> None:
        self._model = model
        self._endpoint = endpoint
        self._timeout = timeout

    @property
    def model(self) -> str:
        return self._model

    def converse(
        self,
        system: str,
        messages: Sequence[dict[str, Any]],
        tools: Sequence[dict[str, Any]],
    ) -> Turn:
        key = os.environ.get(API_KEY_VARIABLE, "").strip()
        if not key:
            raise ProviderUnavailable(
                f"Aucune clé d'API. Posez {API_KEY_VARIABLE} dans l'environnement, puis relancez le copilote."
            )

        # The two blocks that do not change from one turn to the next are the
        # system prompt and the tools: twenty-seven schemas, several thousand
        # tokens, re-sent at every turn of every request. Marking the end of
        # the tools makes everything above it cacheable, which covers both.
        #
        # The project state is deliberately left out of the cache: it changes
        # at every request, and caching what changes costs more than it saves.
        sent_tools = []
        known: dict[str, str] = {}
        for tool in tools:
            renamed = dict(tool)
            original = str(renamed.get("name", ""))
            renamed["name"] = _safe_tool_name(original)
            known[renamed["name"]] = original
            sent_tools.append(renamed)

        if sent_tools:
            sent_tools[-1] = {**sent_tools[-1], "cache_control": {"type": "ephemeral"}}

        body = json.dumps(
            {
                "model": self._model,
                "max_tokens": MAX_TOKENS,
                "system": [{"type": "text", "text": system}],
                "messages": list(messages),
                "tools": sent_tools,
            },
            ensure_ascii=False,
        ).encode("utf-8")

        request = urllib.request.Request(  # noqa: S310 - the endpoint is ours, and it is https
            self._endpoint,
            data=body,
            headers={
                "content-type": "application/json",
                "x-api-key": key,
                "anthropic-version": ANTHROPIC_VERSION,
            },
            method="POST",
        )

        try:
            with urllib.request.urlopen(request, timeout=self._timeout) as answer:  # noqa: S310
                payload = json.loads(answer.read().decode("utf-8"))
        except urllib.error.HTTPError as failure:
            detail = failure.read().decode("utf-8", errors="replace")[:400]
            raise ProviderUnavailable(
                f"Le modèle a refusé la requête ({failure.code}) : {detail}"
            ) from failure
        except (urllib.error.URLError, TimeoutError) as failure:
            raise ProviderUnavailable(f"Le modèle est injoignable : {failure}") from failure
        except json.JSONDecodeError as failure:
            raise ProviderUnavailable("Le modèle a répondu quelque chose d'illisible.") from failure

        return _turn_from(payload, known)


def _turn_from(payload: dict[str, Any], known: dict[str, str] | None = None) -> Turn:
    turn = Turn(stop_reason=payload.get("stop_reason", "end_turn"))

    for block in payload.get("content", []):
        if not isinstance(block, dict):
            continue

        turn.raw_content.append(block)

        if block.get("type") == "text":
            turn.text += block.get("text", "")
        elif block.get("type") == "tool_use":
            turn.tool_calls.append(
                ToolCall(
                    call_id=str(block.get("id", "")),
                    name=_original_tool_name(str(block.get("name", "")), known or {}),
                    arguments=dict(block.get("input") or {}),
                )
            )

    usage = payload.get("usage") or {}
    turn.usage = Usage(
        input_tokens=int(usage.get("input_tokens", 0)),
        output_tokens=int(usage.get("output_tokens", 0)),
        cache_read_tokens=int(usage.get("cache_read_input_tokens", 0)),
        cache_write_tokens=int(usage.get("cache_creation_input_tokens", 0)),
    )

    return turn


class ScriptedProvider:
    """A provider that answers from a list. What the tests run against.

    The pipe is tested with this one; the proof of the week is done against the
    real model, on the real binary, and looked at. A test that questions the
    state proves nothing about what moved on screen.
    """

    def __init__(self, turns: Sequence[Turn], model: str = "scripted") -> None:
        self._turns = list(turns)
        self._model = model
        self.calls: list[list[dict[str, Any]]] = []
        # What each turn was shown: a test checks what a read handed back.
        self.messages: list[list[dict[str, Any]]] = []

    @property
    def model(self) -> str:
        return self._model

    def converse(
        self,
        system: str,
        messages: Sequence[dict[str, Any]],
        tools: Sequence[dict[str, Any]],
    ) -> Turn:
        del system
        self.calls.append([dict(tool) for tool in tools])
        self.messages.append(list(messages))

        if not self._turns:
            return Turn(text="Je n'ai rien de plus à dire.")

        return self._turns.pop(0)
