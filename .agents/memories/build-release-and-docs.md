# Build, Release, Runtime, and Documentation Memory

## Toolchain and runtime layout

- Inspect the actual compiler and package paths in `build*/CMakeCache.txt` before diagnosing Assimp. MinGW/TDM-GCC needs a compatible MinGW triplet; `assimp:x64-windows` is an MSVC ABI and cannot be mixed safely.
- Runtime output keeps `komapedit.exe` and notices at the root, DLLs under `bin/`, and application settings under `settings/`. `maploader.dll`, `model_loader.dll`, Assimp, and copied dependencies must match that layout.
- The EXE requires the exact maploader API and model loader v2 API. Do not add legacy fallback dispatch to hide a mismatch.
- Release-only failures need Release evidence. A historical async Include crash appeared only under optimized MinGW because inline code accessed cross-translation-unit weak TLS; moving TLS access behind out-of-line functions in its owning `.cpp` fixed the root cause. Reconfirm current code before applying this pattern elsewhere.
- PowerShell profile warnings can be unrelated noise. Judge build success from command exit, linker output, produced binaries, and tests.

## Build scope

- Use `.\build_dev.bat` for normal work and `.\build_release.bat` only for packaging, distribution, dependency layout, optimization-sensitive behavior, or explicit requests.
- Strict warnings are opt-in and should be configured explicitly for self-owned targets.
- A link `Permission denied` often means the GUI executable is still running; inspect the process before changing build scripts.

## Documentation ownership

- README files describe user-visible current behavior and limitations.
- `TODO.md` is unfinished work only; `docs/TODO_done.md` archives completion.
- `docs/dev*.md` describes human development facts and architecture.
- `docs/ai-dev*.md` guides people supervising AI coding tools.
- `AGENTS.md` holds non-negotiable constraints, general workflow, and indexes. Detailed repeatable execution belongs in `.agents/skills`; durable historical lessons belong in `.agents/memories`.

## Documentation validation

- Establish every behavior statement from current source/tests before editing docs. Historical memories explain intent but can be stale.
- Keep English and Simplified Chinese pairs semantically aligned, including tables and status wording.
- Preserve UTF-8 without BOM and avoid mixed line endings or mojibake.
- Use `git diff --check`, direct ignored-file inspection, table parity checks, and link/path verification. A documentation-only change normally does not require a build.
