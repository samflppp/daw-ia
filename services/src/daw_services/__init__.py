"""DAW IA services, run in a process separate from core/.

The core talks to this process over JSON-RPC 2.0 on a local socket
(Unix domain socket on Linux, named pipe or loopback TCP on Windows).
"""

__version__ = "0.1.0"
