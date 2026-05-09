{ pkgs, buildPicoProject }:

let
  # Monorepo root (two levels up from this file)
  repoRoot = ../..;
in
buildPicoProject {
  name  = "bmp180-sensor";
  board = "pico";

  # Include only the project and shared library so the nix sandbox
  # has the relative path ../../libs/bmp180 that CMakeLists.txt uses.
  src = pkgs.lib.fileset.toSource {
    root    = repoRoot;
    fileset = pkgs.lib.fileset.unions [
      (repoRoot + /projects/bmp180-sensor)
      (repoRoot + /libs/i2c0)
      (repoRoot + /libs/bmp180)
    ];
  };

  # Tell the cmake setup hook to configure from the project subdirectory
  # (the hook prepends "../" when it cds into the build dir, giving the
  # correct absolute source path to cmake).
  # The cmake setup hook cds into a build/ subdirectory before calling cmake,
  # so cmakeDir must step back up with ../ to reach the source root first.
  cmakeDir = "../projects/bmp180-sensor";
}
