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
      (repoRoot + /libs/i2c0)
      (repoRoot + /libs/i2c1)
      (repoRoot + /libs/bmp180)
      (repoRoot + /libs/bme280)
      (repoRoot + /libs/ina219)
    ];
  };

  # The cmake setup hook cds into a build/ subdirectory before calling cmake,
  # so cmakeDir must step back up with ../ to reach the source root first.
  cmakeDir = "../projects/pico-w-ha-sensor";
}
