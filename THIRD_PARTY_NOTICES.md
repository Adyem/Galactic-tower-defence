# Third-party notices

## SDL2

Tower Ascend uses SDL2 for the platform window, input, audio, and optional haptic
interfaces. The supported baseline is SDL2 2.0.20 or newer, selected through the
CMake package or `pkg-config`. SDL2 is distributed under the zlib license; obtain
the corresponding license text from the SDL2 distribution used for a release.

The project does not bundle SDL2 binaries in source control. Release builds must
record the resolved SDL2 version and preserve its license text alongside the
packaged executable.
