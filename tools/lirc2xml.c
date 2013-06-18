/****************************************************************************
 ** lirc2xml.c **************************************************************
 ****************************************************************************
 *
 * lirc2xml - LIRC XML export
 *
 * Copyright (C) 2009 Bengt Martensson <barf@bengt-martensson.de>.
 * License: GPL 2 or later.
 *
 * TODO: Presently does not generate multiple signal for toggles.
 *       Verify/fix behavior for repeat signals.
 *
 */ 

#define LIRC2XML_VERSION "0.1.2"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <sys/time.h>
#include <pwd.h>
#include <errno.h>
#include <libgen.h>
#ifdef DEBUG
#include <stdarg.h>
#endif

#include "config_file.h"
#include "transmit.h"

struct ir_remote *remotes;
struct ir_remote *repeat_remote = NULL;
struct ir_ncode *repeat_code = NULL;

char *progname="lirc2xml";
const char *configfile = NULL;
char *outputfilename = NULL;
char *selected_remote = NULL;
#ifdef DEBUG
unsigned int debug = 0;
#endif
FILE *xml_file;

/* Need this cruft since config_file.c calls it... */
void logprintf(int prio,char *format_str, ...)
{
#ifdef DEBUG
  if(debug == 0)
    return;

  char fmt[1000];
  strcpy(fmt, format_str);
  if (fmt[strlen(format_str)-1] != '\n') {
    strcat(fmt, "\n");
    format_str = fmt;
  }
  int save_errno = errno;
  va_list ap;
	
  va_start(ap,format_str);
  vfprintf(stderr, format_str, ap);
  va_end(ap);

  errno = save_errno;
#endif
}

void parse_config(void)
{
  FILE *fd;
  
  if (NULL==configfile) {
    fd = stdin;
  } else {
    fd=fopen(configfile, "r");
  }
  if (fd==NULL) {
    fprintf(stderr, "could not open config file '%s'\n", configfile);
    exit(EXIT_FAILURE);
  }

  remotes=read_config(fd, configfile);
  fclose(fd);

  if (remotes==(void *) -1) {
    fprintf(stderr,"parsing of config file failed\n");
    exit(EXIT_FAILURE);
  }

#ifdef DEBUG
  if (debug > 0)
    fprintf(stderr, "config file read\n");
#endif
    
  if (remotes==NULL) {
      fprintf(stderr, "config file contains no valid remote control definition\n");
      exit(EXIT_FAILURE);
  }
}

#ifdef DECODEIR
extern void Version(char *Result);
extern void DecodeIR(unsigned int* Context,
		     int* TpaiBursts, 	
		     int TiFreq,
		     int TiSingleBurstCount,
		     int TiRepeatBurstCount,
		     char* TsProtocol,
		     int* TiDevice,
		     int* TiSubDevice,
		     int* TiOBC,
		     int TaiHex[4],
		     char* TsMisc,
		     char* TsError);
#endif

#define CCF_LEARNED_CODE 0
#define PRONTO_CONSTANT 0.241246

#define DEFAULT_EXPORTDIR "/tmp"


static const char *lirc_type_string(struct ir_remote *r)
{
  return
    is_raw(r)           ? "RAW_CODES"
    : is_space_enc(r)   ? "SPACE_ENC"
    : is_space_first(r) ? "SPACE_FIRST"
    : is_rc5(r)         ? "RC5"
    : is_rc6(r)         ? "RC6"
    : is_rcmm(r)        ? "RCMM"
    : is_goldstar(r)    ? "GOLDSTAR"
    : is_grundig(r)     ? "GRUNDIG"
    : is_bo(r)          ? "BO"
    : is_serial(r)      ? "SERIAL"
    : is_xmp(r)         ? "XMP"
    : "??";
}

unsigned int freq2ccfcode(int freq)
{
  return (unsigned int) (1000000.0/(PRONTO_CONSTANT 
				    * (double) (freq ? freq 
						: DEFAULT_FREQ)) + 0.5);
}

unsigned int us2ccftime(int ccftime, int freq)
{
  return  (unsigned int)(((double)ccftime)
			 *((double)(freq ? freq
				    : DEFAULT_FREQ))/1000000 + 0.5);
}

