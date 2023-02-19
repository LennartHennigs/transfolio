/*
  Transfolio is a file transfer utility that connects to the Atari Portfolio
  pocket computer over the parallel port. It communicates with the built-in
  file transfer software of the Portfolio.

(c) 2018 by Klaus Peichl (pei@freenet.de)

  Instructions:
  - Adapt the source to select the parallel port access method
    or to change the default port address.
    Linux:
    - Either adapt PPDEV to match your parallel port device.
      This will require the ppdev kernel module.
    - Or undefine PPDEV and adapt DATAPORT to match your parallel port address
      This will require root permissions to run the program.
    Windows:
    - Adapt DATAPORT to match your parallel port address
    - Either get the inpout32.dll library for Win NT/2000/XP (from http://www.logix4u.net)
    - Or define DIRECTIO which will not require any DLL but works
      for Win95 and Win98 only.
  - Compiling for Linux:   cc -O3 transfolio.c -o transfolio
    Compiling for Windows: dmc.exe transfolio.c
  - Start file transfer in server mode on Portfolio
  - Run Transfolio on the PC
    Example for running with root permissions and quoting of a backslash:
    sudo ./transfolio -t config.sys c:\\config.sys

*/

/////////////////////////////////////////////////////////////////

/* #define DIRECTIO */
/* #define RASPIWIRING */

#if !defined(__DMC__) && defined(DIRECTIO) && !defined(RASPIWIRING)
#define PPDEV "/dev/parport0"
#endif

/////////////////////////////////////////////////////////////////

#if defined(DIRECTIO) && defined(RASPIWIRING)
#error "Either use DIRECTIO or RASPIWIRING flag"
#elif defined(DIRECTIO)
#pragma message "Using DIRECTIO flag"
#elif defined(RASPIWIRING)
#pragma message "Using RASPIWIRING flag"
#elif defined(PPDEV)
#pragma message "Using PPDEV flag"
#endif

/////////////////////////////////////////////////////////////////

#define DATAPORT 0x378
#define PAYLOAD_BUFSIZE 60000
#define CONTROL_BUFSIZE 100
#define LIST_BUFSIZE 2000
#define MAX_FILENAME_LEN 79
#define BUFFER_SIZE 0x7000
#define MAX_FILESIZE_MB 2
#define VERSION_NUMBER "1.1.0"

/////////////////////////////////////////////////////////////////

#include <ctype.h> /* tolower */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>  /* printf etc. */
#include <stdlib.h> /* strtol, malloc */
#include <string.h> /* strncpy, strlen */
#include <time.h>   /* usleep / nanosleep */
#if defined(__DMC__)
#include <direct.h> /* chdir */
#else
#include <unistd.h> /* usleep, chdir */
#endif
#include <sys/stat.h> /* fstat */
#if defined(RASPIWIRING)
#include <wiringPi.h>
#endif

#if defined(PPDEV)
#include <fcntl.h> /* open */
#include <time.h>
#if defined(__linux__)
#include <linux/ppdev.h> /* Parallel port device */
#include <sys/ioctl.h>
#endif
#else
#if !defined(__DMC__)
#if defined(__linux__)
#include <sys/io.h> /* Direct port access for Linux */
#endif
#else
#if defined(DIRECTIO)
#include <dos.h> /* Direct port access for DOS */
#else
#include <winbase.h> /* Direct port access for Windows */
#include <windef.h>
HINSTANCE hLib;
/* prototype (function typedef) for DLL function Inp32: */
typedef short _stdcall (*inpfuncPtr)(short portaddr);
typedef void _stdcall (*oupfuncPtr)(short portaddr, short datum);
inpfuncPtr inp32;
oupfuncPtr oup32;
#endif
#endif
#endif

/////////////////////////////////////////////////////////////////

int force = 0;
int sourcecount = 0;

unsigned char *payload;
unsigned char *controlData;
unsigned char *list;

#if defined(PPDEV)
int fd;                             /* File descriptor for opened parallel port */
const char defaultDevice[] = PPDEV; /* May be overridden with the -d option */
#elif defined(RASPIWIRING)
// default GPIO pins
const unsigned int wiringClkOut = 7;  // GPIO07 pin 7
                                      // GND    pin 9
