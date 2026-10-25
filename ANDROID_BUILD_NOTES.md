# Android ARM64 build notes

The workflow builds an Android `arm64-v8a` UCI executable using the Android NDK Clang compiler and `-march=armv8-a+simd` (ARM NEON). Sliding attacks use hardware BMI2 PEXT/PDEP on compatible x86 builds and portable software fallbacks on ARM.

**Important network limitation:** the supplied ZIP does not contain `source/nets/net13.nnue`. If the repository also lacks that exact compatible network, the workflow deliberately compiles without an embedded net and the engine falls back to its handcrafted evaluation. This is buildable, but it is not the same strength as the published NNUE build. Add the original compatible `nets/net13.nnue` to `source/nets/` (if you have the right file) to embed it. Do not substitute a network from another engine.

The GitHub Actions artifact contains a Linux-hosted Android ARM64 executable, not an APK. Download the artifact and import the binary into a UCI Android chess GUI that supports external engines.
