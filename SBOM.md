# Source SBOM

**Initial source snapshot, not a release report.** Source commit `c91aa4b04c2bb24210af8ffa5c351abfb2f47961` predates the tooling commit. Future release reports use exact published tags.

Source repository inventory only; completeness is UNKNOWN. Syft inventories tracked source/package manifests. Gitlinks and declared vendored/model evidence are inspected at immutable pins. CMake declarations are conditional requirements, not resolved installations. This is NOT proof of operating-system packages, static-link closure, build-time resolution, or the contents of release binaries.

Generation: **success**

## Source identity

wave-owl/mercury `c91aa4b04c2bb24210af8ffa5c351abfb2f47961`

- sbom:role: source-root
- sbom:git-commit: c91aa4b04c2bb24210af8ffa5c351abfb2f47961
- sbom:git-tree: c9291ec713209ff64cdcbdcd4e272172cadc9cf6
- sbom:source-repository: https://github.com/wave-owl/mercury
- sbom:release-tags: []
- sbom:license-evidence: [{"path": "LICENSE", "sha256": "8486a10c4393cee1c25392769ddd3b2d6c242d6ec7928e1414efff7dfb2f07ef"}]
- sbom:license-resolution: unresolved; manual review required

## Generation errors

None.

## Components and unresolved licenses

| Component | Version | Role | License |
| --- | --- | --- | --- |
| wave-owl/mercury | c91aa4b04c2bb24210af8ffa5c351abfb2f47961 | source-root | unresolved; manual review required |
| Boost | unresolved | conditional-build-requirement | unresolved |
| CppUnit | unresolved | conditional-build-requirement | unresolved |
| Doxygen | unresolved | conditional-build-requirement | unresolved |
| Gnuradio | unresolved | conditional-build-requirement | unresolved |
| Log4cpp | unresolved | conditional-build-requirement | unresolved |
| PythonInterp | unresolved | conditional-build-requirement | unresolved |
| PythonLibs | unresolved | conditional-build-requirement | unresolved |
| Doxygen | unresolved | conditional-build-requirement | unresolved |
| SWIG | unresolved | conditional-build-requirement | unresolved |
| PythonLibs | unresolved | conditional-build-requirement | unresolved |
| Vulkan | unresolved | conditional-build-requirement | unresolved |
| glfw3 | unresolved | conditional-build-requirement | unresolved |
| Dawn | unresolved | conditional-build-requirement | unresolved |
| SDL2 | unresolved | conditional-build-requirement | unresolved |
| Threads | unresolved | conditional-build-requirement | unresolved |
| Dawn | unresolved | conditional-build-requirement | unresolved |
| SDL3 | unresolved | conditional-build-requirement | unresolved |
| Threads | unresolved | conditional-build-requirement | unresolved |
| Dawn | unresolved | conditional-build-requirement | unresolved |
| source/audioio/ffaudio | unresolved | declared-vendored-source | unresolved; manual review required |
| source/audioio/ffbase | unresolved | declared-vendored-source | unresolved; manual review required |
| tests/gr-hermes_ofdm/lib | unresolved | declared-vendored-source | unresolved; manual review required |
| third_party | unresolved | declared-vendored-source | unresolved; manual review required |
| Conditional source requirements declared by Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by examples/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by source/audioio/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/apps/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/docs/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/docs/doxygen/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/grc/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/include/hermes_ofdm/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/lib/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/python/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by tests/gr-hermes_ofdm/swig/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_android_opengl3/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_android_opengl3/android/app/build.gradle | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_android_opengl3/android/build.gradle | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_apple_metal/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_apple_opengl2/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_glfw_metal/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_glfw_opengl2/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_glfw_opengl3/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_glfw_vulkan/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_glfw_vulkan/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_glfw_wgpu/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_glut_opengl2/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_null/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl2_metal/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl2_opengl2/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl2_opengl3/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl2_sdlrenderer2/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl2_vulkan/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl2_wgpu/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl3_metal/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl3_opengl3/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl3_sdlgpu3/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl3_sdlrenderer3/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl3_vulkan/Makefile | unresolved | conditional-build-runtime-requirement | unresolved |
| Conditional source requirements declared by third_party/imgui/examples/example_sdl3_wgpu/CMakeLists.txt | unresolved | conditional-build-runtime-requirement | unresolved |
| Platform audio backends (ALSA/PulseAudio/CoreAudio/Windows) | unresolved | conditional-build-runtime-requirement | unresolved |
| Threads | unresolved | conditional-build-runtime-requirement | unresolved |
| GUI optional GLFW/OpenGL | unresolved | conditional-build-runtime-requirement | unresolved |
| tests/gr-hermes_ofdm GNU Radio/Boost/Python test dependencies | unresolved | conditional-build-runtime-requirement | unresolved |
| GLFW | 3.4.0 | Syft source/package discovery | unresolved |
| actions/checkout | v6 | Syft source/package discovery | unresolved |
| actions/checkout | v6 | Syft source/package discovery | unresolved |
| /Users/gerolfziegenhain/src/wave-owl/local/sbom-upstream-results/mercury/.source-input/third_party/glfw/lib/glfw3.dll | unresolved | Syft source/package discovery | unresolved |
| /Users/gerolfziegenhain/src/wave-owl/local/sbom-upstream-results/mercury/.source-input/third_party/imgui/.github/workflows/build.yml | unresolved | Syft source/package discovery | unresolved |
| /Users/gerolfziegenhain/src/wave-owl/local/sbom-upstream-results/mercury/.source-input/third_party/imgui/.github/workflows/static-analysis.yml | unresolved | Syft source/package discovery | unresolved |

License evidence paths/hashes, source manifests, model hashes and conditional
CMake declarations are recorded in SBOM.cdx.json. Declared upstream pins do not
prove vendored equivalence. No dependency installation or build was performed.