const unsigned int wiringBitOut = 0;  // GPIO00 pin 11
const unsigned int wiringClkIn = 2;   // GPIO02 pin 13
const unsigned int wiringBitIn = 3;   // GPIO03 pin 15
#elif defined(DIRECTIO)
const unsigned short defaultPort = DATAPORT;
unsigned short dataPort;
unsigned short statusPort;
#endif

/////////////////////////////////////////////////////////////////
/* output levels for send and receive block */
typedef enum {
  VERB_QUIET = 0,
  VERB_ERRORS,
  VERB_COUNTER,
  VERB_FLOWCONTROL
} VERBOSITY;

/* data strcuture for sending data, will only work on little endian systems */
#pragma pack(1)
struct transmit_data {
  uint8_t cmd;
  uint16_t buffer_size;
  uint16_t seconds : 5;
  uint16_t minutes : 6;
  uint16_t hours : 5;
  uint16_t day : 5;
  uint16_t month : 4;
  uint16_t year : 7;
  uint32_t fsize;
  char file_path[MAX_FILENAME_LEN];
} transmitInit = {
    .cmd = 3,
    .buffer_size = BUFFER_SIZE};
#pragma pack()

unsigned char receiveInit[82] = {
    0x06,      /* Offset 0: Funktion */
    0x00, 0x70 /* Offset 2: Puffergroesse = 28672 Byte */
               /* Offset 3: Pfad */
};

const unsigned char transmitOverwrite[3] = {0x05, 0x00, 0x70};
const unsigned char transmitCancel[3] = {0x00, 0x00, 0x00};
const unsigned char receiveFinish[3] = {0x20, 0x00, 0x03};

/////////////////////////////////////////////////////////////////
/*
  Opens parallel port. Returns 0 on success
*/
#if defined(PPDEV)
int openPort(const char *device) {
  fd = open(device, O_RDWR);
  if (fd == -1) {
    perror("open");
    fprintf(stderr, "Try 'modprobe ppdev' and 'chmod 666 %s' as root!\n", device);
    return -1;
  }
  fprintf(stderr, "Waiting for %s to become available...\r", device);
  if (ioctl(fd, PPCLAIM)) {
    perror("PPCLAIM");
    close(fd);
    fprintf(stderr, "Try 'fuser -m %s' to see if other processes use the device.\n", device);
    return -1;
  }
  fprintf(stderr, "%s sucessfully opened.               \r", device);

  return fd;
}

#elif defined(RASPIWIRING)
int openPort() {
  if (wiringPiSetup() == -1)
    return -1;
  // configure GPIO pins
  pinMode(wiringClkIn, INPUT);
  pinMode(wiringBitIn, INPUT);
  pinMode(wiringClkOut, OUTPUT);
  pinMode(wiringBitOut, OUTPUT);
  return 0;
}
#elif defined(DIRECTIO)
/*
        Get access to I/O port. Returns 0 on success
*/
int openPort(const unsigned short port) {
  dataPort = port;
  statusPort = port + 1;

#if defined(__DMC__)
#if defined(DIRECTIO)
  return 0;
#else
  hLib = LoadLibrary("inpout32.dll");
  if (hLib == NULL) {
    fprintf(stderr, "INPOUT32.DLL seems to be missing!\n");
    return -1;
  }

  /* get the address of the function */
  inp32 = (inpfuncPtr)GetProcAddress(hLib, "Inp32");
  if (inp32 == NULL) {
    fprintf(stderr, "GetProcAddress for Inp32 Failed.\n");
    return -1;
  }

  oup32 = (oupfuncPtr)GetProcAddress(hLib, "Out32");
  if (oup32 == NULL) {
    fprintf(stderr, "GetProcAddress for Oup32 Failed.\n");
    return -1;
  }
#endif
#else
  return ioperm(dataPort, 3, 255);
#endif
}

#endif

/////////////////////////////////////////////////////////////////
/*
  Reads the status register of the parallel port
*/
static inline unsigned char readPort(void) {
  unsigned char byte = 0;
#if defined(__DMC__)

#if defined(DIRECTIO)
  byte = inp(statusPort);
#else
  byte = (inp32)(statusPort);
#endif

#else

#if defined(PPDEV)
  ioctl(fd, PPRSTATUS, &byte);
#elif defined(RASPIWIRING)
  byte = (digitalRead(wiringClkIn)) << 5 | (digitalRead(wiringBitIn) << 4);
#elif defined(DIRECTIO)
  byte = inb(statusPort);
#endif

#endif
  return byte;
}