#ifdef DECODEIR
static unsigned int decodeir_context[2];
static int decodeir_str(char * decoding, int freq, int *data, int intro_length,
			int rep_length)
{
  char protocol[255] = "";
  int device = -1;
  int subdevice = -1;
  int obc = -1;
  int hex[4] = { -1, -1, -1, -1};
  char misc_message[255] = "";
  char error_message[255] = "";

  DecodeIR(decodeir_context, data, freq, intro_length, rep_length,
	   protocol, &device, &subdevice, &obc, hex, misc_message,
	   error_message);

  if (protocol[0] == '\0')
    decoding[0] = '\0';
  else
    sprintf(decoding, "<decoding"
	    " protocol=\"%s\""
	    " device=\"%d\""
	    " subdevice=\"%d\""
	    " obc=\"%d\""
	    " hex0=\"%d\""
	    " hex1=\"%d\""
	    " hex2=\"%d\""
	    " hex3=\"%d\""
	    " misc=\"%s\""
	    " error=\"%s\""
	    "/>",
	    protocol, device, subdevice, obc,
	    hex[0], hex[1], hex[2], hex[3],
	    misc_message, error_message);
  return strlen(decoding);
}
#endif

static void do_remote(struct ir_remote *r)
{
  struct ir_ncode *c;
  /* int is_toggling; */

  fprintf(xml_file, "  <remote name=\"%s\">\n", r->name);
  fprintf(xml_file, "    <lircdata");
  
  fprintf(xml_file, " type=\"%s\"", lirc_type_string(r));
  fprintf(xml_file, " bits=\"%d\"", r->bits); /* bits (length of code) */
  fprintf(xml_file, " flags=\"%d\"",  r->flags); /* flags */
  fprintf(xml_file, " eps=\"%d\"",  r->eps); /* eps (_relative_ tolerance) */
  fprintf(xml_file, " aeps=\"%d\"",  r->aeps); /* aeps (_absolute_ tolerance) */
  fprintf(xml_file, " pthree=\"%d\" sthree=\"%d\"", r->pthree, r->sthree); /* 3 (only used for RC-MM) */
  fprintf(xml_file, " ptwo=\"%d\" stwo=\"%d\"", r->ptwo, r->stwo); /* 2 (only used for RC-MM) */
  fprintf(xml_file, " pone=\"%d\" sone=\"%d\"", r->pone, r->sone); /* 1 */
  fprintf(xml_file, " pzero=\"%d\" szero=\"%d\"", r->pzero, r->szero); /* 0 */
  fprintf(xml_file, " plead=\"%d\"", r->plead); /* leading pulse */
  fprintf(xml_file, " ptrail=\"%d\"", r->ptrail);	/* trailing pulse */
  fprintf(xml_file, " pfoot=\"%d\" sfoot=\"%d\"", r->pfoot, r->sfoot); /* foot */
  fprintf(xml_file, " prepeat=\"%d\" srepeat=\"%d\"", r->prepeat, r->srepeat); /* indicate repeating */
  
  fprintf(xml_file, " pre_data_bits=\"%d\"", r->pre_data_bits); /* length of pre_data */
  fprintf(xml_file, " pre_data=\"%d\"", (int)r->pre_data); /* data which the remote sends before actual keycode */
  fprintf(xml_file, " post_data_bits=\"%d\"",  r->post_data_bits); /* length of post_data */
  fprintf(xml_file, " post_data=\"%d\"", (int)r->post_data); /* data which the remote sends after actual keycode */
  fprintf(xml_file, " pre_p=\"%d\" pre_s=\"%d\"", r->pre_p, r->pre_s); /* signal between pre_data and keycode */
  fprintf(xml_file, " post_p=\"%d\" post_s=\"%d\"", r->post_p, r->post_s); /* signal between keycode and post_code */
  
  fprintf(xml_file, " gap=\"%d\"", r->gap); /* time between signals in usecs */
  fprintf(xml_file, " gap2=\"%d\"", r->gap2); /* time between signals in usecs */
  fprintf(xml_file, " repeat_gap=\"%d\"", r->repeat_gap);	/* time between two repeat codes if different from gap */
  fprintf(xml_file, " toggle_bit=\"%d\"", r->toggle_bit);	/* obsolete */
  fprintf(xml_file, " toggle_bit_mask=\"%d\"", (int)r->toggle_bit_mask); /* previously only one bit called toggle_bit */
  fprintf(xml_file, " min_repeat=\"%d\"", r->min_repeat);	/* code is repeated at least x times code sent once -> min_repeat=0 */
  fprintf(xml_file, " min_code_repeat=\"%d\"", r->min_code_repeat); /*meaningful only if remote sends
								      a repeat code: in this case
								      this value indicates how often
								      the real code is repeated
								      before the repeat code is being
								      sent */
  fprintf(xml_file, " freq=\"%d\"", r->freq ? r->freq : DEFAULT_FREQ); /* modulation frequency */
  fprintf(xml_file, " duty_cycle=\"%d\"", r->duty_cycle);	/* 0<duty cycle<=100 */
  fprintf(xml_file, " toggle_mask=\"%d\"", (int)r->toggle_mask); /* Sharp (?) error detection scheme */
  fprintf(xml_file, " rc6_mask=\"%d\"", (int)r->rc6_mask); /* RC-6 doubles signal length of some bits */

  /* serial protocols */
  fprintf(xml_file, " baud=\"%d\"", r->baud); /* can be overridden by [p|s]zero, [p|s]one */
  fprintf(xml_file, " bits_in_byte=\"%d\"", r->bits_in_byte); /* default: 8 */
  fprintf(xml_file, " parity=\"%d\"", r->parity);       /* currently unsupported */
  fprintf(xml_file, " stop_bits=\"%d\"", r->stop_bits); /* mapping: 1->2 1.5->3 2->4 */

  fprintf(xml_file, " ignore_mask=\"%d\"", (int)r->ignore_mask); /* mask defines which bits can be ignored when matching a code */
  fprintf(xml_file, "/>\n");

  /*   is_toggling = has_toggle_mask(r) || has_toggle_bit_mask(r); */
  repeat_remote = NULL; //= r;
  r->last_code = NULL;
  for (c = r->codes; c->name; c++) {
    init_send_buffer();
    int success = init_send(r, c);
      if (success) {
	int intro_length = send_buffer.wptr;
	int rep_length = 0;
	int i;
#ifdef DECODEIR
	char decoding[1000];
#endif
#ifdef DO_REPEAT
	send_repeat(r);
	rep_length = send_buffer.wptr - intro_length;
#endif
	fprintf(xml_file,
		"    <code name=\"%s\" codeno=\"0x%016X\">\n",
		c->name, (unsigned int) c->code);
#ifdef DECODEIR
	decodeir_context[0] = 0;
	decodeir_context[1] = 0;
	decodeir_str(decoding, r->freq, send_buffer.data,
		     (intro_length+1)/2, rep_length/2);
	for (i = 0; i < 10 && decoding[0] != '\0'; i++) {
	  fprintf(xml_file, "      %s\n", decoding);
	  decodeir_str(decoding, r->freq,
		       send_buffer.data,
		       (intro_length+1)/2,
		       rep_length/2);
	}
#endif
	fprintf(xml_file, "      <ccf>%d %d %d %d",
		CCF_LEARNED_CODE, freq2ccfcode(r->freq),
		(intro_length+1)/2, rep_length/2);
	for (i = 0; i < intro_length; i++)
	  fprintf(xml_file, " %d",
		  us2ccftime(send_buffer.data[i],
			     r->freq));
	fprintf(xml_file, " %d",
		us2ccftime(r->min_remaining_gap, r->freq));
#ifdef DO_REPEAT
	for (i = intro_length; i < send_buffer.wptr; i++)
	  fprintf(xml_file,
		  " %d",
		  us2ccftime(send_buffer.data[i], r->freq));
#endif
	fprintf(xml_file, "</ccf>\n");
	fprintf(xml_file, "    </code>\n");
    
      } else
	fprintf(xml_file, "    <failedcode name=\"%s\" codeno=\"0x%016X\" length=\"%d\"/>\n",
		c->name, (unsigned int)c->code,
		send_buffer.wptr);
  }

  fprintf(xml_file, "  </remote>\n");
}

