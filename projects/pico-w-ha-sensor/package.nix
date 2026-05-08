{ pkgs, buildPicoProject }:

let
  repoRoot = ../..;
in
buildPicoProject {
  name  = "pico-w-ha-sensor";
  board = "pico_w";

  src = pkgs.lib.fileset.toSource {
    root    = repoRoot;
    fileset = pkgs.lib.fileset.unions [
      (repoRoot + /projects/pico-w-ha-sensor)
      (repoRoot + /libs/bmp180)
      (repoRoot + /libs/ina219)
    ];
  };

  cmakeDir = "projects/pico-w-ha-sensor";
}