/////////////////////////////////////////////////////////////////
/*
  Outputs a byte to the data register of the parallel port
*/
static inline void writePort(const unsigned char byte) {
#if defined(DIRECTIO)
  outb(byte, dataPort);
#elif defined(RASPIWIRING)
  digitalWrite(wiringBitOut, byte & 0x01);
  digitalWrite(wiringClkOut, (byte >> 1) & 0x01);
#elif defined(DIRECTIO)
#if defined(__DMC__)
  ioctl(fd, PPWDATA, &byte);
#else
  (oup32)(dataPort, byte);
#endif
#endif
}

/////////////////////////////////////////////////////////////////

static inline void waitClockHigh(void) {
  unsigned char byte = 0;
  while (!byte) {
    byte = readPort() & 0x20;
  }
}

/////////////////////////////////////////////////////////////////

static inline void waitClockLow(void) {
  unsigned char byte = 1;
  while (byte) {
    byte = readPort() & 0x20;
  }
}

/////////////////////////////////////////////////////////////////

static inline unsigned char getBit(void) {
  return ((readPort() & 0x10) >> 4);
}

/////////////////////////////////////////////////////////////////
/*
  Receives one byte serially, MSB first
  One bit is read on every falling and every rising slope of the clock signal.
*/
unsigned char receiveByte(void) {
  int i;
  unsigned char byte;

  for (i = 0; i < 4; i++) {
    waitClockLow();
    byte = (byte << 1) | getBit();
    writePort(0); /* Clear clock */
    waitClockHigh();
    byte = (byte << 1) | getBit();
    writePort(2); /* Set clock */
  }
  return byte;
}

/////////////////////////////////////////////////////////////////
/*
  Transmits one byte serially, MSB first
  One bit is transmitted on every falling and every rising slope of the clock signal.
*/
void sendByte(unsigned char byte) {
  int i;
  unsigned char b;

#ifdef __DMC__
  /* Should be usleep(50), but smaller arguments than 1000 result in no delay */
  usleep(1000);
#else
  struct timespec t;
  t.tv_sec = 0;
  t.tv_nsec = 50000;
  nanosleep(&t, NULL);
#endif

  for (i = 0; i < 4; i++) {
    b = ((byte & 0x80) >> 7) | 2; /* Output data bit */
    writePort(b);
    b = (byte & 0x80) >> 7; /* Set clock low   */
    writePort(b);

    byte = byte << 1;
    waitClockLow();

    b = (byte & 0x80) >> 7; /* Output data bit */
    writePort(b);
    b = ((byte & 0x80) >> 7) | 2; /* Set clock high  */
    writePort(b);

    byte = byte << 1;
    waitClockHigh();
  }
}

/////////////////////////////////////////////////////////////////
/*
  Transmits a block of data.
  Call int 61h with AX=3002 (open) and AX=3001 (receive) on the Portfolio
*/
void sendBlock(const unsigned char *pData, const unsigned int len, const VERBOSITY verbosity) {
  unsigned char byte;
  unsigned int i;
  unsigned char lenH, lenL;
  unsigned char checksum = 0;

  if (len) {
    byte = receiveByte();
    if (byte == 'Z') {
      if (verbosity >= VERB_FLOWCONTROL) {
        printf("Portfolio ready for receiving.\n");
      }
    } else {
      if (verbosity >= VERB_ERRORS) {
        fprintf(stderr, "Portfolio not ready!\n");
        exit(EXIT_FAILURE);
      }
    }

    usleep(50000);
    sendByte(0x0a5);

    lenH = len >> 8;
    lenL = len & 255;
    sendByte(lenL);
    checksum -= lenL;
    sendByte(lenH);
    checksum -= lenH;

    for (i = 0; i < len; i++) {
      byte = pData[i];
      sendByte(byte);
      checksum -= byte;

      if (verbosity >= VERB_COUNTER)
        printf("Sent %d of %d bytes.\r", i + 1, len);
    }
    sendByte(checksum);

    if (verbosity >= VERB_COUNTER)
      printf("\n");

    byte = receiveByte();

    if (byte == checksum) {
      if (verbosity >= VERB_FLOWCONTROL) {
        fprintf(stderr, "checksum OK\n");
      }
    } else {
      if (verbosity >= VERB_ERRORS) {
        fprintf(stderr, "checksum ERR: %d\n", byte);
        exit(EXIT_FAILURE);
      }
    }
  }
}

