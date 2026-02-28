{
  description = "Open-source driver for Canon CAPT laser printers (LBP2900, LBP3000, LBP3010, LBP3050, LBP6000, etc.)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages = rec {
          captdriver = pkgs.stdenv.mkDerivation {
            pname = "captdriver";
            version = "0.1.4.1-improved";

            src = self;

            nativeBuildInputs = with pkgs; [
              autoreconfHook
              pkg-config
            ];

            buildInputs = with pkgs; [
              cups
            ];

            preConfigure = ''
              # cups-config must be found in PATH
              export PATH="${pkgs.cups}/bin:$PATH"
            '';

            postBuild = ''
              # Generate PPD files from drv source
              LC_ALL=C ${pkgs.cups}/bin/ppdc \
                -I ${pkgs.cups}/share/cups/ppdc \
                src/canon-lbp.drv -d ./ppd
            '';

            installPhase = ''
              runHook preInstall

              # Install the CUPS filter binary
              install -Dm755 src/rastertocapt $out/lib/cups/filter/rastertocapt

              # Install PPD files directly into model/ (no subdirectory)
              # so CUPS cups-driverd can find them regardless of printer config
              if [ -d ppd ]; then
                for ppd in ppd/*.ppd; do
                  install -Dm644 "$ppd" "$out/share/cups/model/$(basename "$ppd")"
                done
              fi

              # Install drv source file for ppdc
              install -Dm644 src/canon-lbp.drv $out/share/cups/drv/captdriver/canon-lbp.drv

              # Install documentation
              install -Dm644 README.md $out/share/doc/captdriver/README.md
              install -Dm644 SPECS $out/share/doc/captdriver/SPECS

              runHook postInstall
            '';

            meta = with pkgs.lib; {
              description = "Open-source driver for Canon CAPT laser printers";
              longDescription = ''
                Captdriver is an open-source alternative driver for Canon laser
                printers that use the proprietary CAPT protocol. Supported models
                include the LBP2900, LBP3000, LBP3010/3018/3050,
                LBP3100/3108/3150, and LBP6000/6018.
              '';
              homepage = "https://github.com/mounaiban/captdriver";
              license = licenses.gpl3Plus;
              platforms = platforms.linux;
              maintainers = [];
            };
          };

          default = captdriver;
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.captdriver ];
          packages = with pkgs; [
            gdb
            valgrind
          ];
        };
      }
    ) // {
      # NixOS module for easy printer setup
      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.services.captdriver;
          captdriver = self.packages.${pkgs.system}.captdriver;
        in
        {
          options.services.captdriver = {
            enable = lib.mkEnableOption "Canon CAPT printer driver (captdriver)";
          };

          config = lib.mkIf cfg.enable {
            # Make the filter and PPDs available to CUPS
            services.printing = {
              enable = true;
              drivers = [ captdriver ];
            };
          };
        };
    };
}
