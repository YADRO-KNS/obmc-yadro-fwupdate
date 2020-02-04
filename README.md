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

## Customization of the update procedure
**NOTE:** This functional is not implemented yet in new (C++ based) version.

The update procedure can be customized in runtime using special scripts
placed inside the firmware package:
* `obmc.update` - tar archive for customizing OpenBMC update procedure;
* `obmc.update.sig` - digital signature of `obmc.update` archive;
* `opfw.update` - tar archive for customizing OpenPOWER update procedure;
* `opfw.update.sig` - digital signature of `bpmc.update` archive;

Each archive can contain one or both executable modules (scripts):
* `preinstall` - used before writing new firmware image;
* `postinstall` - used after writing new firmware image (OpenPOWER firmware
  only).

During call to the `preinstall` module next parameters are passed:
1. Path to the new firmware image file;
2. Update type (`full` for normal update or `clean` if fwupdate was called
   with option `--reset`);
3. Interactive mode (`interactive` or `silent` if fwupdate was called with
   option `--yes`).

Possible exit codes from `preinstall` script:
* `0` - successful, the update procedure can continue;
* `114 (EALREADY)` - successful, the update procedure has been already
  completed by the script;
* Any other values - fail, the update procedure must be stopped.
Exit codes from `postinstall` are ignored.


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