/////////////////////////////////////////////////////////////////
/*
  Receives a block of data and returns its length in bytes.
  Call int 61h with AX=3002 (open) and AX=3000 (transmit) on the Portfolio.
*/
int receiveBlock(unsigned char *pData, const unsigned int maxLen, const VERBOSITY verbosity) {
  unsigned int len, i;
  unsigned char lenH, lenL;
  unsigned char checksum = 0;
  unsigned char byte;

  sendByte('Z');

  byte = receiveByte();
  if (byte == 0x0a5) {
    if (verbosity >= VERB_FLOWCONTROL) {
      fprintf(stderr, "Acknowledge OK\n");
    }
  } else {
    if (verbosity >= VERB_ERRORS) {
      fprintf(stderr, "Acknowledge ERROR (received %2X instead of A5)\n", byte);
      exit(EXIT_FAILURE);
    }
  }

  lenL = receiveByte();
  checksum += lenL;
  lenH = receiveByte();
  checksum += lenH;
  len = (lenH << 8) | lenL;

  if (len > maxLen) {
    if (verbosity >= VERB_ERRORS) {
      fprintf(stderr, "Receive buffer too small (%d instead of %d bytes).\n", maxLen, len);
    }
    return 0;
  }

  for (i = 0; i < len; i++) {
    unsigned char byte = receiveByte();
    checksum += byte;
    pData[i] = byte;

    if (verbosity >= VERB_COUNTER)
      printf("Received %d of %d bytes\r", i + 1, len);
  }

  if (verbosity >= VERB_COUNTER)
    printf("\n");

  byte = receiveByte();

  if ((unsigned char)(256 - byte) == checksum) {
    if (verbosity >= VERB_FLOWCONTROL) {
      fprintf(stderr, "checksum OK\n");
    }
  } else {
    if (verbosity >= VERB_ERRORS) {
      fprintf(stderr, "checksum ERR %d %d\n", (unsigned char)(256 - byte), checksum);
      exit(EXIT_FAILURE);
    }
  }

  usleep(100);
  sendByte((unsigned char)(256 - checksum));

  return len;
}

/////////////////////////////////////////////////////////////////
/*
  Returns the file creation time
*/
struct tm *getFileTime(FILE *file) {
  struct stat statbuf;

  if (fstat(fileno(file), &statbuf)) {
    fprintf(stderr, "stat() failed\n");
    exit(EXIT_FAILURE);
  }
  return localtime(&statbuf.st_mtime);
}

/////////////////////////////////////////////////////////////////
/*
  Returns the file size
*/
int getFileSize(FILE *file) {
  int val, len;
  val = fseek(file, 0, SEEK_END);
  if (val != 0) {
    fprintf(stderr, "Seek error!\n");
    exit(EXIT_FAILURE);
  }
  len = ftell(file);
  /* Directories and huge files (>32 MB) are skipped */
  if (len == -1 || len > 32 * 1024 * 1024) {
    return -1;
  }
  val = fseek(file, 0, SEEK_SET);
  if (val != 0) {
    fprintf(stderr, "Seek error!\n");
    exit(EXIT_FAILURE);
  }
  return len;
}

