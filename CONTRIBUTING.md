# Contributing to Salvus

Thank you for your interest in contributing to Salvus. Because Salvus operates on storage devices and sensitive user data, we hold all contributions to strict standards of correctness, safety, and clarity.

## Core Engineering Rules

1. **Data integrity overrides convenience:** Never introduce code paths that write to source storage or modify partition tables automatically.
2. **Validate all untrusted metadata:** File-system structures, boot sectors, MFT records, and IPC messages must be treated as untrusted input with strict bounds and resource checks.
3. **No simulated functionality:** Do not submit placeholder recovery results, fake progress bars, or mock data presented as real features.
4. **Preserve official branding:** `Salvus.ico` and `Salvus.png` must remain in the repository root without renaming, relocating, or modifying them.
5. **Keep dependencies minimal:** Justify every new third-party dependency and document it in `docs/DEPENDENCIES.md`.

## Development Workflow

1. Fork the repository and create a topic branch from `main`.
2. Build and test both the C++20 engine (`Salvus.Engine`) and the .NET 10 solution (`Salvus.sln`). See [docs/BUILD.md](docs/BUILD.md) and [docs/TESTING.md](docs/TESTING.md).
3. Ensure all existing and new unit tests pass with zero warnings treated as errors where configured.
4. Update relevant documentation and `CHANGELOG.md` when adding user-visible changes.
5. Open a pull request describing the change, testing performed, and any data-integrity or security considerations.

## Licensing

By submitting a contribution, you agree that your work is licensed under the GNU General Public License v3.0 or later (`GPL-3.0-or-later`).
