# Vortex validation — 2026-09-11

Final build: Vortex 3.15.0, Windows x64 Release, MSVC 19.44, Velopack C API/packager 1.2.0.

- All 33 CTest tests passed, including the existing overlay, graphics, camera, API, offset, settings, and build-script tests.
- New loader integration test loaded an inert fixture DLL into an owned x64 host, observed its entry point, rejected stale PID metadata and malformed files, detected a duplicate load, and verified update blocking while a host mapped the DLL.
- After correcting a startup race, the loader test passed 30 consecutive runs.
- Profile migration test verified existing choices, custom asset copying and resolution, current packaged defaults, preservation of subsequent edits, and an unchanged original profile.
- HTTP server tests verified feed/HEAD/download-page responses, rejected traversal paths and source files, and rejected write methods.
- All three launcher pages were rendered through D3D11 and inspected. The desktop inspection runtime timed out, so visual verification used screenshots captured directly from the app renderer.
- Installed the 3.15.0 test package into an isolated directory. The client reported up to date against its initial loopback feed.
- Published 3.15.1 to the PC's loopback server. The installed client detected and downloaded it, then applied it. The installed executable's ProductVersion changed to 3.15.1, and a user-data sentinel remained intact.
- With the server stopped, the client reported an update-service error and still passed its launcher smoke test.
- Uninstalled the isolated test copy successfully.
- The final 3.15.0 preview was rebuilt without the loopback test URL. It uses the same updater implementation, with no public endpoint configured yet.

Limits: No live CS2 injection was performed; DLL loading was verified using a controlled host. Public internet delivery needs a stable HTTPS tunnel/hostname. The initial installer is unsigned because no publisher signing identity was supplied; signing options are implemented in the packaging script.

An upstream Velopack 1.2.0 setup argument-parser issue occurred only when forwarding extra executable arguments after '--'. Retrying the documented silent install without forwarded arguments succeeded. The user-facing install and release commands do not forward executable arguments.

Final setup installed Vortex 3.15.0 at %LOCALAPPDATA%\Joshu.Vortex. The local download server at http://127.0.0.1:17843 returned HTTP 200 and advertised only the final 3.15.0 release. The Desktop installer SHA-256 matched the server copy.