/////////////////////////////////////////////////////////////////
/*
  Reads source file on PC and transmit it to the Portfolio (/t)
*/
void transmitFile(const char *source, const char *dest) {
  FILE *file = fopen(source, "rb");
  int len, blocksize;
  struct transmit_data *tp;
  tp = &transmitInit;

  if (file == NULL) {
    fprintf(stderr, "File not found: %s\n", source);
    exit(EXIT_FAILURE);
  }

  struct tm *modtime = getFileTime(file);
  transmitInit.hours = modtime->tm_hour;
  transmitInit.minutes = modtime->tm_min;
  transmitInit.seconds = modtime->tm_sec;
  transmitInit.year = modtime->tm_year + 1900 - 2000;
  transmitInit.month = modtime->tm_mon + 1;
  transmitInit.day = modtime->tm_mday;

  len = getFileSize(file);
  if (transmitInit.fsize == -1) {
    fprintf(stderr, "Skipping %s.\n", source);
    return;
  }
  transmitInit.fsize = len;
  strncpy((char *)&transmitInit.file_path, dest, MAX_FILENAME_LEN);
  /*
  printf("date: %d-%.2d-%.2d\n", transmitInit.year, transmitInit.month, transmitInit.day);
  printf("time: %.2d:%.2d:%.2d\n", transmitInit.hours, transmitInit.minutes, transmitInit.seconds);
  printf("size: %d\n", transmitInit.fsize);
  printf("file: [%s]\n", transmitInit.file_path);
  */
  sendBlock((char *)tp, sizeof(transmitInit), VERB_ERRORS);
  receiveBlock(controlData, CONTROL_BUFSIZE, VERB_ERRORS);

  if (controlData[0] == 0x10) {
    fprintf(stderr, "Invalid destination file!\n");
    exit(EXIT_FAILURE);
  }

  if (controlData[0] == 0x20) {
    printf("File exists on Portfolio");
    if (force) {
      printf(" and is being overwritten.\n");
      sendBlock(transmitOverwrite, sizeof(transmitOverwrite), VERB_ERRORS);
    } else {
      printf("! Use -f to force overwriting.\n");
      sendBlock(transmitCancel, sizeof(transmitCancel), VERB_ERRORS);
      return; /* proceed to next file */
    }
  }

  blocksize = controlData[1] + (controlData[2] << 8);
  if (blocksize > PAYLOAD_BUFSIZE) {
    fprintf(stderr, "Payload buffer too small!\n");
    exit(EXIT_FAILURE);
  }

  if (len > blocksize) {
    printf("Transmission consists of %d blocks of payload.\n", (len + blocksize - 1) / blocksize);
  }
  int readed;
  while (len > blocksize) {
    readed = fread(payload, sizeof(char), blocksize, file);
    sendBlock(payload, blocksize, VERB_COUNTER);
    len -= blocksize;
  }

  readed = fread(payload, sizeof(char), len, file);
  if (len)
    sendBlock(payload, len, VERB_COUNTER);
  receiveBlock(controlData, CONTROL_BUFSIZE, VERB_ERRORS);

  fclose(file);

  if (controlData[0] != 0x20) {
    fprintf(stderr, "Transmission failed!\nPossibly disk full on Portfolio or directory does not exist.\n");
    exit(EXIT_FAILURE);
  }
  /*
          printf("%.2X%.2X %.2X%.2X%.2X%.2X\n", controlData[1], controlData[2], controlData[3], controlData[4], controlData[5], controlData[6]);
  */
}

