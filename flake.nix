{
  description = "illus - a lightweight, stateless package manager for user space";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "armv7l-linux"
        "aarch64-darwin"
      ];

      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      version = "1.3.0";

      mkIllus = pkgs: pkgs.stdenv.mkDerivation {
        pname = "illus";
        inherit version;
        src = ./.;

        # Plain Makefile project: no configure step.
        dontConfigure = true;

        # Local `make` leaves a binary in the tree; path: flakes copy it in, so
        # force a fresh compile (a foreign binary has the wrong ELF interpreter
        # on NixOS).
        preBuild = ''
          make clean
        '';

        makeFlags = [ "PREFIX=${placeholder "out"}" ];
        installFlags = [ "PREFIX=${placeholder "out"}" ];

        meta = with nixpkgs.lib; {
          description = "A lightweight, stateless C package manager for user space";
          homepage = "https://github.com/campbell-nick/Dreams";
          platforms = platforms.unix;
          mainProgram = "illus";
        };
      };
    in
    {
      packages = forAllSystems (pkgs: {
        default = mkIllus pkgs;
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            gcc
            gnumake
          ];
        };
      });

      formatter = forAllSystems (pkgs: pkgs.nixpkgs-fmt);
    };
}
