Velopack C API 1.2.0, downloaded from the official tagged release.
https://github.com/velopack/velopack/releases/tag/1.2.0

SDK archive SHA-256:
547262ED7A1AB1FF62F580AA53851EDE2F1A451AC61B8974EB7BC01117488835

License: ../../licenses/Velopack-LICENSE.txt
The Windows x64 import library expects the DLL to be named velopack_libc.dll at runtime.
The CMake post-build step copies the architecture-specific download using that runtime name.
The versioned C API is used directly; online C++ wrapper examples differ from this release's header.
