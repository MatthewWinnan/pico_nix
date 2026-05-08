# Reusable builder for C/C++ Pico SDK projects.
# Usage in a project's package.nix:
#
#   { pkgs, buildPicoProject }:
#   buildPicoProject {
#     name = "my-sensor";
#     src = ./.;
#     # board defaults to "pico" (RP2040). Use "pico2" for RP2350.
#     board = "pico";
#     # extraBuildInputs for any additional C libraries
#     extraBuildInputs = [];
#   }

# pico-sdk is passed from the flake with withSubmodules = true (includes TinyUSB).
{ pkgs, pico-sdk }:

{ name
, src
, board ? "pico"
, extraCmakeFlags ? []
, extraBuildInputs ? []
, meta ? {}
}:

let
  # Select the cmake toolchain file based on chip family.
  # RP2040 (pico, pico_w, ...) → Cortex-M0+
  # RP2350 (pico2, pico2_w, ...) → Cortex-M33
  toolchainFile =
    if builtins.elem board [ "pico2" "pico2_w" ]
    then "${pico-sdk}/lib/pico-sdk/cmake/preload/toolchains/pico_arm_cortex_m33_gcc.cmake"
    else "${pico-sdk}/lib/pico-sdk/cmake/preload/toolchains/pico_arm_cortex_m0plus_gcc.cmake";
in

pkgs.stdenv.mkDerivation {
  pname = name;
  version = "0.1.0";
  inherit src;

  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    gcc-arm-embedded
    pioasm
    picotool  # pico-sdk 2.x uses picotool for .uf2 generation; must be pre-installed
    python3   # pico-sdk build scripts need python
  ];

  buildInputs = [
    pico-sdk
  ] ++ extraBuildInputs;

  cmakeFlags = [
    "-DCMAKE_TOOLCHAIN_FILE=${toolchainFile}"
    "-DPICO_BOARD=${board}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DPICO_TOOLCHAIN_PATH=${pkgs.gcc-arm-embedded}"
    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
    # The nix cmake setup hook passes -DCMAKE_C_COMPILER= and -DCMAKE_CXX_COMPILER=
    # (empty strings) derived from the stdenv CC/CXX env vars. cmake command-line
    # -D flags lock the cache — the pico-sdk toolchain file cannot override them.
    # By placing our explicit paths here (appended after the nix hook flags),
    # they become the last -D for these variables and win.
    "-DCMAKE_C_COMPILER=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-gcc"
    "-DCMAKE_CXX_COMPILER=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-g++"
    "-DCMAKE_ASM_COMPILER=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-gcc"
    # The pico-sdk toolchain sets CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY, which
    # restricts find_package() to the ARM sysroot. picotool lives in the nix
    # store, outside that root, so find_package(picotool) fails and the sdk falls
    # back to FetchContent (no network in sandbox). picotool_DIR bypasses the
    # search entirely and points cmake straight at the installed config file.
    "-Dpicotool_DIR=${pkgs.picotool}/lib/cmake/picotool"
  ] ++ extraCmakeFlags;

  preConfigure = ''
    export PICO_SDK_PATH="${pico-sdk}/lib/pico-sdk"
    export PICO_TOOLCHAIN_PATH="${pkgs.gcc-arm-embedded}"
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    # Copy all build artifacts: .uf2 (drag-drop flash), .elf (openocd/picotool), .bin
    find . -maxdepth 2 \( -name "*.uf2" -o -name "*.elf" -o -name "*.bin" \) \
      -exec cp {} $out/ \;
    runHook postInstall
  '';

  inherit meta;
}
