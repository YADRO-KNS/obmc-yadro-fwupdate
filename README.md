# YADRO Firmware Update tool

Script fwupdate.py is used to:
* write OpenBMC and OpenPOWER firmware images to appropriate flash devices;
* restore manufacture default state (reset options: clear PNOR flash
  partitions and BMC RW image);
* get information about installed firmwares version.

## Firmware package (distributive)
Firmware package MUST contain files:
* `MANIFEST` - Contains metadata about this bundle
* `MANIFEST.sig` - Signature of MANIFEST made by the system private key
* `publickey` - Public key generated from the image specific private key
* `publickey.sig` - Signature of publickey made by the system private key
* `image-bmc` - image of OpenBMC firmware
* `image-bmc.sig` - Signature of OpenBMC image made by the image private key
* `vesnin.pnor` - image of OpenPOWER firmware
* `vesnin.pnor.sig` - Signature of OpenPOWER image made by the image private
  key

## Digital signature
Digital signature files are created during a build process. Signature is a
binary file with SHA256 digest signed with a private key.

The system's private key is stored on a build server, corresponded public key
presents on the BMC filesystem at path `/etc/acitvationdata/yadro/publickey`.
_TODO: Store public key inside the EEPROM (BMC's VPD)_

The upstream services support the bundle specific key pair. This possibility
is also implemented here. But for simplifying, we are using the same key pair
in both cases.

Example command to create signature file:
`openssl dgst -sha256 -sign privatekey -out image-bmc.sig image-bmc`

## Downgrade
To perform downgrade firmware to version without signature use option
`--no-sign` (root only, this option is not available in `klish`).
