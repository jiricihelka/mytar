{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in {
      devShells.x86_64-linux.default = pkgs.mkShell {
        packages = with pkgs; [ clang-tools gcc valgrind gdb ];
        NIX_NO_SELF_RPATH = true; # Hack for https://github.com/NixOS/nixpkgs/issues/177952
      };
    };
}
