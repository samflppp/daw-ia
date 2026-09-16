# services/

Services Python, process séparé du core. IPC : JSON-RPC 2.0 sur socket local.

| Package | Rôle |
|---|---|
| `rpc` | Transport JSON-RPC, seul point d'entrée depuis `core/` |
| `mcp` | Serveur MCP, expose les commandes du Command Bus |
| `harmony` | Moteur harmonique |
| `conditioning` | Conditionnement des entrées modèle |
| `ia_provider` | Interface IA : API externe puis modèle ONNX local |

```bash
uv sync
uv run pytest
uv run ruff check .
```
