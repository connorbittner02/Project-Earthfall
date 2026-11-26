# Earthfall contributor notes

## Project
- Unreal Engine 5.8 C++ project.
- Keep the prototype self-contained and runnable without marketplace plugins.
- Treat the attached HTML as design input; validate technical claims before adopting them.

## Collaboration
- Claude Code: read this file and CLAUDE.md; work on a feature branch, build before opening a PR, and do not commit generated Unreal folders.
- Gemini: use the repository as design/review context; propose changes as a branch or patch, and do not overwrite other contributors' work.
- All contributors should use pull requests for non-trivial changes and avoid direct pushes to main.

## Build and launch
- Open Earthfall.uproject in Unreal Engine 5.8.
- C++ target/module: Earthfall.
- Keep large source datasets and licensed assets out of Git; document provenance and import steps.