int xml_export()
{
  time_t timep[0];
  char s1[1000];
  char s2[1000];
  char *p;
  char decodeir_version[100] = "none";
  int remote_match = 0;
  struct ir_remote *r;

#ifdef DECODEIR
  Version(decodeir_version);
#endif

  if (outputfilename == NULL) {
    outputfilename = (char*) malloc(1000); /* Don't bother to free. */
    strcpy(outputfilename,
	   getenv("LIRC_XML_EXPORTDIR") ? getenv("LIRC_XML_EXPORTDIR")
	   : DEFAULT_EXPORTDIR);
    strcat(outputfilename, "/");
    strcpy(s1, configfile);
    strcpy(s2, basename(s1));
    p = strrchr(s2, '.');
    if (p)
      *p = '\0';
    strcat(outputfilename, s2);
    strcat(outputfilename, ".xml");
	
    xml_file = fopen(outputfilename, "w");
  } else if (strcmp(outputfilename, "-") == 0)
    xml_file = stdout;
  else
    xml_file = fopen(outputfilename, "w");

  if (!xml_file) {
    fprintf(stderr, "Cannot open %s for writing, exiting.\n",
	    outputfilename);
    _exit(EXIT_FAILURE);
  }
  time(timep);
  fprintf(xml_file,
	  "<lircremotes creation-date=\"%s\" creator=\"%s\""
	  " lircversion=\"%s\" lirc2xml_version=\"%s\" configfile=\"%s\""
	  " decodeir_version=\"%s\""
	  ">\n",
	  strndup(ctime(timep),(size_t)(strlen(ctime(timep))-1)),
	  /* NOTE removed for android... getpwnam(getlogin())->pw_gecos*/"", VERSION, LIRC2XML_VERSION,
	  configfile, decodeir_version
	  );
  
  for (r = remotes; r; r = r->next)
    if (selected_remote == NULL || strcmp(selected_remote, r->name) == 0) {
	do_remote(r);
	remote_match++;
    }

  if (remote_match == 0)
    fprintf(stderr,
	    "Note: requested remote `%s' was not found in file `%s.'\n",
	    selected_remote, configfile);

  fprintf(xml_file, "</lircremotes>\n");
  fclose(xml_file);
  fprintf(stderr, "XML export %s successfully created.\n", outputfilename);
  return(1);
}

