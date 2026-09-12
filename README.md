# Vortex

Native Windows x64 launcher and Direct3D 11 overlay with configurable profiles, preview, and managed updates.

Download **VortexSetup.exe** from [the latest release](https://github.com/vortex-release/Vortex/releases/latest). Install once; later releases appear on the Updates page. No GitHub account is required to download or update.

The current integration and native feature limits are documented in [the 3.27 integration notes](docs/integration-3.27.md). The periodic-stall and finish corrections are covered in [the 3.27.1 hotfix notes](docs/hotfix-3.27.1.md). The latest rendering and skeleton changes are covered in [the 3.27.2 visual notes](docs/visuals-3.27.2.md). Jumper and Strafer corrections and timing limits are covered in [the 3.27.3 movement notes](docs/movement-3.27.3.md).

## Build and release

See [the build and release guide](docs/VORTEX-RELEASES.md) for prerequisites, automatic updates, versioning, checksums, troubleshooting and publishing.

The version lives in `release/config.json`. Publish an incremented version from PowerShell with `./release.ps1 MAJOR.MINOR.PATCH`. The helper builds and verifies locally before committing, tagging and pushing; GitHub Actions produces the official installer.

User settings under `%LOCALAPPDATA%\Vortex` survive upgrades. This release is unsigned. See [LICENSE](LICENSE) and [third-party licenses](licenses/).
