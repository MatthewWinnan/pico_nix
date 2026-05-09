{ pkgs, buildPicoProject }:

let
  repoRoot = ../..;
in
buildPicoProject {
  name  = "pico-w-air-sensor";
  board = "pico_w";

  src = pkgs.lib.fileset.toSource {
    root    = repoRoot;
    fileset = pkgs.lib.fileset.unions [
      (repoRoot + /projects/pico-w-air-sensor)
      (repoRoot + /libs/pmsa003)
      (repoRoot + /libs/ssd1306)
    ];
  };

  cmakeDir = "../projects/pico-w-air-sensor";
}
