# YADRO Firmware Update tool

Script fwupdate.py is used for:
* write OpenBMC and OpenPOWER firmware images to appropriate flash devices;
* restore manufacture default state (reset options: clear PNOR flash
  partitions and BMC RW image);
* get information about installed firmwares version.

## Firmware package (distributive)
Firmware package MUST contain files:
* `image-bmc` - image of OpenBMC firmware
* `image-bmc.digest` - digital signature of OpenBMC image
* `vesnin.pnor` - image of OpenPOWER firmware
* `vesnin.pnor.digest` - digital signature of OpenPOWER image

## Digital signature
Digital signature files are created during build process. Signature is a
binary file with SHA256 digest signed with private key. Private key is stored
on a build server, public key MUST be placed to the BMC filesystem at path
`/etc/yadro.public.pem`.
_TODO: Store public key inside the EEPROM (BMC's VPD)_
Example command to create digest file:
`openssl dgst -sha256 -sign privatekey.pem -out image-bmc.digest image-bmc`

## Downgrade
To perform downgrade firmware to version without signature use option
`--no-sign` (root only, this option is not available in `klish`).
