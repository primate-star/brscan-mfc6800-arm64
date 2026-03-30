/* sane - Scanner Access Now Easy.

   This file (C) 2003 Fernando Trias

   This is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   Thi is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with sane; see the file COPYING.  If not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   This file implements a SANE backend for Brother MFC multifunction 
   scanner/fax/printer

History:

20 Feb 2003   FT   Initial version
 1 Mar 2003   FT   1. Read input into temp buffer in order to match
                      up color lines and send at least one complete
                      line at a time back to the frontend.
                   2. Add alignment and rounding to coordinates. 24-bit
                      color must be rounded to even pixels. 8-bit gray
                      must be rounded to nearest nibble (0xNNN0).
                   3. Add sending control sequence at start of each scan.
		   4. Create device_read/write fxn to incorporate future
		      parallel port code.
 3 Apr 2003   FT   Clean up code and distribute to brother-mfc mailing 
                   list.

Credits:

This driver would not have been possible without the investigative
work of Dmitri Katchalov (dmitrik@users.sourceforge.net), who deciphered
the protocol used to communicate with the MFC. He also researched the
parallel port interface, which may be added to this driver at some
point in the future.

Also, Franklin Meng (fmeng@users.sourceforge.net) provided a lot of raw
data for the USB protocol, as well as discovering how to fax with the
MFC.

http://www.sourceforge.net/projects/brother-mfc

*/

#define BUILD 1

#include "../include/sane/config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../include/sane/sane.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_usb.h"
#include "../include/sane/sanei_pio.h"
#include "../include/sane/sanei_config.h"

#define BACKEND_NAME brother
#define BROTHER_CONFIG_FILE "brother.conf"

#include "../include/sane/sanei_backend.h"

#include "brother.h"

static BrotherMFC *first_dev = NULL;
static const SANE_Device **devlist = 0;
static int num_devices = 0;

#define DBG_error   1

/*****************************************/

static const SANE_Int dpi_list[] =
  {4, 100, 200, 300, 600};

static SANE_String_Const scan_mode_list[] = {
  COLOR_STR,
  GRAY_STR,
  BLACK_WHITE_STR,
  NULL
};

#define FLATBED_STR     SANE_I18N("Flatbed")
#define ADF_STR         SANE_I18N("ADF")

static SANE_String_Const scan_source_list[] = {
  FLATBED_STR,
  ADF_STR,
  NULL
};

static SANE_Range x_range;
static SANE_Range y_range;

/*****************************************/

SANE_Status device_write(BrotherMFC *dev, const SANE_Byte *buf, size_t *len);
SANE_Status device_read(BrotherMFC *dev, SANE_Byte *buf, size_t *len);
SANE_Status device_poll_ready(BrotherMFC *dev);
BrotherMFC *find_dev(SANE_String_Const devname);
SANE_Status attach_one_usb(SANE_String_Const devname);
SANE_Status attach_one_device(SANE_String_Const devname, int dtype);
SANE_Status send_scan_command(BrotherMFC *dev);
int process_buffer(BrotherMFC *dev, SANE_Byte *buf, SANE_Int maxlen);

/* Returns the length of the longest string, including the terminating
 * character. */
static size_t
max_string_size (SANE_String_Const strings[])
{
  size_t size, max_size = 0;
  int i;

  for (i = 0; strings[i]; ++i) {
    size = strlen (strings[i]) + 1;
    if (size > max_size) {
      max_size = size;
    }
  }

  return max_size;
}

/* Write data to a device - this function should be used instead
 * of writing directly to the device in order to automatically
 * support either USB or parallel port
 */
SANE_Status device_write(BrotherMFC *dev, const SANE_Byte *buf, size_t *len)
{
  SANE_Status status;
  if (dev->port == BROTHER_USB) {
    status = sanei_usb_write_bulk(dev->husb, buf, len);
    return status;
  }
  else if (dev->port == BROTHER_PARPORT) {
    return SANE_STATUS_INVAL;
  }
  return SANE_STATUS_INVAL;
}

SANE_Status device_read(BrotherMFC *dev, SANE_Byte *buf, size_t *len)
{
  SANE_Status status;
  if (dev->port == BROTHER_USB) {
    status = sanei_usb_read_bulk(dev->husb, buf, len);
    return status;
  }
  else if (dev->port == BROTHER_PARPORT) {
    return SANE_STATUS_INVAL;
  }
  return SANE_STATUS_INVAL;
}

/* Poll interrupt endpoint EP5 (0x85) to check if scanner has data ready.
 * The MFC-6800 uses this to signal scan data availability.
 * Returns SANE_STATUS_GOOD when data is ready, or status indicating wait/error.
 */
SANE_Status device_poll_ready(BrotherMFC *dev)
{
  /* Skip interrupt polling - MFC-6800 interrupt endpoint doesn't
   * reliably indicate data readiness. Just proceed with bulk reads. */
  (void)dev;  /* Unused */
  return SANE_STATUS_GOOD;
}

BrotherMFC *
find_dev(SANE_String_Const devname)
{
  BrotherMFC *dev;
  for(dev = first_dev; dev; dev = dev->next) {
    if (strcmp(dev->sane.name, devname)==0) {
      return dev;
    }
  }
  return NULL;
}

