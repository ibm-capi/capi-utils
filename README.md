# capi-flash-script

Usage: `sudo capi-flash-script.sh <path-to-bit-file>`

This script can be used to flash a specific card in a system with one or more CAPI cards installed.

The benefits of using this script instead of directly using the `capi-flash` binaries are threefold;
  
1. This script will write some information to `/var/cxl/card#` whenever someone flashes a new image. This information will be displayed the next time someone wants to flash a new image to one of the cards, making it easier for people to share the cards.

2. This script will read the PSL revision from the card and matches it to one of the items in the `psl-revisions` file. This makes it easier for people to target the right card when multiple cards of different vendors are present in one system.

3. This script will take care of the reset required to use the new image.

Please note that the `capi-flash` binaries should be located in the same directory as this script and should be named according to the following naming convention; `capi-flash-XXXX` where `XXXX` is the PSL revision as listed in `psl-revisions`.
