# Transfolio

Transfolio is a file transfer utility, by Klaus Peichl, that connects a computer to the Atari Portfolio pocket computer over the parallel port. It communicates with the built-in file transfer software of the Portfolio.

The full documentation is currently only available in German language at [www.pofowiki.de](http://www.pofowiki.de/doku.php?id=software:vorstellung:exchanges:transfolio)

**Note**: Please see the [CHANGELOG](https://github.com/LennartHennigs/transfolio/blob/master/CHANGELOG.md) for the latest updates.

## Usage

``` bash
  Syntax: ./transfolio [-d DEVICE] [-f] {-t|-r} SOURCE DEST
    or    ./transfolio [-d DEVICE] -l PATTERN

  -t  Transmit file to Portfolio.
      Wildcards are not directly supported but may be expanded
      by the shell to generate a list of source files.
  -r  Receive file(s) from Portfolio.
      Wildcards in SOURCE are evaluated by the Portfolio.
      In a Unix like shell, quoting is required.
  -l  List directory files on Portfolio matching PATTERN
  -f  Force overwriting an existing file
  -d  Select parallel port device (default: /dev/parport0)

  Notes:
  - SOURCE may be a single file or a list of files.
    In the latter case, DEST specifies a directory.
  - The Portfolio must be in server mode when running this program!
```

## Building it (WiP)

### Raspberry Pi

``` bash
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install git git-core
sudo apt install wiringpi=2.50
git clone https://github.com/LennartHennigs/transfolio.git
cd transfolio
make rpfolio
chmod +x rpfolio
sudo cp rpfolio /usr/local/bin/
cd
sudo rm -r transfolio
```

![Pi Wiring](https://i0.wp.com/lennarthennigs.de/wp-content/uploads/2022/03/pi-parallel.png?resize=768%2C615&ssl=1)
