# capi-utils

This package contains useful utilities for CAPI adapters.

Install: `sudo make install`

The default install point for capi-utils is `/usr/local/lib`.

Uninstall: `sudo make uninstall`

# capi-flash-script

Usage: `sudo capi-flash-script <path-to-bin-file>`

This script can be used to flash a specific card in a system with one or more CAPI cards installed.

There are three benefits from using this script rather than calling the `capi-flash` binaries directly;

1. This script will write some information to `/var/cxl/card#` whenever someone flashes a new image. This information will be displayed the next time someone wants to flash a new image to one of the cards, making it easier for people to share the cards. To quickly check if your image is still loaded onto the card use: `cat /var/cxl/card#` where `#` is the card you want to use.

2. This script will read the PSL revision from the card and matches it to one of the items in the `psl-devices` file. This makes it easier for people to target the right card when multiple cards of different vendors are present in one system.

3. This script will take care of the reset required to use the new image.

Please note that the `capi-flash` binaries should be located in the installation directory (/usr/local/lib/capi-utils by default) and should be named according to the following naming convention; `capi-flash-XXXX` where `XXXX` is the board vendor as listed in `psl-devices`.

# capi_reset

Usage: 
```
sudo capi-reset <CARD_ID>                   -- Reset Card # to factory region (default)
sudo capi-reset                             -- Reset all of the CAPI cards in the system
sudo capi-reset <CARD_ID>  user|factory     -- Reset Card # to user or factory region
```

Usually you *don't* need this script. Because the above capi-flash-script already includes this reset step. But if your AFU doesn't exit correctly and leave the FPGA into an unknown state, you can use this script alone to reset FPGA chip, and avoid programming the flash again.

`CARD_ID` is a single digit like 0, 1, 2, 3. Check `/var/cxl/card#` to know which card your are operating at. If `CARD_ID` is not assigned, the script will reset all of the cards in the system.

# Acknowledgements


This project was forked from: https://github.com/mbrobbel/capi-flash-script

