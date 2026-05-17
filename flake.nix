{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    stef = {
      url = "github:devnull-cz/stef";
      flake = false;
    };
    prgc = {
      url = "github:devnull-cz/c-prog-lang";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      stef,
      prgc,
    }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      tests = "${prgc}/getting-credits/2026/tests/";

      patchedStef = pkgs.runCommand "patched-stef" { } ''
        mkdir -p $out/bin
        cp ${stef}/stef.sh $out/bin/stef
        chmod +x $out/bin/stef

        substituteInPlace $out/bin/stef \
          --replace /bin/ls ${pkgs.coreutils}/bin/ls

        patchShebangs $out/bin/stef
      '';

      patchedMktmp = pkgs.writeShellApplication {
        name = "mktmp";
        runtimeInputs = [ pkgs.coreutils ];
        text = ''
          set -euo pipefail
          tmpdir="''${TMPDIR:-/tmp}"
          exec ${pkgs.coreutils}/bin/mktemp -d "$tmpdir/dir.XXXXXX"
        '';
      };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          clang-tools
          gcc
          valgrind
          gdb
          openssl
          gnutar
          bash
          coreutils
          patchedMktmp
        ];

        NIX_NO_SELF_RPATH = true;

        shellHook = ''
          export STEF=${patchedStef}/bin/stef
          # export MYTAR_C=${toString ./.}/mytar.c
          export GNUTAR=${pkgs.gnutar}/bin/tar
          export TESTS=${tests}
          export TMPDIR=$(mktemp -d)

          alias test-test='cd "$TESTS" && MYGNUTAR=${pkgs.gnutar}/bin/tar ${pkgs.bash}/bin/bash -c "./run-tests.sh $(cat phase-1.tests)"'
        '';
      };
    };
}
