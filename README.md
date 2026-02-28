# captdriver

Alternative open-source CUPS filter driver for Canon CAPT laser printers.

## Supported Printers

| Model                   | Status       |
|-------------------------|--------------|
| LBP2900                 | Works        |
| LBP3000                 | Experimental |
| LBP3010 / LBP3018 / LBP3050 | Works   |
| LBP3100 / LBP3108 / LBP3150 | Experimental |
| LBP6000 / LBP6018      | Works |

## Building

### Prerequisites

- C99 compiler (GCC or Clang)
- CUPS development libraries (`libcups2-dev` / `cups-devel`)
- GNU Autotools (`autoconf`, `automake`)

### From Source

```sh
aclocal
autoconf
automake --add-missing
./configure
make
make ppd
```

### Install

```sh
sudo make install
sudo cp /usr/local/bin/rastertocapt "$(cups-config --serverbin)/filter/"
sudo lpadmin -p PRINTER_NAME -v PRINTER_URI -P PPD_FILE -E
sudo lpadmin -d PRINTER_NAME
```

Replace `PRINTER_NAME`, `PRINTER_URI`, and `PPD_FILE` with your values.
Generated PPD files are in the `ppd/` directory after `make ppd`.

The `rastertocapt` binary must be owned by root with read-and-execute
permissions for other users, or CUPS will refuse to run it.

### Nix

Build and install via the included flake:

```sh
nix build
```

On NixOS, enable the module in your configuration:

```nix
{
  inputs.captdriver.url = "github:cryptoluks/captdriver";

  # In your system configuration:
  imports = [ captdriver.nixosModules.default ];
  services.captdriver.enable = true;
}
```

This makes the CUPS filter and PPD files available automatically.

## Protocol Documentation

See [SPECS](SPECS) for the reverse-engineered Canon CAPT protocol documentation,
including the A0-command protocol and Hi-SCoA compression algorithm.

## License

GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>

This is unofficial software, not endorsed by Canon Inc.