int main(int argc,char **argv)
{
  while(1) {
    static struct option long_options[] =
      {
	{"help",no_argument,NULL,'h'},
	{"version",no_argument,NULL,'v'},
	{"output",required_argument,NULL,'o'},
	{"remote",required_argument,NULL,'r'},
#ifdef DEBUG
	{"debug",optional_argument,NULL,'d'},
#endif
	{0, 0, 0, 0}
      };
    int c = getopt_long(argc,argv,
			"hvo:r:"
#ifdef DEBUG
			"d::"
#endif
			,long_options,NULL);
    if(c==-1)
      break;

    switch (c) {
    case 'h':
      printf("Usage: %s [options] [config-file]\n",progname);
      printf("\t -h --help\t\t\tdisplay this message\n");
      printf("\t -v --version\t\t\tdisplay version\n");
      printf("\t -o --output=filename\t\tXML output filename\n");
      printf("\t -r --remote=remotename\t\tInclude only this remote in the export\n");
      printf("\t -d[debug_level] --debug[=debug_level]\n");
      return(EXIT_SUCCESS);
      break;
    case 'v':
      printf("%s %s using LIRC version %s.\n", progname, LIRC2XML_VERSION, VERSION);
      return(EXIT_SUCCESS);
      break;
    case 'o':
      outputfilename=optarg;
      break;
#ifdef DEBUG
    case 'd':
      debug = optarg==NULL ? 1 : atoi(optarg);
    break;
#endif
    case 'r':
      selected_remote = optarg;
      break;
    default:
      printf("Usage: %s [options] [config-file]\n", progname);
      return(EXIT_FAILURE);
      break;
    }
  }
  if(optind==argc-1)
    configfile=argv[optind];
  else if(optind!=argc) {
    fprintf(stderr,"%s: invalid argument count\n", progname);
    return(EXIT_FAILURE);
  }
	
#ifdef DEBUG
  if (debug > 0) {
    fprintf(stderr, "debug = %d\n", debug);
    fprintf(stderr, "outputfilename = %s\n", outputfilename);
    fprintf(stderr, "configfile = %s\n", configfile);
    fprintf(stderr, "selected_remote = %s\n", selected_remote);
  }
#endif

  remotes=NULL;
  parse_config();
  xml_export();

  return(EXIT_SUCCESS); 
}
