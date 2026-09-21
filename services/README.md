# services/

Services Python, process séparé du core. IPC : JSON-RPC 2.0 sur socket local.

| Package | Rôle |
|---|---|
| `rpc` | Transport JSON-RPC, seul point d'entrée depuis `core/` |
| `mcp` | Serveur MCP, expose les commandes du Command Bus |
| `harmony` | Moteur harmonique |
| `conditioning` | Conditionnement des entrées modèle |
| `ia_provider` | Interface IA : API externe puis modèle ONNX local |
| `copilot` | L'agent : une requête en français devient une entrée d'historique |

```bash
uv sync
uv run pytest
uv run ruff check .
uv run ruff format --check .
```

## Le copilote

Le DAW le lance et lui passe le port sur lequel il écoute. À la main, pour
déboguer contre un DAW déjà ouvert :

```bash
uv run daw-services copilot --port 49500
```

La clé d'API se pose dans l'environnement, jamais dans le dépôt et jamais sur
une ligne de commande — un argument est lisible par tous les process de la
machine. La variable est `DAW_IA_ANTHROPIC_API_KEY`, et `DAW_IA_MODEL` remplace
le modèle par défaut.

Sans clé, le copilote démarre, répond en français qu'il n'en a pas, et le projet
ne bouge pas. La CI n'appelle aucune API payante : les tests tournent contre un
fournisseur scripté.