/////////////////////////////////////////////////////////////////
/*
  Receives source file(s) from the Portfolio and save it on the PC (/r)
*/
void receiveFile(const char *source, const char *dest) {
  static int nReceivedFiles = 0;
  FILE *file;
  int i, num, len, total;
  int destIsDir = 0;
  int blocksize = BUFFER_SIZE; /* TODO: Check if this is always the same */
  char startdir[256];
  char *namebase;
  char *basename;
  char *pos;

  /* Check if the destination parameter specifies a directory */
  if (!getcwd(startdir, sizeof(startdir))) {
    fprintf(stderr, "Unexpected error: getcwd() failed!\n  %s", dest);
    exit(EXIT_FAILURE);
  }
  if (chdir(dest) == 0) {
    destIsDir = 1;
  }

  /* Get list of matching files */
  receiveInit[0] = 6;
  strncpy((char *)receiveInit + 3, source, MAX_FILENAME_LEN);
  sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);
  receiveBlock((unsigned char *)list, 2000, VERB_ERRORS);

  num = list[0] + (list[1] << 8);

  if (num == 0) {
    printf("File not found on Portfolio: %s\n", source);
    exit(EXIT_FAILURE);
  }

  /* Set up pointer to behind the path where basename shall be appended */
  namebase = (char *)receiveInit + 3;
  pos = strrchr(namebase, ':');
  if (pos) {
    namebase = pos + 1;
  }
  pos = strrchr(namebase, '\\');
  if (pos) {
    namebase = pos + 1;
  }

  basename = (char *)list + 2;

  /* Transfer each file from the list */
  for (i = 1; i <= num; i++) {
    printf("Transferring file %d", nReceivedFiles + i);
    if (sourcecount == 1) {
      /* We know the total number of files only if a single source item
               has been specified (potentially using wildcards). */
      printf(" of %d", num);
    }
    printf(": %s\n", basename);

    if (destIsDir)
      dest = basename;

    /* Check if destination file exists */
    file = fopen(dest, "rb");
    if (file != NULL) {
      fclose(file);
      if (!force) {
        printf("File exists! Use -f to force overwriting.\n");
        if (i < num)
          printf("Remaining files are not copied!\n");
        exit(EXIT_FAILURE);
      }
    }

    /* Open destination file */
    file = fopen(dest, "wb");
    if (file == NULL) {
      fprintf(stderr, "Cannot create file: %s\n", dest);
      exit(EXIT_FAILURE);
    }

    /* Request Portfolio to send file */
    receiveInit[0] = 2;
    strncpy(namebase, basename, MAX_FILENAME_LEN);
    sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);

    /* Get file length information */
    receiveBlock(controlData, CONTROL_BUFSIZE, VERB_ERRORS);

    if (controlData[0] != 0x20) {
      fprintf(stderr, "Unknown protocol error! \n");
      exit(EXIT_FAILURE);
    }

    total = controlData[7] + ((int)controlData[8] << 8) + ((int)controlData[9] << 16);

    if (total > blocksize) {
      printf("Transmission consists of %d blocks of payload.\n", (total + blocksize - 1) / blocksize);
    }

    /* Receive and save actual payload */
    while (total > 0) {
      len = receiveBlock(payload, PAYLOAD_BUFSIZE, VERB_COUNTER);
      fwrite(payload, 1, len, file);
      total -= len;
    }

    /* Close connection and destination file */
    sendBlock(receiveFinish, sizeof(receiveFinish), VERB_ERRORS);
    fclose(file);

    basename += strlen(basename) + 1;
/*
// !!! NEW
    struct stat statBuf;
    struct utimbuf newTimes;
    struct tm modTime;

    if (stat(dest, &statBuf) < 0) {
      fprintf(stderr, "Failed to get timestamp for file %s\n", dest);
      exit(EXIT_FAILURE);
    }
    newTimes.actime = statBuf.st_atime;   // keep atime unchanged
    int val = (controlData[4] << 8) + controlData[3];
    modTime.tm_hour = (val >> 11) & 0x1f;
    modTime.tm_min = (val >> 5) & 0x3f;
    modTime.tm_sec = (val << 1) & 0x3e;

    val = (controlData[6] << 8) + controlData[5];
    modTime.tm_year = ((val >> 9) & 0x7f) - 1900 + 1980;
    modTime.tm_mon = ((val >> 5) & 0x0f) - 1;
    modTime.tm_mday = val & 0x1f;

    modTime.tm_isdst = -1;

    newTimes.modtime = mktime(&modTime);

    if (utime(dest, &newTimes) < 0) {
      fprintf(stderr, "Failed to set timestamp for file %s\n", dest);
      exit(EXIT_FAILURE);
    }
  }
// !!! NEW END
*/

  /* Change back to original directory */
  if (destIsDir) {
    if (chdir(startdir) != 0) {
      fprintf(stderr, "Unexpected error: chdirs(%s) failed!\n", startdir);
      exit(EXIT_FAILURE);
    }
  }
  nReceivedFiles += num;
}

/////////////////////////////////////////////////////////////////
/*
  Gets directory listing from the Portfolio and display it (/l)
*/
void listFiles(const char *pattern) {
  int i, num;
  char *name;

  printf("Fetching directory listing for %s\n", pattern);

  strncpy((char *)receiveInit + 3, pattern, MAX_FILENAME_LEN);
  sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);
  receiveBlock(payload, PAYLOAD_BUFSIZE, VERB_ERRORS);

  num = payload[0] + (payload[1] << 8);
  if (num == 0)
    printf("No files.\n");

  name = (char *)payload + 2;

  for (i = 0; i < num; i++) {
    printf("%s\n", name);
    name += strlen(name) + 1;
  }
}

