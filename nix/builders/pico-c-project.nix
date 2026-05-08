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
    pico-sdk
  ] ++ extraBuildInputs;

  cmakeFlags = [
    "-DPICO_BOARD=${board}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DPICO_TOOLCHAIN_PATH=${pkgs.gcc-arm-embedded}"
  ] ++ extraCmakeFlags;

  # nixpkgs pico-sdk installs to $out/lib/pico-sdk/ — set both paths explicitly.
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
