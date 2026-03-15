#define BROTHER_COLOR_TEXT 1
#define BROTHER_COLOR_ERRDIF 2
#define BROTHER_COLOR_GRAY64 4
#define BROTHER_COLOR_C256 8
#define BROTHER_COLOR_CGRAY 0x10
#define BROTHER_COLOR_PAL 0x20

#define BROTHER_COLOR_MODE_GRAY "GRAY64"
#define BROTHER_COLOR_MODE_RGB "CGRAY"
#define BROTHER_COLOR_MODE_BW "TEXT"

#define BROTHER_USB 1
#define BROTHER_PARPORT 2

/* data returned when querying MFC functionality */
struct brother_info {
  unsigned char magic[2];
  unsigned char size;
  unsigned char res1;
  unsigned char signalType;
  unsigned char colorType;
  unsigned char ntsc[2];
  unsigned char pal[2];
  unsigned char secam[2];
  unsigned char hwType;
  unsigned char hwVersion;
  unsigned char dpi;
  unsigned char res2;
  unsigned char res3[256];
};


#define BLACK_WHITE_STR	SANE_I18N("Black & White")
#define GRAY_STR        SANE_I18N("Grayscale")
#define COLOR_STR       SANE_I18N("Color")

#define MM_PER_INCH     25.4
#define mmToIlu(mm) (((mm) * dev->x_resolution) / MM_PER_INCH)
#define iluToMm(ilu) (((ilu) * MM_PER_INCH) / dev->x_resolution)

enum BrotherOption
{
  /* Must come first */
  OPT_NUM_OPTS = 0,
  OPT_MODE_GROUP,
  OPT_MODE,
  OPT_SOURCE,
  OPT_RESOLUTION,                 /* X and Y resolution */
  OPT_GEOMETRY_GROUP,
  OPT_TL_X,			/* upper left X */
  OPT_TL_Y,			/* upper left Y */
  OPT_BR_X,			/* bottom right X */
  OPT_BR_Y,			/* bottom right Y */
  OPT_PREVIEW,
  OPT_NUM_OPTIONS
};

/* Option_Value may already be defined by sanei_backend.h */
#ifndef SANE_OPTION
typedef union
{
  SANE_Word w;                  /* word */
  SANE_Word *wa;                /* word array */
  SANE_String s;                /* string */
} Option_Value;
#define SANE_OPTION 1
#endif


struct BrotherMFC {
  struct BrotherMFC *next;

  SANE_Device sane;

  int scanning;
  int startscan;
  int port;

  int x_resolution;
  int y_resolution;
  int x_tl;
  int y_tl;
  int x_br;
  int y_br;
  int width;
  int length;
  unsigned int interleave;
  const char *colormode;
  FILE *logfile;

  unsigned char readbuf[32767];
  unsigned int readi;
  unsigned int readlen;

  int scan_lines;
  time_t last_data_time;  /* tracks last successful data read for ADF timeout */
  int adf_done;           /* set when ADF has no more pages (0x80 received) */
  int page_count;         /* page counter for ADF multi-page scans */

  struct brother_info devcap;

  char * devicename;

  /*struct usb_dev_handle * usb;*/
  SANE_Int husb;

  SANE_Parameters params;
  SANE_Option_Descriptor opt[OPT_NUM_OPTIONS];
  Option_Value val[OPT_NUM_OPTIONS];
};

typedef struct BrotherMFC BrotherMFC;