/////////////////////////////////////////////////////////////////
/*
    Assembles full destination path and name if only the destination directory is given.
    The current source file name is appended to the destination directory and modified
    to fulfill the (most important) DOS file naming restrictions.
*/
void composePofoName(char *source, char *dest, char *pofoName, int sourcecount) {
  char *pos;
  char *ext;
  char lastChar;

  /* Exchange Slash by Backslash (Unix path -> DOS path) */
  while ((pos = strchr(dest, '/'))) {
    *pos = '\\';
  }

  strncpy(pofoName, dest, MAX_FILENAME_LEN);

  lastChar = pofoName[strlen(pofoName) - 1];

  if (sourcecount > 1 || lastChar == '\\' || lastChar == ':') {
    /* "dest" is a directory. */
    int len;

    /* Append Backslash: */
    if (lastChar != '\\')
      strncat(pofoName, "\\", MAX_FILENAME_LEN - strlen(pofoName));

    /* Skip path part in source: */
    pos = strrchr(source, '/');
    if (!pos)
      pos = strrchr(source, '\\');
    if (pos)
      source = pos + 1;

    ext = strrchr(source, '.');
    if (ext) {
      /* Replace dots before extension by underscores */
      while ((pos = strchr(source, '.')) != ext) {
        *pos = '_';
      }

      /* Append file name without extension: */
      len = ext - source;
      if (len > 8)
        len = 8;
      if (len > MAX_FILENAME_LEN - strlen(pofoName))
        len = MAX_FILENAME_LEN - strlen(pofoName);
      strncat(pofoName, source, len);

      /* Append file name extension */
      len = 4;
      if (len > MAX_FILENAME_LEN - strlen(pofoName))
        len = MAX_FILENAME_LEN - strlen(pofoName);
      strncat(pofoName, ext, len);
    } else {
      /* There is no extension */
      len = 8;
      if (len > MAX_FILENAME_LEN - strlen(pofoName))
        len = MAX_FILENAME_LEN - strlen(pofoName);
      strncat(pofoName, source, len);
    }
  }
}

/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
#if defined(PPDEV)
  const char *device = defaultDevice;
#elif defined(RASPIWIRING)
  // TODO?
#elif defined(DIRECTIO)
  unsigned short port = defaultPort;
#endif
  char **sourcelist = NULL;
  char *dest = NULL;
  unsigned char byte;
  char mode = 'h';
  int i, j;

  printf("Transfolio %s - (c) 2018 by Klaus Peichl\n", VERSION_NUMBER);
  /* Command line parsing: Get source, destination, mode and the force flag */
  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-'
#if defined(__DMC__)
        || argv[i][0] == '/'