SANE_Status 
attach_one_usb(SANE_String_Const devname)
{
  return attach_one_device(devname, BROTHER_USB);
}

SANE_Status 
attach_one_device(SANE_String_Const devname, int dtype)
{
  BrotherMFC *dev;
  
  dev = find_dev(devname);
  if (dev) {
    return SANE_STATUS_GOOD;
  }

  dev = malloc(sizeof(BrotherMFC));
  if (dev == NULL) {
    DBG (DBG_error, "ERROR: not enough memory\n");
    return SANE_STATUS_NO_MEM;
  }

  memset(dev, 0, sizeof(BrotherMFC));
  
  dev->devicename = strdup(devname);
  dev->sane.name = dev->devicename;
  dev->sane.vendor = "Brother";
  dev->sane.model = "MFC";
  dev->sane.type = "multi-function peripheral";

  dev->port = dtype;

  dev->next = first_dev;
  first_dev = dev;

  num_devices++;

  return SANE_STATUS_GOOD;
}


SANE_Status
sane_init (SANE_Int *vc, SANE_Auth_Callback cb)
{
  char str[1024];
  char usb[256];
  FILE * fp;

  DBG_INIT();

  /* all MFC devices seem to have the same code */
  strcpy(usb, "usb 0x4f9 0x111");

  DBG( 1, "brother.c: test debug msg\n");
  
  fp = sanei_config_open (BROTHER_CONFIG_FILE);
  
  if (fp) {
    while (sanei_config_read (str, sizeof (str), fp)) {
      if (strncmp(str, "usb", 3)==0) {
	strcpy(usb, str);
      }
    }
    fclose(fp);
  }

  cb = cb;

  /* try USB devices */
  sanei_usb_init();
  sanei_usb_attach_matching_devices(usb, attach_one_usb);
  
  if (num_devices==0) {
    /* TODO: no USB, so try the parallel port */
  }
  
  if (vc) {
    *vc = SANE_VERSION_CODE (V_MAJOR, V_MINOR, BUILD);
  }
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_devices (const SANE_Device ***dl, SANE_Bool local)
{
  BrotherMFC *dev;
  int i;
  
  local = local;
  
  if (devlist) free(devlist);
  
  devlist = malloc((num_devices + 1) * sizeof(devlist[0]));
  if (!devlist) {
    DBG( 1, "out of memory (line %d)\n", __LINE__);
    return SANE_STATUS_NO_MEM;
  }
  
  i = 0;
  
  for (dev = first_dev; i < num_devices; dev = dev->next) {
    devlist[i++] = &dev->sane;
  }
  
  devlist[i++] = 0;
  *dl = devlist;
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_open (SANE_String_Const name, SANE_Handle *h)
{
  unsigned int i;
  char cap[] = "\x1bQ\n\x80";
  char bx[0xff];
  size_t len, len2;
  BrotherMFC *dev;
  SANE_Status status;

  DBG(1, "sane_open: name='%s'\n", name);

  if (name[0]) {
    dev = find_dev(name);
    if (!dev) {
      DBG(1, "sane_open: find_dev failed, trying attach\n");
      status = attach_one_usb(name);
      if (status != SANE_STATUS_GOOD) return status;
      dev = find_dev(name);
    }
  }
  else {
    dev = first_dev;
  }

  if (!dev) {
    DBG (DBG_error, "No scanner available\n");
    return SANE_STATUS_INVAL;
  }
  
  /* save the handle */
  *h = dev;
  
  if (dev->port == BROTHER_USB) {
    status = sanei_usb_open(name, &dev->husb);
    if (status != SANE_STATUS_GOOD) {
      return status;
    }

    /* Initial control message - non-fatal, some devices don't support it */
    len = sizeof(bx);
    memset(bx, 0, sizeof(bx));
    status = sanei_usb_control_msg(dev->husb, 0xc0, 1, 2, 0,
				   len, (unsigned char*)bx);
    if (status != SANE_STATUS_GOOD) {
      DBG(2, "sane_open: control msg failed (status=%d), continuing\n", status);
    }

    /* Brief pause to let scanner settle */
    usleep(500000);
  }
  else if (dev->port == BROTHER_PARPORT) {
    /* TODO: initialize the par port */
  }

  /* Skip capability query - use known values for MFC-6800 */
  memset(&dev->devcap, 0, sizeof(dev->devcap));
  dev->devcap.magic[0] = 0xC1;  /* Valid magic byte */
  
  dev->scanning = 0;
  dev->page_count = 0;
  dev->startscan = 0;

  memset (dev->opt, 0, sizeof (dev->opt));
  memset (dev->val, 0, sizeof (dev->val));
  
  for (i = 0; i < OPT_NUM_OPTIONS; ++i) {
    dev->opt[i].size = sizeof (SANE_Word);
    dev->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  }
  
  x_range.min = SANE_FIX (0);
  x_range.max = SANE_FIX (8.5 * MM_PER_INCH);
  x_range.quant = 0;
  y_range.min = SANE_FIX (0);
  y_range.max = SANE_FIX (11.5 * MM_PER_INCH);
  y_range.quant = 0;

  /* Number of options. */
  dev->opt[OPT_NUM_OPTS].name = "";
  dev->opt[OPT_NUM_OPTS].title = SANE_TITLE_NUM_OPTIONS;
  dev->opt[OPT_NUM_OPTS].desc = SANE_DESC_NUM_OPTIONS;
  dev->opt[OPT_NUM_OPTS].type = SANE_TYPE_INT;
  dev->opt[OPT_NUM_OPTS].cap = SANE_CAP_SOFT_DETECT;
  dev->val[OPT_NUM_OPTS].w = OPT_NUM_OPTIONS;
  
  /* Mode group */
  dev->opt[OPT_MODE_GROUP].title = SANE_I18N ("Scan mode");
  dev->opt[OPT_MODE_GROUP].desc = "";   /* not valid for a group */
  dev->opt[OPT_MODE_GROUP].type = SANE_TYPE_GROUP;
  dev->opt[OPT_MODE_GROUP].cap = 0;
  dev->opt[OPT_MODE_GROUP].size = 0;
  dev->opt[OPT_MODE_GROUP].constraint_type = SANE_CONSTRAINT_NONE;
  
  /* Scanner supported modes */
  dev->opt[OPT_MODE].name = SANE_NAME_SCAN_MODE;
  dev->opt[OPT_MODE].title = SANE_TITLE_SCAN_MODE;
  dev->opt[OPT_MODE].desc = SANE_DESC_SCAN_MODE;
  dev->opt[OPT_MODE].type = SANE_TYPE_STRING;
  dev->opt[OPT_MODE].size = max_string_size (scan_mode_list);
  dev->opt[OPT_MODE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  dev->opt[OPT_MODE].constraint.string_list = scan_mode_list;
  dev->val[OPT_MODE].s = (SANE_Char *) strdup (scan_mode_list[0]);

  /* Scan source (Flatbed / ADF) */
  dev->opt[OPT_SOURCE].name = SANE_NAME_SCAN_SOURCE;
  dev->opt[OPT_SOURCE].title = SANE_TITLE_SCAN_SOURCE;
  dev->opt[OPT_SOURCE].desc = SANE_DESC_SCAN_SOURCE;
  dev->opt[OPT_SOURCE].type = SANE_TYPE_STRING;
  dev->opt[OPT_SOURCE].size = max_string_size (scan_source_list);
  dev->opt[OPT_SOURCE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  dev->opt[OPT_SOURCE].constraint.string_list = scan_source_list;
  dev->val[OPT_SOURCE].s = (SANE_Char *) strdup (scan_source_list[0]);

  /* X and Y resolution */
  dev->opt[OPT_RESOLUTION].name = SANE_NAME_SCAN_RESOLUTION;
  dev->opt[OPT_RESOLUTION].title = SANE_TITLE_SCAN_RESOLUTION;
  dev->opt[OPT_RESOLUTION].desc = SANE_DESC_SCAN_RESOLUTION;
  dev->opt[OPT_RESOLUTION].type = SANE_TYPE_INT;
  dev->opt[OPT_RESOLUTION].unit = SANE_UNIT_DPI;
  dev->opt[OPT_RESOLUTION].constraint_type = SANE_CONSTRAINT_WORD_LIST;
  dev->opt[OPT_RESOLUTION].constraint.word_list = dpi_list;
  dev->val[OPT_RESOLUTION].w = dpi_list[1];
  
  /* Geometry group */
  dev->opt[OPT_GEOMETRY_GROUP].title = SANE_I18N ("Geometry");
  dev->opt[OPT_GEOMETRY_GROUP].desc = "";	/* not valid for a group */
  dev->opt[OPT_GEOMETRY_GROUP].type = SANE_TYPE_GROUP;
  dev->opt[OPT_GEOMETRY_GROUP].cap = SANE_CAP_ADVANCED;
  dev->opt[OPT_GEOMETRY_GROUP].size = 0;
  dev->opt[OPT_GEOMETRY_GROUP].constraint_type = SANE_CONSTRAINT_NONE;
  
  /* Upper left X */
  dev->opt[OPT_TL_X].name = SANE_NAME_SCAN_TL_X;
  dev->opt[OPT_TL_X].title = SANE_TITLE_SCAN_TL_X;
  dev->opt[OPT_TL_X].desc = SANE_DESC_SCAN_TL_X;
  dev->opt[OPT_TL_X].type = SANE_TYPE_FIXED;
  dev->opt[OPT_TL_X].unit = SANE_UNIT_MM;
  dev->opt[OPT_TL_X].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->opt[OPT_TL_X].constraint.range = &x_range;
  dev->val[OPT_TL_X].w = x_range.min;
  
  /* Upper left Y */
  dev->opt[OPT_TL_Y].name = SANE_NAME_SCAN_TL_Y;
  dev->opt[OPT_TL_Y].title = SANE_TITLE_SCAN_TL_Y;
  dev->opt[OPT_TL_Y].desc = SANE_DESC_SCAN_TL_Y;
  dev->opt[OPT_TL_Y].type = SANE_TYPE_FIXED;
  dev->opt[OPT_TL_Y].unit = SANE_UNIT_MM;
  dev->opt[OPT_TL_Y].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->opt[OPT_TL_Y].constraint.range = &y_range;
  dev->val[OPT_TL_Y].w = y_range.min;

  /* Bottom-right x */
  dev->opt[OPT_BR_X].name = SANE_NAME_SCAN_BR_X;
  dev->opt[OPT_BR_X].title = SANE_TITLE_SCAN_BR_X;
  dev->opt[OPT_BR_X].desc = SANE_DESC_SCAN_BR_X;
  dev->opt[OPT_BR_X].type = SANE_TYPE_FIXED;
  dev->opt[OPT_BR_X].unit = SANE_UNIT_MM;
  dev->opt[OPT_BR_X].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->opt[OPT_BR_X].constraint.range = &x_range;
  dev->val[OPT_BR_X].w = x_range.max;

  /* Bottom-right y */
  dev->opt[OPT_BR_Y].name = SANE_NAME_SCAN_BR_Y;
  dev->opt[OPT_BR_Y].title = SANE_TITLE_SCAN_BR_Y;
  dev->opt[OPT_BR_Y].desc = SANE_DESC_SCAN_BR_Y;
  dev->opt[OPT_BR_Y].type = SANE_TYPE_FIXED;
  dev->opt[OPT_BR_Y].unit = SANE_UNIT_MM;
  dev->opt[OPT_BR_Y].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->opt[OPT_BR_Y].constraint.range = &y_range;
  dev->val[OPT_BR_Y].w = y_range.max;

  dev->opt[OPT_PREVIEW].name = SANE_NAME_PREVIEW;
  dev->opt[OPT_PREVIEW].title = SANE_TITLE_PREVIEW;
  dev->opt[OPT_PREVIEW].desc = SANE_DESC_PREVIEW;
  dev->opt[OPT_PREVIEW].type = SANE_TYPE_BOOL;
  dev->val[OPT_PREVIEW].w = 0;
  
  return SANE_STATUS_GOOD;
}

const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle h, SANE_Int opt)
{
  BrotherMFC *dev = h;
  if (opt < 0 || opt >= OPT_NUM_OPTIONS) {
    return NULL;
  }
  return dev->opt + opt;
}

SANE_Status
sane_control_option (SANE_Handle h, SANE_Int opt, SANE_Action act,
		     void *val, SANE_Word *info)
{
  SANE_Word cap;
  BrotherMFC *dev = h;
  
  if (info) *info = 0;
  
  if (!val) return SANE_STATUS_INVAL;
  
  if (opt < 0 || opt >= OPT_NUM_OPTIONS) {
    return SANE_STATUS_INVAL;
  }

  if (dev->scanning) {
    return SANE_STATUS_DEVICE_BUSY;
  }
  
  cap = dev->opt[opt].cap;
  if (!SANE_OPTION_IS_ACTIVE (cap)) {
    return SANE_STATUS_INVAL;
  }
  
  switch (act) 
    {
    case SANE_ACTION_GET_VALUE:
      switch (opt)
	{
	  /* word options */
	case OPT_NUM_OPTS:
	case OPT_PREVIEW:
	case OPT_RESOLUTION:
	case OPT_TL_Y:
	case OPT_BR_Y:
	case OPT_TL_X:
	case OPT_BR_X:
	  *(SANE_Word *) val = dev->val[opt].w;
	  return SANE_STATUS_GOOD;
	case OPT_MODE:
	case OPT_SOURCE:
	  strcpy (val, dev->val[opt].s);
	  return SANE_STATUS_GOOD;
	default:
	  return SANE_STATUS_INVAL;				      
	}
      break;
    case SANE_ACTION_SET_VALUE:
      if (!SANE_OPTION_IS_SETTABLE (cap)) {
	DBG (DBG_error, "could not set option, not settable\n");
	return SANE_STATUS_INVAL;
      }
      switch (opt)
	{
	  /* word options */
	case OPT_NUM_OPTS:
	case OPT_PREVIEW:
	case OPT_RESOLUTION:
	case OPT_TL_Y:
	case OPT_BR_Y:
	case OPT_TL_X:
	case OPT_BR_X:
	  dev->val[opt].w = *(SANE_Word *) val;
	  if (info) *info |= SANE_INFO_RELOAD_PARAMS;
	  return SANE_STATUS_GOOD;
	case OPT_MODE:
	case OPT_SOURCE:
	  if (strcmp (dev->val[opt].s, val) == 0)
	    return SANE_STATUS_GOOD;
	  free (dev->val[opt].s);
	  dev->val[opt].s = (SANE_Char *) strdup (val);
	  return SANE_STATUS_GOOD;
	default:
	  return SANE_STATUS_INVAL;
	}
      break;
    default:
      break;
    }
  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status
sane_get_parameters (SANE_Handle h, SANE_Parameters *parms)
{
  BrotherMFC *dev = h;
  char *mode;
  int round;
  
  mode = dev->val[OPT_MODE].s;
  
  /* Depending on the color depth, pixels and bytes must be aligned.
     I'm not sure what the optimal numbers are, but these work for
     the MFC 6800 */
  if (strcmp(mode, COLOR_STR)==0) {
    dev->colormode = BROTHER_COLOR_MODE_RGB;
    round = ~1;
  }
  else if (strcmp(mode, GRAY_STR)==0) {
    dev->colormode = BROTHER_COLOR_MODE_GRAY;
    round = ~15;
  }
  else {
    dev->colormode = BROTHER_COLOR_MODE_BW;
    round = ~7;  /* Use ~7 for 100-300 DPI, adjusted below for 600 */
    if (dev->val[OPT_RESOLUTION].w >= 600) round = ~255;
  }
  
  /* for now, preview is the same res as the request */
  if (0 && dev->val[OPT_PREVIEW].w) { /* this code is disabled */
    dev->x_resolution = 100;
    dev->y_resolution = 100;
    dev->x_tl = 0;
    dev->y_tl = 0;
    dev->x_br = mmToIlu(SANE_UNFIX(x_range.max)+0.5);
    dev->y_br = mmToIlu(SANE_UNFIX(y_range.max)+0.5);
  }
  else {
    dev->x_resolution = dev->val[OPT_RESOLUTION].w;
    dev->y_resolution = dev->val[OPT_RESOLUTION].w;
    dev->x_tl = mmToIlu (SANE_UNFIX(dev->val[OPT_TL_X].w))+0.5;
    dev->y_tl = mmToIlu (SANE_UNFIX(dev->val[OPT_TL_Y].w))+0.5;
    dev->x_br = mmToIlu (SANE_UNFIX(dev->val[OPT_BR_X].w))+0.5;
    dev->y_br = mmToIlu (SANE_UNFIX(dev->val[OPT_BR_Y].w))+0.5;
  }
  
  /* Now round the X coordinates */
  dev->x_tl &= round;
  dev->x_br &= round;

  /* width is the difference, but length is one less; at least
     that's the way it is for the MFC 6800 */
  dev->width = dev->x_br - dev->x_tl;
  dev->length = dev->y_br - dev->y_tl - 1;
  
  memset(&dev->params, 0, sizeof(SANE_Parameters));
  
  /* set the right parameters for the color mode */
  if (strcmp(mode, COLOR_STR)==0) {
    dev->params.format = SANE_FRAME_RGB;
    dev->params.lines = -1;  /* unknown - let frontend count actual lines */
    dev->params.pixels_per_line = dev->width;
    /* 3 bytes per pixel (24-bit color) */
    dev->params.bytes_per_line = dev->params.pixels_per_line * 3;
    dev->params.depth = 8;
    dev->interleave = 3;
  }
  else if (strcmp(mode, GRAY_STR)==0) {
    dev->params.format = SANE_FRAME_GRAY;
    dev->params.lines = -1;
    dev->params.pixels_per_line = dev->width;
    dev->params.bytes_per_line = dev->params.pixels_per_line;
    dev->params.depth = 8;
    dev->interleave = 1;
  }
  else { /* BLACK_WHITE_STR */
    dev->params.format = SANE_FRAME_GRAY;
    dev->params.lines = -1;
    dev->params.pixels_per_line = dev->width;
    /* 8 pixels per byte, so divide */
    dev->params.bytes_per_line = dev->params.pixels_per_line / 8;
    dev->params.depth = 1;
    dev->interleave = 1;
  }
  
  dev->params.last_frame = SANE_TRUE;
  
  if (parms) *parms = dev->params;
  
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_start (SANE_Handle h)
{
  BrotherMFC *dev = h;

  /* In ADF mode, if the previous page ended with 0x80 (no more pages)
   * or timed out, don't start another scan - it would hit the flatbed. */
  if (dev->adf_done && strcmp(dev->val[OPT_SOURCE].s, ADF_STR) == 0) {
    DBG(1, "sane_start: ADF done, no more pages\n");
    return SANE_STATUS_NO_DOCS;
  }

  /* The scan command is sent in the first call to scan_read
     in order to ensure that all parameters have been saved
     properly. Perhaps this is not necessary, but it seemed
     to help for me. - FT */
  dev->startscan = 1;
  dev->scanning = 1;
  return SANE_STATUS_GOOD;
}

SANE_Status
send_scan_command(BrotherMFC *dev)
{
  SANE_Status status;
  char bx[1024];
  size_t len;
  int i, ok;
  
  /* if we are scanning, then something went wrong and we're being
     called twice */
  /*  if (dev->scanning) return SANE_STATUS_CANCELLED; */
  
  /* sanity checks for resolution */
  ok = 0;
  for(i=1; i <= dpi_list[0]; i++) {
    if (dev->x_resolution == dpi_list[i]) ok++;
    if (dev->y_resolution == dpi_list[i]) ok++;
  }

  if (ok != 2) { /* didn't find both resolutions */
    DBG (1, "invalid resolution requested");
    return SANE_STATUS_INVAL;
  }


  /* ADF pages 2+: send short next-page command, skip full init */
  if (dev->page_count > 0 && strcmp(dev->val[OPT_SOURCE].s, ADF_STR) == 0) {
    char nextcmd[] = "\x1bX\n\x80";
    len = 4;
    DBG(2, "send_scan_command: ADF page %d next-page cmd\n", dev->page_count);
    device_write(dev, (SANE_Byte *)nextcmd, &len);
    dev->startscan = 0;
    dev->scan_lines = 0;
    dev->readlen = sizeof(dev->readbuf);
    dev->readi = 0;
    dev->page_count++;
    dev->last_data_time = time(NULL);
    return SANE_STATUS_GOOD;
  }

  /* Send I-command to negotiate scan parameters with scanner.
   * The original Brother driver sends this before the X-command
   * to set resolution and color mode. */
  sprintf(bx, "\x1bI\n"
	  "R=%d,%d\n"
	  "M=%s\n"
	  "\x80",
	  dev->x_resolution, dev->y_resolution,
	  dev->colormode);
  len = strlen(bx);
  DBG(2, "send_scan_command: sending I-command (%lu bytes)\n",
      (unsigned long)len);
  status = device_write(dev, (SANE_Byte *)bx, &len);
  if (status == SANE_STATUS_GOOD) {
    SANE_Byte ibuf[256];
    int retries;
    /* Retry reading the I-command response - scanner may need time */
    for (retries = 0; retries < 20; retries++) {
      usleep(500000);  /* 500ms between retries */
      len = sizeof(ibuf);
      status = device_read(dev, ibuf, &len);
      if (status == SANE_STATUS_GOOD && len > 0) {
	DBG(2, "send_scan_command: I-command response %lu bytes "
	    "(retry %d), first=0x%02x\n",
	    (unsigned long)len, retries, ibuf[0]);
	break;
      }
    }
    if (retries >= 20) {
      DBG(1, "send_scan_command: no I-command response after retries\n");
    }
  }

  {
    int use_adf = (strcmp(dev->val[OPT_SOURCE].s, ADF_STR) == 0);
    if (use_adf) {
      sprintf(bx, "\x1bX\n"
	      "R=%d,%d\n"
	      "M=%s\n"
	      "C=NONE\n"
	      "B=50\n"
	      "N=50\n"
	      "U=ON\n"
	      "P=OFF\n"
	      "A=%d,%d,%d,%d\n"
	      "\x80",
	      dev->x_resolution, dev->y_resolution,
	      dev->colormode,
	      dev->x_tl, dev->y_tl,
	      dev->x_br, dev->y_br+1
	      );
    } else {
      sprintf(bx, "\x1bX\n"
	      "R=%d,%d\n"
	      "M=%s\n"
	      "C=NONE\n"
	      "B=50\n"
	      "N=50\n"
	      "U=OFF\n"
	      "P=OFF\n"
	      "A=%d,%d,%d,%d\n"
	      "\x80",
	      dev->x_resolution, dev->y_resolution,
	      dev->colormode,
	      dev->x_tl, dev->y_tl,
	      dev->x_br, dev->y_br+1
	      );
    }
    DBG(2, "send_scan_command: source=%s (adf=%d)\n",
	dev->val[OPT_SOURCE].s, use_adf);
  }
  len = strlen(bx);
  DBG(2, "send_scan_command: sending X-command (%lu bytes)\n",
      (unsigned long)len);
  status = device_write(dev, (SANE_Byte *)bx, &len);
  if (status != SANE_STATUS_GOOD) {
    DBG (DBG_error, "cannot write scan command\n");
    return SANE_STATUS_INVAL;
  }
  DBG(2, "send_scan_command: X-command sent successfully\n");

  dev->startscan = 0;
  dev->scan_lines = 0;  /* keep count of scanned lines */
  dev->adf_done = 0;    /* reset ADF status for new scan job */
  dev->page_count++;

  /* init the data read buffer */
  dev->readlen = sizeof(dev->readbuf);
  dev->readi = 0;
  /* Wait for scanner to start, then drain command responses */
  DBG(2, "send_scan_command: waiting for scanner (page %d)\n", dev->page_count);
  if (dev->page_count <= 1) {
    sleep(6);
  } else {
    usleep(500000);
  }

  /* Drain command responses - read until we get scan data or nothing */
  {
    SANE_Byte drain[32768];
    int drained = 0;
    int drain_retries;
    size_t drain_total = 0;
    for (drain_retries = 0; drain_retries < 5; drain_retries++) {
      size_t rlen = sizeof(drain) - drain_total;
      if (rlen == 0) break;
      status = device_read(dev, drain + drain_total, &rlen);
      if (status != SANE_STATUS_GOOD || rlen == 0)
        break;
      /* Check if this looks like a command response (starts with ESC) */
      if (drain_total == 0 && drain[0] == 0x1B) {
        DBG(2, "send_scan_command: drained command response "
            "(%lu bytes)\n", (unsigned long)rlen);
        drained += rlen;
        usleep(500000);
        continue;
      }
      drain_total += rlen;
      if (drain_total > 15000) break;
    }

    if (drain_total > 0 && dev->interleave == 1) {
      /* Grayscale/BW: sync to record boundary to avoid 600dpi shearing */
      unsigned int si;
      int synced = 0;
      unsigned int expected_bpl = dev->params.bytes_per_line;
      unsigned int recsize = expected_bpl + 3;

      for (si = 0; si + recsize + 3 <= drain_total; si++) {
        if (!(drain[si] & 0x80)) {
          unsigned int rc = drain[si+1] + drain[si+2]*256;
          if (rc == expected_bpl) {
            unsigned int next = si + recsize;
            if (next + 3 <= drain_total && !(drain[next] & 0x80)) {
              unsigned int rc2 = drain[next+1] + drain[next+2]*256;
              if (rc2 == expected_bpl) {
                unsigned int usable = drain_total - si;
                if (usable > sizeof(dev->readbuf)) usable = sizeof(dev->readbuf);
                memcpy(dev->readbuf, drain + si, usable);
                dev->readi = usable;
                synced = 1;
                DBG(1, "send_scan_command: synced at offset %u, %u usable\n", si, usable);
                break;
              }
            }
          }
        }
      }
      if (!synced) {
        /* Fallback: put data in readbuf as-is */
        if (drain_total <= sizeof(dev->readbuf)) {
          memcpy(dev->readbuf, drain, drain_total);
          dev->readi = drain_total;
        }
        DBG(1, "send_scan_command: gray no sync, %u bytes in readbuf\n", (unsigned)drain_total);
      }
    } else if (drain_total > 0) {
      /* Color: put drain data directly in readbuf (original behavior) */
      if (drain_total <= sizeof(dev->readbuf)) {
        memcpy(dev->readbuf, drain, drain_total);
        dev->readi = drain_total;
      }
      DBG(1, "send_scan_command: %u bytes in readbuf\n", (unsigned)drain_total);
    }
  }

  dev->last_data_time = time(NULL);


  dev->logfile = fopen("/tmp/sane.raw", "wb");

  return SANE_STATUS_GOOD;
}

/* Check for scanner status/error code at the start of the read buffer.
 * After process_buffer() shifts data, readbuf[0] is always at a record
 * boundary. Scanner status bytes have bit 7 set (0x80+).
 * Returns SANE_STATUS_GOOD if no status byte is present. */
static SANE_Status
check_scan_status(BrotherMFC *dev)
{
  unsigned char code;

  if (dev->readi < 1 || !(dev->readbuf[0] & 0x80))
    return SANE_STATUS_GOOD;

  code = dev->readbuf[0];
  dev->params.last_frame = 1;

  if (code == 0x80) { /* page end, no more pages */
    dev->adf_done = 1;
    return SANE_STATUS_EOF;
  }
  if (code == 0x81 || code == 0xE3) /* more pages available */
    return SANE_STATUS_EOF;
  if (code == 0x83) /* cancel ack */
    return SANE_STATUS_CANCELLED;
  if (code == 0xC2) /* no document */
    return SANE_STATUS_NO_DOCS;
  if (code == 0xC3) /* document jam */
    return SANE_STATUS_JAMMED;
  if (code == 0xC4) /* cover open */
    return SANE_STATUS_COVER_OPEN;
  return SANE_STATUS_IO_ERROR;
}

/* This function is called by sane_read() to see if there's enough
   read data to send one or more complete lines to the front end */
int
process_buffer(BrotherMFC *dev, SANE_Byte *buf, SANE_Int maxlen)
{
  unsigned int rechead, reclen, recnum, len;
  unsigned int k, j, i, i2;
  unsigned int count , bcount;
  unsigned char * start;

  rechead = 3;  /* header is status byte, followed by length word */
  reclen = dev->params.pixels_per_line + rechead; /* add rec overhead */
  reclen *= dev->interleave;   /* times number of bytes per pixel */

  /* are there records in the buffer already? */
  if (dev->readi > reclen) {
    start = dev->readbuf;
    /* now calculate the read record length from the first record */
    count = start[1] + start[2]*256;
    reclen = count + rechead; /* add rec overhead */
    reclen *= dev->interleave;
    
    /* how many complete records? (use integer math) */
    recnum = dev->readi / reclen;
    /* do the records fit into the buffer? */
    if (recnum > (unsigned)maxlen / reclen) {
      recnum = (unsigned)maxlen / reclen;
    }
    
    /* length of output records (take out headers) */
    len = recnum * dev->params.bytes_per_line;
    
    j = 0;
    for(k=0; k < recnum; k++) {
      /* read one line */
      if (start[0] & 0x80) break; /* check for error flag */
      count = start[1] + start[2]*256; /* two-byte length */
      bcount = count + rechead; /* record length incl 3-byte header */
      if (count <= 0) break;
      /* now process the data line */
      for(i=0; i<count; i++, j+=dev->interleave) {
	/* interleave the color bytes if required */
	for(i2=0; i2<dev->interleave; i2++) {
	  if (j+i2 < (unsigned)maxlen) {
	    buf[j+i2] = start[i+3 + bcount*i2];
	  }
	}
      }
      dev->scan_lines++;
      start += bcount * dev->interleave;
    }
    
    count = recnum * reclen; /* number of bytes we have processed */
    /* now copy the end of the buffer to the start */
    memcpy(dev->readbuf, 
	   dev->readbuf + count, 
	   dev->readi - count + 1);
    dev->readi -= count;
    
    return len;
  }
  
  return 0;
}

/* sane_read() will return data from a buffer that is processed by the
   process_buffer() function. It is necessary to keep a separate buffer
   for data from the scanner because the scanner will return one color
   at a time and thus, may return partial lines (lines where not all three
   colors are present). process_buffer() keeps track of the number of
   complete lines and then rearranges the colors to suit what sane_read()
   must return.
*/

SANE_Status
sane_read (SANE_Handle h, SANE_Byte *buf, SANE_Int maxlen, SANE_Int *lenp)
{
  unsigned int i = 0;
  unsigned char *tmp;
  size_t len;
  size_t readmax;
  SANE_Status status;
  BrotherMFC *dev = h;

  *lenp = 0;

  if (!dev->scanning) {
    return SANE_STATUS_EOF;
  }

  if (dev->startscan) {
    status = send_scan_command(dev);
    if (status != SANE_STATUS_GOOD) {
      return status;
    }
  }

  /* Do we have buffered data that we can process now? */
  *lenp = process_buffer(dev, buf, maxlen);
  if (*lenp > 0) {
    return SANE_STATUS_GOOD;
  }

  /* Check for status/error byte at buffer start (always a record
   * boundary after process_buffer shifts data). This catches
   * end-of-page markers left behind by a previous read. */
  status = check_scan_status(dev);
  if (status != SANE_STATUS_GOOD)
    return status;

  /* point the next byte to fill */
  tmp = dev->readbuf + dev->readi;
  /* read only as many bytes as remain in the buffer */
  readmax = dev->readlen - dev->readi;

  /* Poll interrupt endpoint EP5 to check if scanner has data ready */
  status = device_poll_ready(dev);
  if (status != SANE_STATUS_GOOD) {
    DBG(3, "sane_read: poll_ready returned %d, continuing anyway\n", status);
    /* Don't fail on poll error - some devices may not support it */
  }

  /* attempt to read from the device; repeat 10 times
     and wait for data to appear */
  for(i=10; i>0; i--) {
    len = readmax;
    status = device_read(dev, tmp, &len);
    if (status == SANE_STATUS_GOOD) {
      if (len > 0) {
	dev->readi += len;
        DBG(1, "sane_read: USB read %lu bytes at readi=%u, first 4: %02x %02x %02x %02x\n", (unsigned long)len, dev->readi - (unsigned)len, tmp[0], tmp[1], tmp[2], tmp[3]);
	dev->last_data_time = time(NULL);
	break;
      }
      /* Poll again before retrying */
      device_poll_ready(dev);
      usleep(100000);  /* 100ms instead of 1 second */
    }
    else if (status == SANE_STATUS_EOF) {
      device_poll_ready(dev);
      usleep(100000);
    }
    else {
      return status;
    }
  }

  if (dev->logfile && len > 0) fwrite(tmp, 1, len, dev->logfile);

  /* process data in read-ahead buffer */
  *lenp = process_buffer(dev, buf, maxlen);
  if (*lenp > 0)
    return SANE_STATUS_GOOD;

  /* No complete lines could be processed. Check for status/error byte
   * at the start of the buffer (record boundary). This is the correct
   * place to detect end-of-page markers - NOT at tmp[0] which may point
   * to the middle of a record when there was leftover data in the buffer. */
  status = check_scan_status(dev);
  if (status != SANE_STATUS_GOOD)
    return status;

  /* Safety timeout: if no data has been received for 5 seconds,
   * assume end of document. This handles ADF end-of-feed when
   * the scanner stops sending data without a status byte. */
  {
    time_t now = time(NULL);
    if (now - dev->last_data_time >= 60) {
      DBG(1, "sane_read: no data for %ld seconds, assuming end of document\n",
	  (long)(now - dev->last_data_time));
      dev->adf_done = 1;
      return SANE_STATUS_EOF;
    }
  }

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_set_io_mode (SANE_Handle h, SANE_Bool non_blocking)
{
  BrotherMFC *dev = h;
  
  if (!dev->scanning) {
    return SANE_STATUS_INVAL;
  }
  else if (non_blocking) {
    return SANE_STATUS_GOOD;
  }
  else {
    return SANE_STATUS_UNSUPPORTED;
  }
}

SANE_Status
sane_get_select_fd (SANE_Handle h, SANE_Int *fdp)
{
  h = h; fdp = fdp;
  return SANE_STATUS_UNSUPPORTED;
}

void
sane_cancel (SANE_Handle h)
{
  SANE_Status status;
  char * bx = "\x1bR\n\x80";
  size_t len;
  BrotherMFC *dev = h;
  
  len = strlen(bx);
  status = device_write(dev, (SANE_Byte *)bx, &len);
  if (status != SANE_STATUS_GOOD) {
    DBG (DBG_error, "cannot write cancel command\n");
  }
  dev->scanning = 0;
  dev->startscan = 0;
  
  if (dev->logfile) fclose(dev->logfile);
}

void
sane_close (SANE_Handle dev_orig)
{
  int i;
  BrotherMFC *dev_tmp;
  BrotherMFC *dev = dev_orig;
  
  DBG (5, "sane_close()\n");
  
  if (first_dev == dev) {
    first_dev = dev->next;
  }
  else {
    dev_tmp = first_dev;
    while (dev_tmp->next && dev_tmp->next != dev) {
      dev_tmp = dev_tmp->next;
    }
    if (dev_tmp->next != NULL) {
      dev_tmp->next = dev_tmp->next->next;
    }
  }

  if (dev->port == BROTHER_USB) {
    sanei_usb_close(dev->husb);
  }
  else if (dev->port == BROTHER_PARPORT) {
    /* TODO: close the par port */
  }
  
  for (i = 1; i < OPT_NUM_OPTIONS; i++) {
    if (dev->opt[i].type == SANE_TYPE_STRING && dev->val[i].s) {
      free (dev->val[i].s);
    }
  }
  free (dev);
  num_devices--;
}

void
sane_exit (void)
{
  while (first_dev) {
    sane_close (first_dev);
  }

  if (devlist) {
    free (devlist);
    devlist = NULL;
  }
}
