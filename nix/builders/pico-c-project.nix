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

{ pkgs }:

{ name
, src
, board ? "pico"
, extraCmakeFlags ? []
, extraBuildInputs ? []
, meta ? {}
}:

pkgs.stdenv.mkDerivation {
  pname = name;
  version = "0.1.0";
  inherit src;

  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    gcc-arm-embedded
    pioasm
    python3   # pico-sdk build scripts need python
  ];

  buildInputs = [
    pkgs.pico-sdk
  ] ++ extraBuildInputs;

  cmakeFlags = [
    "-DPICO_BOARD=${board}"
    "-DCMAKE_BUILD_TYPE=Release"
    # Point cmake at the ARM toolchain
    "-DPICO_TOOLCHAIN_PATH=${pkgs.gcc-arm-embedded}"
  ] ++ extraCmakeFlags;

  # pico-sdk setup hook sets PICO_SDK_PATH; cmake picks it up via
  # pico_sdk_import.cmake which the SDK ships.
  preConfigure = ''
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
