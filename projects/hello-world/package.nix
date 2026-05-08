{ pkgs, buildPicoProject }:

buildPicoProject {
  name = "hello-world";
  src = ./.;
  board = "pico";
}
