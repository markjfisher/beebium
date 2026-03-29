# SCSI Test Assets

## BBCHDDs.zip

22 blank, pre-formatted ADFS hard disc images (2 MB to 512 MB).

Source: Jon Ripley's BBC Micro Hard Drives page
        https://jonripley.com/8bit/HardDrives/
        Direct download: https://jonripley.com/8bit/HardDrives/BBCHDDs.zip

These are raw ADFS old-map format images (256-byte sectors, "Hugo" root
directory marker) with an empty root directory and free space map. The
archive contains nested zip files, each containing a single `.adl` image
file.

## scsi0.dat / scsi0.dsc

BeebEm ADFS-formatted SCSI hard disc image (9.7 MB) with sample files.
Used for existing C++ integration tests (test_scsi_adfs_boot, etc.).
