
# Changelog

## 1.1.0 (2018-05-21)

- [Michał Skuza](https://github.com/skudi/transfolio) added Raspberry Pi Support
## 1.0.1 (2018-11-25)

- Small improvement of error message strings.
- Included new version of inpout32.dll from https://www.highrez.co.uk/Downloads/InpOut32/

## 1.0  (2018-02-18)

### Bugfix

- Prevent endless and meaningless transmission that occurred with the Linux version when one of the source file names was actually a directory.

### New/changed features

- Do not exit immediately if a destination file exists and the -f option is not given. Instead, continue with the next file from the SOURCE list.
- Do not try to transmit files larger than 32 M.

### Cleanup

- Replaced usleep() by nanosleep() for the Linux build.
- Updated included header file for open() function.
- Made some inline functions static.

## 0.9 (2008-11-16)

### Bugfixes

- Windows-version: Increased argument to usleep() in order to have the delay take effect. This issue caused frequent transmission errors on fast computers.
- Fixed checksum evaluation which failed when the result was 0.
- Transmitting zero length files to the Portfolio did not work.
- Display correct file name in "File not found" error message when transmitting a file to the Portfolio.

### New/changed features

- Improved synchronization and error handling for data transmission
- Accept command line switches starting with '/' in addition to the syntax using '-'.
- Allow wildcards for SOURCE and directories for DEST when receiving files (e.g. transfolio -r *.txt stories)

### Cleanup

- Minor changes of error messages, writing to stderr rather than stdout
- Changed exit codes
- Declared getBit() as inline function
- Fixes for DOS port: Use malloc() instead of big payload[] array, include appropriate header for usleep()

## 0.8  (2006-08-28)

- Bugfix for Windows-version: Binary files were treated as text files and got corrupted during transmission

## 0.7

- Enabled receiving of large files (was limited to about 30K before)

## 0.6  

- Enabled sending of large files (was limited to about 30K before)
- Changed maximum path length from 50 to 79
- Minor fixes

## 0.5

- Windows version uses a DLL for port access for Winver > 98.

## 0.4  

- First Windows release, using direct access to I/O ports

## 0.3  

- Added directory list feature

## 0.2  

- Added receiving of files

## 0.1  

- First release, only sending of files
- Klaus Peichl, 2006-01-22