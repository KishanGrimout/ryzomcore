# AGENTS.md — working rules for AI agents / contributors

## Documentation location

All project-specific documentation produced or maintained during this integration work
lives in the **`docs/` folder at this repo root**. Keep it the single source of truth:

- `docs/LEGACY_DYNWORLD_INTEGRATION.md` — the legacy-dynworld integration roadmap
  (audit conclusions, port status, unit checklists, runtime findings, error-code notes).
- Agent sessions must **read it before starting** and **append runtime findings there**
  (including new error codes encountered in logs, root causes, fixes and their file
  paths) rather than leaving that knowledge only in chat history.

Rules:

- New docs, checklists and notes of project-wide relevance → create them under `docs/`.
- Upstream `README.md` (Ryzom Core project readme) stays at the repo root untouched.
- Deployment-local READMEs that travel with the artifacts they describe
  (e.g. `pipeline/client_dev/local/README_SANDBOX.md`) may stay next to those
  artifacts, but link to them from `docs/` when they document integration-wide state.
- When moving docs, use `git mv` so the history stays connected.

## Shard research/debugging conventions (summary — full list in the roadmap doc)

- Deployed shard: `pipeline/shard_dev/*`, client sandbox: `pipeline/client_dev/local`.
- Runtime error codes shown by the client login UI are defined in the web-shard PHP
  layer (`code/web/.../login_translations.php`), NOT in C++ source — grep `code/web`
  for them; meanings are recorded in the roadmap doc's "Runtime error codes" section.
- Kept-by-design instrumentation (strip only after Phase 5 validation completes, then
  rebuild + redeploy): `LOOP_SNAIL` in `nel/src/net/service.cpp`, `MS_SNAIL` in
  `ms_automaton.cpp`/`tick_proxy.cpp`, client-side cbTick gap alarm in
  `tick_event_handler.cpp`.
