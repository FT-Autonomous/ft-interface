{
  description = "Convert Markdown files to DOCX with rendered Mermaid diagrams";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.stdenv.mkDerivation {
            pname = "markdown-to-docx";
            version = "1.0.0";
            src = ./.;

            nativeBuildInputs = [ pkgs.makeWrapper ];

            dontConfigure = true;

            buildPhase = ''
              runHook preBuild
              $CXX -std=c++17 -O2 -Wall -Wextra -Wpedantic \
                md_to_docx.cpp -o md-to-docx
              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall
              mkdir -p "$out/bin"
              install -m755 md-to-docx "$out/bin/md-to-docx"
              wrapProgram "$out/bin/md-to-docx" \
                --prefix PATH : ${pkgs.lib.makeBinPath [
                  pkgs.pandoc
                  pkgs.mermaid-filter
                ]} \
                --set-default MERMAID_FILTER_SCALE 2
              runHook postInstall
            '';

            meta = {
              description = "Recursive Markdown to DOCX converter";
              mainProgram = "md-to-docx";
              platforms = pkgs.lib.platforms.linux;
            };
          };
        }
      );

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/md-to-docx";
        };
      });
    };
}
