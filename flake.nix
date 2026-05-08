{
  description = "RPi Pico firmware mono-repo - C/C++ and MicroPython projects";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Reusable builder function, passed to each project's package.nix
        buildPicoProject = pkgs.callPackage ./nix/builders/pico-c-project.nix { inherit pkgs; };

        # Helper: load a project's package.nix and pass it the builder
        mkProject = path:
          pkgs.callPackage path { inherit pkgs buildPicoProject; };

      in
      {
        # --- Dev shells ---
        # nix develop .#c          → C/C++ Pico SDK environment
        # nix develop .#micropython → MicroPython environment
        devShells = {
          c           = pkgs.callPackage ./nix/shells/c.nix { inherit pkgs; };
          micropython = pkgs.callPackage ./nix/shells/micropython.nix { inherit pkgs; };

          # Default drops you into the C shell
          default = pkgs.callPackage ./nix/shells/c.nix { inherit pkgs; };
        };

        # --- Project firmware packages ---
        # Add entries here as projects are created:
        # nix build .#my-sensor
        #
        # packages = {
        #   my-sensor = mkProject ./projects/my-sensor/package.nix;
        # };
        packages = {};
      }
    );
}
