# Vortex build, releases and updates

The permanent public repository is https://github.com/vortex-release/Vortex. Vortex is Windows x64 C++, built with Visual Studio 2022, CMake and Windows SDK. Velopack 1.2.0 remains the installer/update engine; the installation identity remains Joshu.Vortex.

## Publish a release

From the project directory in PowerShell:

```powershell
./release.ps1 3.23.3
```

Review your changes and update release/notes.md first. The helper checks the main branch and origin, validates the version, rejects existing local/remote tags and GitHub releases, and audits eligible files for credentials and build garbage. It updates the central version, builds x64 Release, runs tests, builds/verifies the installer, commits, creates an annotated tag, and atomically pushes main plus the tag. A failed local build cannot publish. Existing tags/releases are never overwritten. If a push fails, inspect the local commit/tag before retrying; the script will deliberately refuse to replace an existing tag.

For an unpublished version, validation without committing/tagging/pushing is available:

```powershell
./release.ps1 3.23.2 -ValidateOnly
```

Validation restores the original version file afterwards and prints the artifacts directory. Choose a newer unpublished version once 3.23.2 is released. GitHub CLI is needed only on the developer machine. Its one-time web authorization stores credentials in the OS credential store. Vortex users never need to authenticate with GitHub.

## Central version and builds

release/config.json is authoritative. CMake uses its version for runtime AppVersion, executable version resources, package manifests and build information. Conflicting overrides are rejected. Versions contain three numeric components, each 0 through 65535, with no leading zeroes. Tags prepend v. Historical release notes are not a second version source.

Install Git, Visual Studio 2022 C++ tools, Windows SDK, CMake and .NET 8 runtime on a developer machine. .NET is only needed by the packager; the shipped application is native and embeds its MSVC runtime. Required ImGui, MinHook, Velopack C SDK and nlohmann/json dependencies are pinned and vendored with licenses. Unused SDK architectures/static libraries are excluded from git.

```powershell
./scripts/setup-release-tools.ps1
./build.ps1 -NoDeploy -BuildDirectory "$env:LOCALAPPDATA\Vortex\Build"
```

The setup script reuses installed tooling or retrieves the pinned official Velopack archive and verifies its SHA-256 before extraction. The release helper and CI use the same build/package scripts. Tests use fixtures and hidden Direct3D/WARP windows; they do not launch or control a live CS2 match.

## GitHub Actions

Flow: developer commit/tag -> GitHub -> Windows Actions runner -> Release build/tests -> installer -> verified draft upload -> public release -> installed Vortex update notification.

.github/workflows/release.yml triggers on v* tags, runs on windows-2022, sets up .NET 8, checks repository/tag/version agreement, then builds and tests before packaging. It uploads four assets:

- VortexSetup.exe: first installation and manual upgrades.
- Joshu.Vortex-VERSION-full.nupkg: managed update payload.
- releases.win.json: identity, version, sizes and package checksums.
- SHA256SUMS.txt: installer, package and feed SHA-256 hashes.

The built-in GITHUB_TOKEN is scoped to the publishing step; contents: write is granted only to the release job. Build failures prevent release creation. Uploads go to a draft first; missing/mismatched assets prevent publication. Reruns refuse an existing release, including a draft. Inspect any failed draft before a deliberate recovery, and never move a published tag.

Inspect build progress and failure logs at https://github.com/vortex-release/Vortex/actions. Releases are at https://github.com/vortex-release/Vortex/releases. Logs contain the failed command and test details. Fix the cause before publishing a new version. The helper prints the workflow page after GitHub accepts the tag.

## Automatic update behavior

Installed Vortex asynchronously checks https://api.github.com/repos/vortex-release/Vortex/releases/latest on startup and every six hours. Manual checks are limited to one per minute per instance. Numeric comparison correctly orders 1.0.10 after 1.0.9; older/equal versions, drafts and prereleases do not cause upgrades. Requests use a Vortex/version User-Agent and GitHub API headers, with no credentials, cookies or authentication provider.

Assets are resolved from browser_download_url and restricted to the configured repository and tag. Installer, package, feed and checksum assets must exist. HTTPS redirects are bounded and restricted to GitHub asset hosts, with Windows TLS verification enabled. Timeouts, size bounds and cancellation checks limit network work. Downloads use unique partial files and verify HTTP status, exact length, SHA-256 and disk flush before atomic finalization. Failed partial files are cleaned up. Signed redirect URLs and credentials are not logged.

The dedicated update architecture is preserved: initial installation uses VortexSetup.exe; in-app upgrades download the verified full nupkg instead of a redundant bootstrap installer. The Updates page shows current and available versions, download progress, and Install and restart. Users can defer by continuing to use the app. Velopack's separate Update.exe waits for Vortex to exit, replaces files, then restarts it. Vortex never copies over its running executable. The per-user installation needs no privilege workarounds. Replacement is blocked while CS2 or the preview holds application DLLs; these processes are never killed.

Preferences, named profiles and logs stay under %LOCALAPPDATA%\Vortex, separate from replaceable installed files. Portable/development copies ask for installation once. Pre-GitHub builds with an empty update URL also need VortexSetup.exe once; later releases are detected automatically. Offline, DNS, TLS, HTTP, malformed-response and download failures leave Vortex usable. Closing the launcher cancels a transfer; cleanup can take up to a bounded network timeout.

## Signing and diagnostics

This first public release is unsigned. SHA-256 verifies integrity but does not replace a publisher signing certificate. Existing -SignParams and -AzureTrustedSignFile packaging options remain available; signing credentials must stay outside the repository.

Update logs: %LOCALAPPDATA%\Vortex\logs\Vortex.log, rotated at 2 MiB. The old loopback update server remains available for development and is unnecessary for GitHub releases.

The audio-device check is reported as skipped when Windows exposes no playback device (as on hosted CI). Audio decoding, cancellation and error reporting still run everywhere. Local hardware playback remains tested.
