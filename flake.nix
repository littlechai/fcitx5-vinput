{
  description = "Offline voice input addon for Fcitx5";

  inputs.nixpkgs.url = "github:littlechai/nixpkgs/sherpa-onnx-1.12.31";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system}.default = pkgs.callPackage ./nix/package.nix { };

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ self.packages.${system}.default ];
      };
    };
}