#endif
    ) {
      /* Command line switch */

      int optLen = strlen(argv[i]);
      if (optLen < 2 || optLen > 3) {
        mode = 'h';
        break;
      }
      for (j = 1; j < optLen; j++) {
        char letter = tolower(argv[i][j]);

        switch (letter) {
          case 't':
          case 'r':
          case 'l':
            mode = letter;
            break;
          case 'f':
            force = 1;
            break;
#if defined(PPDEV)
          case 'd':
            device = NULL; /* the next argument is used as the device name */
            break;
#elif defined(RASPIWIRING)
            // TODO: param for wired: pin list
#elif defined(DIRECTIO)
          case 'p':
            port = 0; /* the next argument is used as the port address */
            break;
#endif
          default:
            mode = 'h';
        }
      }
    } else {
      /* Command line argument */
#if defined(PPDEV)
      if (!device) {
        device = argv[i];
      } else
#elif defined(RASPIWIRING)
      // TODO: parse pin list for wired
#elif defined(DIRECTIO)
      if (!port) {
        char *endptr;
        port = strtol(argv[i], &endptr, 0);
      } else
#endif
          if (!sourcelist) {
        sourcelist = argv + i;
        sourcecount = 1;
      } else {
        if (dest || mode == 'l') {
          /* The argument to which dest was set before is actually part of the source list */
          sourcecount++;
        }
        dest = argv[i];
      }
    }
  }

  /*
          Show help screen in case of an invalid command line
  */
  if ((mode == 'h') ||
      (mode == 't' && dest == NULL) ||
      (mode == 'r' && dest == NULL) ||
      (mode == 'l' && sourcelist == NULL)) {
    printf(
        "\nSyntax: %s "
#if defined(PPDEV)
        "[-d DEVICE] "
#elif defined(RASPIWIRING)
    // TODO: param for wired: pin list
#else
        "[-p ADR] "
#endif
        "[-f] {-t|-r} SOURCE DEST \n",
        argv[0]);
    printf(
        "  or    %s "
#if defined(PPDEV)
        "[-d DEVICE] "
#elif defined(RASPIWIRING)
    // TODO: param for wired: pin list
#else
        "[-p ADR] "
#endif
        "-l PATTERN \n\n",
        argv[0]);
    printf("-t  Transmit file(s) to Portfolio.\n");
    printf("    Wildcards are not directly supported but may be expanded\n");
    printf("    by the shell to generate a list of source files.\n");
    printf("-r  Receive file(s) from Portfolio.\n");
    printf("    Wildcards in SOURCE are evaluated by the Portfolio.\n");
    printf("    In a Unix like shell, quoting is required.\n");
    printf("-l  List directory files on Portfolio matching PATTERN \n");
    printf("-f  Force overwriting an existing file \n");
#if defined(PPDEV)
    printf("-d  Select parallel port device (default: %s) \n", defaultDevice);
#elif defined(RASPIWIRING)
    // TODO: param for wired: pin list
#elif defined(DIRECTIO)
    printf("-p  Select parallel port address (default: 0x%x) \n", defaultPort);
#endif
    printf("\nNotes:\n");
    printf("- SOURCE may be a single file or a list of files.\n");
    printf("  In the latter case, DEST specifies a directory.\n");
    printf("- The Portfolio must be in server mode when running this program!\n");
    exit(EXIT_FAILURE);
  }

  /*
          Memory allocation
  */
  payload = malloc(PAYLOAD_BUFSIZE);
  controlData = malloc(CONTROL_BUFSIZE);
  list = malloc(LIST_BUFSIZE);

  if (payload == NULL || controlData == NULL || list == NULL) {
    fprintf(stderr, "Out of memory!\n");
    exit(EXIT_FAILURE);
  }

  /*
          Open the parallel port
  */
  if (openPort(
#if defined(PPDEV)
          device
#elif defined(RASPIWIRING)
  // TODO: pin list for wired (struct)
#elif defined(DIRECTIO)
          port
#endif
          ) == -1) {
    fprintf(stderr, "Cannot open parallel port!\n");
    exit(EXIT_FAILURE);
  }

  /*
          Wait for Portfolio to enter server mode
  */
  fprintf(stderr, "Waiting for Portfolio...                           \r");
  writePort(2);
  waitClockHigh();
  byte = receiveByte();
  /* synchronization */
  while (byte != 90) {
    waitClockLow();
    writePort(0);
    waitClockHigh();
    writePort(2);
    byte = receiveByte();
  }

  /*
          Call subroutine depending on the mode of operation
  */
  for (i = 0; i < sourcecount; i++)
    switch (mode) {
      case 't': {
        char pofoName[MAX_FILENAME_LEN + 1];
        composePofoName(sourcelist[i], dest, pofoName, sourcecount);
        printf("Transmitting file %d of %d: %s -> %s\n", i + 1, sourcecount, sourcelist[i], pofoName);
        transmitFile(sourcelist[i], pofoName);
        break;
      }
      case 'r':
        receiveFile(sourcelist[i], dest);
        break;
      case 'l':
        listFiles(sourcelist[i]);
        break;
    }

#if defined(PPDEV)
  /*
          Close the parallel port device
  */
  ioctl(fd, PPRELEASE);
  close(fd);
#endif

#if defined(RASPIWIRING)
  pinMode(wiringBitOut, INPUT);
  pinMode(wiringClkOut, INPUT);
#elif defined(__DMC__) && !defined(DIRECTIO)
  FreeLibrary(hLib);
#endif
  return (0);
}

/////////////////////////////////////////////////////////////////
