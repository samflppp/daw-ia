"""JSON-RPC 2.0 transport over a local socket. Only entry point for calls from core/.

One JSON object per line, UTF-8. Content-Length framing would buy nothing here:
every message is small, and a newline cannot appear inside a JSON string without
being escaped.

Both sides ask. The DAW asks the copilot to answer a request typed by the user;
the copilot asks the DAW for the project, the tools and the commands. So a peer
is a client and a server at once, and the reader thread has to tell the two
apart: a message with a "method" is a request, anything else is an answer to one
of ours.

Requests are handled on a worker thread, never on the reader thread. Handling
`copilot.ask` means calling back into the DAW and waiting, and a reader thread
that waits for a line it is supposed to read itself is a deadlock.
"""

from __future__ import annotations

import json
import socket
import threading
from collections.abc import Callable
from concurrent.futures import Future, ThreadPoolExecutor
from typing import Any

JsonObject = dict[str, Any]
Handler = Callable[[JsonObject], Any]


class RpcError(Exception):
    """An error the other side sent back, or a connection that died."""

    def __init__(self, message: str, code: str = "rpc") -> None:
        super().__init__(message)
        self.code = code


class Peer:
    """One end of the link. Connects, reads, answers, and asks."""

    def __init__(self, connection: socket.socket, handlers: dict[str, Handler] | None = None) -> None:
        self._socket = connection
        self._handlers: dict[str, Handler] = handlers or {}
        self._pending: dict[int, Future[Any]] = {}
        self._next_id = 1
        self._send_lock = threading.Lock()
        self._state_lock = threading.Lock()
        self._workers = ThreadPoolExecutor(max_workers=4, thread_name_prefix="rpc")
        self._closed = threading.Event()

    @classmethod
    def connect(cls, port: int, handlers: dict[str, Handler] | None = None, timeout: float = 5.0) -> Peer:
        connection = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        connection.settimeout(None)
        return cls(connection, handlers)

    def on(self, method: str, handler: Handler) -> None:
        self._handlers[method] = handler

    # --- reading ----------------------------------------------------------

    def serve_forever(self) -> None:
        """Reads until the other end goes away. Returns then, it never raises."""
        buffer = b""
        while not self._closed.is_set():
            try:
                chunk = self._socket.recv(8192)
            except OSError:
                break

            if not chunk:
                break

            buffer += chunk
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                if line.strip():
                    self._dispatch(line.decode("utf-8"))

        self.close()

    def _dispatch(self, line: str) -> None:
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            return

        if not isinstance(message, dict):
            return

        if "method" in message:
            self._workers.submit(self._answer, message)
            return

        self._settle(message)

    def _settle(self, message: JsonObject) -> None:
        identifier = message.get("id")
        if not isinstance(identifier, int):
            return

        with self._state_lock:
            pending = self._pending.pop(identifier, None)

        if pending is None:
            return

        error = message.get("error")
        if error:
            text = error.get("message", "erreur inconnue") if isinstance(error, dict) else str(error)
            code = error.get("code", "rpc") if isinstance(error, dict) else "rpc"
            pending.set_exception(RpcError(text, code))
            return

        pending.set_result(message.get("result"))

    def _answer(self, message: JsonObject) -> None:
        method = message.get("method", "")
        handler = self._handlers.get(method)
        identifier = message.get("id")

        if handler is None:
            if identifier is not None:
                self._write(
                    {
                        "jsonrpc": "2.0",
                        "id": identifier,
                        "error": {"code": "unknownMethod", "message": f"no method {method}"},
                    }
                )
            return

        try:
            result = handler(message.get("params") or {})
        except Exception as failure:  # noqa: BLE001 - a handler must never kill the link
            if identifier is not None:
                self._write(
                    {
                        "jsonrpc": "2.0",
                        "id": identifier,
                        "error": {"code": "handler", "message": str(failure)},
                    }
                )
            return

        if identifier is not None:
            self._write({"jsonrpc": "2.0", "id": identifier, "result": result})

    # --- asking -----------------------------------------------------------

    def request(self, method: str, params: JsonObject | None = None, timeout: float = 30.0) -> Any:
        pending: Future[Any] = Future()

        with self._state_lock:
            identifier = self._next_id
            self._next_id += 1
            self._pending[identifier] = pending

        self._write({"jsonrpc": "2.0", "id": identifier, "method": method, "params": params or {}})
        return pending.result(timeout=timeout)

    def notify(self, method: str, params: JsonObject | None = None) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def _write(self, message: JsonObject) -> None:
        payload = (json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8")
        with self._send_lock:
            try:
                self._socket.sendall(payload)
            except OSError as failure:
                raise RpcError(f"lien coupé : {failure}", "disconnected") from failure

    # --- closing ----------------------------------------------------------

    def close(self) -> None:
        if self._closed.is_set():
            return

        self._closed.set()

        # Whoever was waiting is told, rather than left waiting for a line that
        # will never come.
        with self._state_lock:
            waiting = list(self._pending.values())
            self._pending.clear()

        for pending in waiting:
            if not pending.done():
                pending.set_exception(RpcError("le lien avec le DAW est coupé", "disconnected"))

        try:
            self._socket.close()
        except OSError:
            pass

        self._workers.shutdown(wait=False)
