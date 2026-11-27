# Earthfall contributor notes

## Project
- Unreal Engine 5.8 C++ project.
- Keep the prototype self-contained and runnable without marketplace plugins.

## Plugin policy
- Built-in UE 5.8 plugins may be enabled. Free, open-source plugins may be added if they install headlessly, compile on UE 5.8 and clearly improve realism; record each one here.
- A plugin that needs a manual Epic Launcher login or breaks the build is reverted, and its manual install steps are documented instead.
- **Cesium for Unreal v2.31.0** (Apache 2.0, github.com/CesiumGS/cesium-unreal) streams terrain and imagery below 1.10 planet radii. It lives in `Plugins/CesiumForUnreal/`, which is git-ignored and never committed, and is listed as `"Optional": true` in `Earthfall.uproject`, so a clone without it still opens and builds. `Earthfall.Build.cs` defines `EARTHFALL_WITH_CESIUM` from the folder's presence. Install: extract `CesiumForUnreal-58-v2.31.0.zip` into `Plugins/`, delete `Plugins/CesiumForUnreal/Intermediate`, build. Streaming needs a Cesium ion access token set as the project default in the editor's Cesium panel; see `docs/30k-ft-cesium-plan.md`.
- Treat the attached HTML as design input; validate technical claims before adopting them.

## Collaboration
- Claude Code: read this file and CLAUDE.md; work on a feature branch, build before opening a PR, and do not commit generated Unreal folders.
- Gemini: use the repository as design/review context; propose changes as a branch or patch, and do not overwrite other contributors' work.
- All contributors should use pull requests for non-trivial changes and avoid direct pushes to main.

## Build and launch
- Open Earthfall.uproject in Unreal Engine 5.8.
- C++ target/module: Earthfall.
- Keep large source datasets and licensed assets out of Git; document provenance and import steps.
