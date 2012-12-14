/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
#-----------------------------------------------------------------------------
# Original Python implementation by Artem Pavlenko
# Re-implementation by Jon Burgess, Copyright 2006
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#-----------------------------------------------------------------------------
*/
#define _GNU_SOURCE

/* 2011-07-03 02:30
   Markus Weber */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#include "osmtypes.h"
#include "reprojection.h"
#include "output.h"

#define inline

typedef enum {false= 0,true= 1} bool;
typedef uint8_t byte;
typedef unsigned int uint;
#define isdig(x) isdigit((unsigned char)(x))
static int loglevel= 0;  /* logging to stderr; */
/* 0: no logging; 1: small logging; 2: normal logging;
   3: extended logging; */
#define DP(f) fprintf(stderr,"- Debug: " #f "\n");
#define DPv(f,...) fprintf(stderr,"- Debug: " #f "\n",__VA_ARGS__);
#if __WIN32__
#define NL "\r\n"  /* use CR/LF as new-line sequence */
  #define off_t off64_t
  #define lseek lseek64
#else
#define NL "\n"  /* use LF as new-line sequence */
  #define O_BINARY 0
#endif

#define PERR(f) \
  fprintf(stderr,"osm2pgsql Error: " f "\n");
/* print error message */
#define PERRv(f,...) \
  fprintf(stderr,"osm2pgsql Error: " f "\n",__VA_ARGS__);
/* print error message with value(s) */
#define WARN(f) { static int msgn= 3; if(--msgn>=0) \
  fprintf(stderr,"osm2pgsql Warning: " f "\n"); }
/* print a warning message, do it maximal 3 times */
#define WARNv(f,...) { static int msgn= 3; if(--msgn>=0) \
  fprintf(stderr,"osm2pgsql Warning: " f "\n",__VA_ARGS__); }
/* print a warning message with value(s), do it maximal 3 times */
#define PINFO(f) \
  fprintf(stderr,"osm2pgsql: " f "\n");
/* print info message */
#define ONAME(i) \
  (i==0? "node": i==1? "way": i==2? "relation": "unknown object")

static inline char *stpcpy0(char *dest, const char *src) {
    /* redefinition of C99's stpcpy() because it's missing in MinGW,
       and declaration in Linux seems to be wrong; */
  while(*src!=0)
    *dest++= *src++;
  *dest= 0;
  return dest;
}  /* end stpcpy0() */

static inline char* uint32toa(uint32_t v,char* s) {
    /* convert uint32_t integer into string;
       v: long integer value to convert;
       return: s;
       s[]: digit string; */
  char* s1,*s2;
  char c;

  s1= s;
  if(v==0)
    *s1++= '0';
  s2= s1;
  while(v>0)
    { *s2++= "0123456789"[v%10]; v/= 10; }
  *s2--= 0;
  while(s2>s1)
    { c= *s1; *s1= *s2; *s2= c; s1++; s2--; }
  return s;
}  /* end   uint32toa() */

static inline void createtimestamp(uint64_t v,char* sp) {
    /* write a timestamp in OSM format, e.g.: "2010-09-30T19:23:30Z",
       into a string;
       v: value of the timestamp;
       sp[21]: destination string; */
  time_t vtime;
  struct tm tm;
  int i;

  vtime= v;
  #if __WIN32__
  memcpy(&tm,gmtime(&vtime),sizeof(tm));
  #else
  gmtime_r(&vtime,&tm);
  #endif
  i= tm.tm_year+1900;
  sp+= 3; *sp--= i%10+'0';
  i/=10; *sp--= i%10+'0';
  i/=10; *sp--= i%10+'0';
  i/=10; *sp= i%10+'0';
  sp+= 4; *sp++= '-';
  i= tm.tm_mon+1;
  *sp++= i/10+'0'; *sp++= i%10+'0'; *sp++= '-';
  i= tm.tm_mday;
  *sp++= i/10+'0'; *sp++= i%10+'0'; *sp++= 'T';
  i= tm.tm_hour;
  *sp++= i/10+'0'; *sp++= i%10+'0'; *sp++= ':';
  i= tm.tm_min;
  *sp++= i/10+'0'; *sp++= i%10+'0'; *sp++= ':';
  i= tm.tm_sec%60;
  *sp++= i/10+'0'; *sp++= i%10+'0'; *sp++= 'Z'; *sp= 0;
}  /* end   createtimestamp() */



/*------------------------------------------------------------
  Module pbf_   protobuf conversions module
  ------------------------------------------------------------
  
  this module provides procedures for conversions from
  protobuf formats to regular numbers;
  as usual, all identifiers of a module have the same prefix,
  in this case 'pbf'; one underline will follow in case of a
  global accessible object, two underlines in case of objects
  which are not meant to be accessed from outside this module;
  the sections of private and public definitions are separated
  by a horizontal line: ----
  many procedures have a parameter 'pp'; here, the address of
  a buffer pointer is expected; this pointer will be incremented
  by the number of bytes the converted protobuf element consumes;

  ------------------------------------------------------------ */

static inline uint32_t pbf_uint32(byte** pp) {
    /* get the value of an unsigned integer;
       pp: see module header; */
  byte* p;
  uint32_t i;
  uint32_t fac;

  p= *pp;
  i= *p;
  if((*p & 0x80)==0) {  /* just one byte */
    (*pp)++;
return i;
    }
  i&= 0x7f;
  fac= 0x80;
  while(*++p & 0x80) {  /* more byte(s) will follow */
    i+= (*p & 0x7f)*fac;
    fac<<= 7;
    }
  i+= *p++ *fac;
  *pp= p;
  return i;
}  /* end   pbf_uint32() */

static inline int32_t pbf_sint32(byte** pp) {
    /* get the value of an unsigned integer;
       pp: see module header; */
  byte* p;
  int32_t i;
  int32_t fac;
  int sig;

  p= *pp;
  i= *p;
  if((*p & 0x80)==0) {  /* just one byte */
    (*pp)++;
    if(i & 1)  /* negative */
return -1-(i>>1);
    else
return i>>1;
    }
  sig= i & 1;
  i= (i & 0x7e)>>1;
  fac= 0x40;
  while(*++p & 0x80) {  /* more byte(s) will follow */
    i+= (*p & 0x7f)*fac;
    fac<<= 7;
    }
  i+= *p++ *fac;
  *pp= p;
  if(sig)  /* negative */
return -1-i;
    else
return i;
}  /* end   pbf_sint32() */

static inline uint64_t pbf_uint64(byte** pp) {
    /* get the value of an unsigned integer;
       pp: see module header; */
  byte* p;
  uint64_t i;
  uint64_t fac;

  p= *pp;
  i= *p;
  if((*p & 0x80)==0) {  /* just one byte */
    (*pp)++;
return i;
    }
  i&= 0x7f;
  fac= 0x80;
  while(*++p & 0x80) {  /* more byte(s) will follow */
    i+= (*p & 0x7f)*fac;
    fac<<= 7;
    }
  i+= *p++ *fac;
  *pp= p;
  return i;
}  /* end   pbf_uint64() */

static inline int64_t pbf_sint64(byte** pp) {
    /* get the value of a signed integer;
       pp: see module header; */
  byte* p;
  int64_t i;
  int64_t fac;
  int sig;

  p= *pp;
  i= *p;
  if((*p & 0x80)==0) {  /* just one byte */
    (*pp)++;
    if(i & 1)  /* negative */
return -1-(i>>1);
    else
return i>>1;
    }
  sig= i & 1;
  i= (i & 0x7e)>>1;
  fac= 0x40;
  while(*++p & 0x80) {  /* more byte(s) will follow */
    i+= (*p & 0x7f)*fac;
    fac<<= 7;
    }
  i+= *p++ *fac;
  *pp= p;
  if(sig)  /* negative */
return -1-i;
    else
return i;
}  /* end   pbf_sint64() */

#if 0  /* not used at present */
static inline void pbf_intjump(byte** pp) {
    /* jump over a protobuf formatted integer;
       pp: see module header;
       we do not care about a possibly existing identifier,
       therefore as the start address *pp the address of the
       integer value is expected; */
  byte* p;

  p= *pp;
  while(*p & 0x80) p++; p++;
  *pp= p;
}  /* end   pbf_intjump() */
#endif

/*------------------------------------------------------------
  end   Module pbf_   protobuf conversions module
  ------------------------------------------------------------ */



/*------------------------------------------------------------
  Module read_   OSM file read module
  ------------------------------------------------------------
  
  this module provides procedures for buffered reading of
  standard input;
  as usual, all identifiers of a module have the same prefix,
  in this case 'read'; one underline will follow in case of a
  global accessible object, two underlines in case of objects
  which are not meant to be accessed from outside this module;
  the sections of private and public definitions are separated
  by a horizontal line: ---- */

#define read_PREFETCH ((32+3)*1024*1024)
/* number of bytes which will be available in the buffer after
   every call of read_input();
   (important for reading .pbf files:
   size must be greater than pb__blockM) */
#define read__bufM (read_PREFETCH*5)  /* length of the buffer; */
typedef struct {  /* members may not be accessed from external */
    int fd;  /* file descriptor */
    bool eof;  /* we are at the end of input file */
    byte* bufp;  /* pointer in buf[] */
    byte* bufe;  /* pointer to the end of valid input in buf[] */
  int64_t read__counter;
    /* byte counter to get the read position in input file; */
  uint64_t bufferstart;
    /* dummy variable which marks the start of the read buffer
       concatenated  with this instance of read info structure; */
   } read_info_t;

/*------------------------------------------------------------*/

static read_info_t* read_infop= NULL;
/* presently used read info structure, i.e. file handle */
#define read__buf ((byte*)&read_infop->bufferstart)
/* start address of the file's input buffer */
static byte* read_bufp= NULL;  /* may be incremented by external */
/* up to the number of read_PREFETCH bytes before read_input() is
   called again; */
static byte* read_bufe= NULL;  /* may not be changed from external */

static int read_open(const char* filename) {
    /* open an input file;
       filename[]: path and name of input file;
               ==NULL: standard input;
               return: 0: ok; !=0: error;
               read_infop: handle of the file;
               note that you should close every opened file with read_close()
               before the program ends;
               
               save status of presently processed input file (if any) */
    if(read_infop!=NULL) {
    read_infop->bufp= read_bufp;
    read_infop->bufp= read_bufe;
    }

    /* get memory space for file information and input buffer */
  read_infop= (read_info_t*)malloc(sizeof(read_info_t)+read__bufM);
  if(read_infop==NULL) {
    PERRv("could not get %i bytes of memory.",read__bufM)
return 1;
    }

  /* initialize read info structure */
  read_infop->fd= 0;  /* (default) standard input */
  read_infop->eof= false;  /* we are at the end of input file */
  read_infop->bufp= read_infop->bufe= read__buf;  /* pointer in buf[] */
  /* pointer to the end of valid input in buf[] */
  read_infop->read__counter= 0;

  /* set modul-global variables which are associated with this file */
  read_bufp= read_infop->bufp;
  read_bufe= read_infop->bufe;

  /* open the file */
  if(loglevel>=2)
    fprintf(stderr,"Read-opening: %s",
      filename==NULL? "stdin": filename);
  if(filename==NULL)  /* stdin shall be opened */
    read_infop->fd= 0;
  else if(filename!=NULL) {  /* a real file shall be opened */
    read_infop->fd= open(filename,O_RDONLY|O_BINARY);
    if(read_infop->fd<0) {
      if(loglevel>=2)
        fprintf(stderr," -> failed\n");
      PERRv("could not open input file: %.80s\n",
        filename==NULL? "standard input": filename)
      free(read_infop); read_infop= NULL;
      read_bufp= read_bufe= NULL;
return 1;
      }
  }  /* end   a real file shall be opened */
  if(loglevel>=2)
    fprintf(stderr," -> FD %i\n",read_infop->fd);
return 0;
}  /* end   read_open() */

static void read_close() {
    /* close an opened file;
       read_infop: handle of the file which is to close; */
  int fd;

  if(read_infop==NULL)  /* handle not valid; */
return;
  fd= read_infop->fd;
  if(loglevel>=1) {  /* verbose */
    fprintf(stderr,"osm2pgsql: Number of bytes read: %"PRIu64"\n",
      read_infop->read__counter);
    }
  if(loglevel>=2) {
    fprintf(stderr,"Read-closing FD: %i\n",fd);
    }
  if(fd>0)  /* not standard input */
    close(fd);
  free(read_infop); read_infop= NULL;
  read_bufp= read_bufe= NULL;
}  /* end   read_close() */

static inline bool read_input() {
    /* read data from standard input file, use an internal buffer;
       make data available at read_bufp;
       read_open() must have been called before calling this procedure;
       return: there are no (more) bytes to read;
       read_bufp: start of next bytes available;
       may be incremented by the caller, up to read_bufe;
       read_bufe: end of bytes in buffer;
       must not be changed by the caller;
       after having called this procedure, the caller may rely on
       having available at least read_PREFETCH bytes at address
       read_bufp - with one exception: if there are not enough bytes
       left to read from standard input, every byte after the end of
       the reminding part of the file in the buffer will be set to
       0x00 - up to read_bufp+read_PREFETCH; */
  int l,r;

  if(read_bufp+read_PREFETCH>=read_bufe) {  /* read buffer is too low */
      if(!read_infop->eof) {  /* still bytes in the file */
          if(read_bufe>read_bufp) {  /* bytes remaining in buffer */
        memmove(read__buf,read_bufp,read_bufe-read_bufp);
        /* move remaining bytes to start of buffer */
        read_bufe= read__buf+(read_bufe-read_bufp);
        /* protect the remaining bytes at buffer start */
        }
          else  /* no remaining bytes in buffer */
              read_bufe= read__buf;  /* no bytes remaining to protect */
          /* add read bytes to debug counter */
      read_bufp= read__buf;
      do {  /* while buffer has not been filled */
        l= (read__buf+read__bufM)-read_bufe-4;
        /* number of bytes to read */
        r= read(read_infop->fd,read_bufe,l);
        if(r<=0) {  /* no more bytes in the file */
          read_infop->eof= true;
          /* memorize that there we are at end of file */
          l= (read__buf+read__bufM)-read_bufe;
          /* reminding space in buffer */
          if(l>read_PREFETCH) l= read_PREFETCH;
          memset(read_bufe,0,l);
          /* set reminding space up to prefetch bytes in buffer to 0 */
      break;
          }
        read_infop->read__counter+= r;
        read_bufe+= r;  /* set new mark for end of data */
        read_bufe[0]= 0; read_bufe[1]= 0;  /* set 4 null-terminators */
        read_bufe[2]= 0; read_bufe[3]= 0;
      } while(r<l);  /* end   while buffer has not been filled */
      }  /* end   still bytes to read */
  }  /* end   read buffer is too low */
  return read_infop->eof && read_bufp>=read_bufe;
}  /* end   read__input() */


/*------------------------------------------------------------
  end Module read_   OSM file read module
  ------------------------------------------------------------ */



/*------------------------------------------------------------
  Module str_   string read module
  ------------------------------------------------------------
  
  this module provides procedures for conversions from
  strings which have been stored in data stream objects to
  c-formatted strings;
  as usual, all identifiers of a module have the same prefix,
  in this case 'str'; one underline will follow in case of a
  global accessible object, two underlines in case of objects
  which are not meant to be accessed from outside this module;
  the sections of private and public definitions are separated
  by a horizontal line: ---- */

#define str__tabM (15000+4000)
/* +4000 because it might happen that an object has a lot of 
   key/val pairs or refroles which are not stored already; */
#define str__tabstrM 250  /* must be < row size of str__rab[] */
typedef struct str__info_struct {
    /* members of this structure must not be accessed
       from outside this module; */
  char tab[str__tabM][256];
    /* string table; see o5m documentation;
     row length must be at least str__tabstrM+2;
     each row contains a double string; each of the two strings
     is terminated by a zero byte, the logical lengths must not
     exceed str__tabstrM bytes in total;
     the first str__tabM lines of this array are used as
     input buffer for strings; */
    int tabi;  /* index of last entered element in string table; */
    int tabn;  /* number of valid strings in string table; */
    struct str__info_struct* prev;  /* address of previous unit; */
  } str_info_t;
str_info_t* str__infop= NULL;

static void str__end() {
    /* clean-up this module; */
  str_info_t* p;

  while(str__infop!=NULL) {
    p= str__infop->prev;
    free(str__infop);
    str__infop= p;
    }
}  /* end str__end() */

/*------------------------------------------------------------*/

static str_info_t* str_open() {
    /* open an new string client unit;
       this will allow us to process multiple o5m input files;
       return: handle of the new unit;
       ==NULL: error;
       you do not need to care about closing the unit(s); */
    static bool firstrun= true;
  str_info_t* prev;

  prev= str__infop;
  str__infop= (str_info_t*)malloc(sizeof(str_info_t));
  if(str__infop==NULL) {
    PERR("could not get memory for string buffer.")
return NULL;
    }
  str__infop->tabi= 0;
  str__infop->tabn= 0;
  str__infop->prev= prev;
  if(firstrun) {
    firstrun= false;
    atexit(str__end);
    }
  return str__infop;
}  /* end   str_open() */


static void inline str_reset() {
    /* clear string table;
   must be called before any other procedure of this module
   and may be called every time the string processing shall
   be restarted; */
  str__infop->tabi= str__infop->tabn= 0;
}  /* end   str_reset() */

static void str_read(byte** pp,char** s1p,char** s2p) {
    /* read an o5m formatted string (pair), e.g. key/val, from
       standard input buffer;
       if got a string reference, resolve it, using an internal
       string table;
       no reference is used if the strings are longer than
       250 characters in total (252 including terminators);
       pp: address of a buffer pointer;
       this pointer will be incremented by the number of bytes
       the converted protobuf element consumes;
       s2p: ==NULL: read not a string pair but a single string;
       return:
       *s1p,*s2p: pointers to the strings which have been read; */
  char* p;
  int len1,len2;
  int ref;

  p= (char*)*pp;
  if(*p==0) {  /* string (pair) given directly */
    *s1p= ++p;
    len1= strlen(p);
    p+= len1+1;
    if(s2p==NULL) {  /* single string */
        /* p= strchr(p,0)+1;   jump over second string (if any) */
      if(len1<=str__tabstrM) {
        char* tmpcharp;

        /* single string short enough for string table */
        tmpcharp= stpcpy0(str__infop->tab[str__infop->tabi],*s1p);
        tmpcharp[1]= 0;
        /* add a second terminator, just in case someone will try
           to read this single string as a string pair later; */
        if(++str__infop->tabi>=str__tabM) str__infop->tabi= 0;
        if(str__infop->tabn<str__tabM) str__infop->tabn++;
      }  /* end   single string short enough for string table */
    }  /* end   single string */
    else {  /* string pair */
      *s2p= p;
      len2= strlen(p);
      p+= len2+1;
      if(len1+len2<=str__tabstrM) {
          /* string pair short enough for string table */
        memcpy(str__infop->tab[str__infop->tabi],*s1p,len1+len2+2);
        if(++str__infop->tabi>=str__tabM) str__infop->tabi= 0;
        if(str__infop->tabn<str__tabM) str__infop->tabn++;
      }  /*  end   string pair short enough for string table */
    }  /* end   string pair */
    *pp= (byte*)p;
  }  /* end   string (pair) given directly */
  else {  /* string (pair) given by reference */
    ref= pbf_uint32(pp);
    if(ref>str__infop->tabn) {  /* string reference invalid */
      WARNv("invalid .o5m string reference: %i->%i",
        str__infop->tabn,ref)
      *s1p= "(invalid)";
      if(s2p!=NULL)  /* caller wants a string pair */
        *s2p= "(invalid)";
    }  /* end   string reference invalid */
    else {  /* string reference valid */
      ref= str__infop->tabi-ref;
      if(ref<0) ref+= str__tabM;
      *s1p= str__infop->tab[ref];
      if(s2p!=NULL)  /* caller wants a string pair */
        *s2p= strchr(str__infop->tab[ref],0)+1;
    }  /* end   string reference valid */
  }  /* end   string (pair) given by reference */
}  /* end   str_read() */

/*------------------------------------------------------------
  end   Module str_   string read module
  ------------------------------------------------------------ */



int streamFileO5m(char *filename,int sanitize,struct osmdata_t *osmdata) {
    /* open and parse an .o5m file; */
    /* return: ==0: ok; !=0: error; */
    int otype;  /*  type of currently processed object; */
  /* 0: node; 1: way; 2: relation; */
  uint32_t hisver;
  int64_t histime;
  int64_t hiscset;
  uint32_t hisuid;
  char* hisuser;
  str_info_t* str;  /* string unit handle (if o5m format) */
  bool endoffile;
  int64_t o5id;  /* for o5m delta coding */
  int32_t o5lon,o5lat;  /* for o5m delta coding */
  int64_t o5histime;  /* for o5m delta coding */
  int64_t o5hiscset;  /* for o5m delta coding */
  int64_t o5rid[3];  /* for o5m delta coding */
  byte* bufp;  /* pointer in read buffer */
#define bufsp ((char*)bufp)  /* for signed char */
  byte* bufe;  /* pointer in read buffer, end of object */
  char c;  /* latest character which has been read */
  byte b;  /* latest byte which has been read */
  int l;
  byte* bp;

  /* procedure initializations */
  str= str_open();
  /* call some initialization of string read module */
  str_reset();
  o5id= 0;
  o5lat= o5lon= 0;
  o5hiscset= 0;
  o5histime= 0;
  o5rid[0]= o5rid[1]= o5rid[2]= 0;

  /* open the input file */
  if(read_open(filename)!=0) {
    fprintf(stderr,"Unable to open %s\n",filename);
return 1;
    }
  endoffile= false;

  /* determine file type */ {
    char* p;

    read_input();
    if(*read_bufp!=0xff) {  /* cannot be an .o5m file, nor an .o5c file */
      PERR("File format neither .o5m nor .o5c")
return 1;
      }
    p= strchr(filename,0)-4;  /* get end of filename */
    if(memcmp(read_bufp,"\xff\xe0\0x04""o5m2",7)==0)
      osmdata->filetype= FILETYPE_OSM;
    else if(memcmp(read_bufp,"\xff\xe0\0x04""o5c2",7)==0)
      osmdata->filetype= FILETYPE_OSMCHANGE;
    else if(p>=filename && strcmp(p,".o5m")==0)
      osmdata->filetype= FILETYPE_OSM;
    else if(p>=filename && (strcmp(p,".o5c")==0 || strcmp(p,".o5h")==0))
      osmdata->filetype= FILETYPE_OSMCHANGE;
    else {
      WARN("File type not specified. Assuming .o5m")
      osmdata->filetype= FILETYPE_OSM;
      }
    if(osmdata->filetype==FILETYPE_OSM)
      PINFO("Processing .o5m file (not a change file).")
    else
      PINFO("Processing .o5c change file.")
    }

  /* process the input file */
  for(;;) {  /* read input file */

      /* get next object */
    read_input();
    bufp= read_bufp;
    b= *bufp; c= (char)b;

    /* care about file end */
    if(read_bufp>=read_bufe)  /* at end of input file; */
  break;

    if(endoffile) {  /* after logical end of file */
      fprintf(stderr,"osm2pgsql Warning: unexpected contents "
        "after logical end of file.\n");
  break;
      }

    /* care about header and unknown objects */
    if(b<0x10 || b>0x12) {  /* not a regular dataset id */
        if(b>=0xf0) {  /* single byte dataset */
          if(b==0xff) {  /* file start, resp. o5m reset */
          str_reset();
          o5id= 0;
          o5lat= o5lon= 0;
          o5hiscset= 0;
          o5histime= 0;
          o5rid[0]= o5rid[1]= o5rid[2]= 0;
          }
        else if(b==0xfe)
            endoffile= true;
        else
          WARNv("unknown .o5m short dataset id: 0x%02x\n",b)
        read_bufp++;
  continue;
      }  /* end   single byte dataset */
      else {  /* unknown multibyte dataset */
        if(b!=0xe0 && b!=0xdc)
              WARNv("unknown .o5m dataset id: 0x%02x\n",b)
        read_bufp++;
        l= pbf_uint32(&read_bufp);  /* jump over this dataset */
        read_bufp+= l;  /* jump over this dataset */
  continue;
      }  /* end   unknown multibyte dataset */
    }  /* end   not a regular dataset id */
    otype= b&3;

    /* object initialization */
    hisver= 0;
    histime= 0;
    hiscset= 0;
    hisuid= 0;
    hisuser= "";
    osmdata->nd_count= 0;
    osmdata->member_count= 0;

    /* read object id */
    bufp++;
    l= pbf_uint32(&bufp);
    read_bufp= bufe= bufp+l;
    osmdata->osm_id= o5id+= pbf_sint64(&bufp);

    /* do statistics on object id */
    switch(otype) {
    case 0:  /* node */
      if(osmdata->osm_id>osmdata->max_node)
        osmdata->max_node= osmdata->osm_id;
      if (osmdata->count_node == 0) {
        time(&osmdata->start_node);
      }
      osmdata->count_node++;
      if(osmdata->count_node%10000==0) printStatus(osmdata);
      break;
    case 1:  /* way */
      if(osmdata->osm_id>osmdata->max_way)
        osmdata->max_way= osmdata->osm_id;
      if (osmdata->count_way == 0) {
        time(&osmdata->start_way);
      }
      osmdata->count_way++;
      if(osmdata->count_way%1000==0) printStatus(osmdata);
      break;
    case 2:  /* relation */
      if(osmdata->osm_id>osmdata->max_rel)
        osmdata->max_rel= osmdata->osm_id;
      if (osmdata->count_rel == 0) {
        time(&osmdata->start_rel);
      }
      osmdata->count_rel++;
      if(osmdata->count_rel%10==0) printStatus(osmdata);
      break;
    default: ;
      }

    /* read history */ {
      char tmpstr[50];
      char* sp;

      hisver= pbf_uint32(&bufp);
      uint32toa(hisver,tmpstr);
      addItem(&(osmdata->tags),"osm_version",tmpstr,0);
      if(hisver!=0) {  /* history information available */
        histime= o5histime+= pbf_sint64(&bufp);
        createtimestamp(histime,tmpstr);
        addItem(&(osmdata->tags),"osm_timestamp",tmpstr, 0);
        if(histime!=0) {
            hiscset= o5hiscset+= pbf_sint32(&bufp);  /* (not used) */
          str_read(&bufp,&sp,&hisuser);
          hisuid= pbf_uint64((byte**)&sp);
          uint32toa(hisuid,tmpstr);
          addItem(&(osmdata->tags),"osm_uid",tmpstr,0);
          addItem(&(osmdata->tags),"osm_user",hisuser,0);
          }
      }  /* end   history information available */
    }  /* end   read history */

    /* perform action */
    if(bufp>=bufe) {
        /* just the id and history, i.e. this is a delete request */
      osmdata->action= ACTION_DELETE;
      switch(otype) {
      case 0:  /* node */
        osmdata->out->node_delete(osmdata->osm_id);
        break;
      case 1:  /* way */
        osmdata->out->way_delete(osmdata->osm_id);
        break;
      case 2:  /* relation */
        osmdata->out->relation_delete(osmdata->osm_id);
        break;
      default: ;
        }
      resetList(&(osmdata->tags));
      continue;  /* end processing for this object */
    }  /* end   delete request */
    else {  /* not a delete request */

        /* determine action */
      if(osmdata->filetype==FILETYPE_OSMCHANGE && hisver>1)
        osmdata->action= ACTION_MODIFY;
      else
        osmdata->action= ACTION_CREATE;

      /* read coordinates (for nodes only) */
      if(otype==0) {  /* node */
          /* read node body */
        osmdata->node_lon= (double)(o5lon+= pbf_sint32(&bufp))/10000000;
        osmdata->node_lat= (double)(o5lat+= pbf_sint32(&bufp))/10000000;
        if(!node_wanted(osmdata,osmdata->node_lat,osmdata->node_lon)) {
          resetList(&(osmdata->tags));
  continue;
          }
        reproject(&(osmdata->node_lat),&(osmdata->node_lon));
      }  /* end   node */

      /* read noderefs (for ways only) */
      if(otype==1) {  /* way */
        l= pbf_uint32(&bufp);
        bp= bufp+l;
        if(bp>bufe) bp= bufe;  /* (format error) */
        while(bufp<bp) {  /* for all noderefs of this way */
          osmdata->nds[osmdata->nd_count++]= o5rid[0]+= pbf_sint64(&bufp);
          if(osmdata->nd_count>=osmdata->nd_max)
            realloc_nodes(osmdata);
        }  /* end   for all noderefs of this way */
      }  /* end   way */

      /* read refs (for relations only) */
      else if(otype==2) {  /* relation */
          int64_t ri;  /* temporary, refid */
        int rt;  /* temporary, reftype */
        char* rr;  /* temporary, refrole */

        l= pbf_uint32(&bufp);
        bp= bufp+l;
        if(bp>bufe) bp= bufe;  /* (format error) */
        while(bufp<bp) {  /* for all references of this relation */
          ri= pbf_sint64(&bufp);
          str_read(&bufp,&rr,NULL);
          rt= (*rr++ -'0')%3;
          switch(rt) {
          case 0:  /* node */
            osmdata->members[osmdata->member_count].type= OSMTYPE_NODE;
            break;
          case 1:  /* way */
            osmdata->members[osmdata->member_count].type= OSMTYPE_WAY;
            break;
          case 2:  /* relation */
            osmdata->members[osmdata->member_count].type= OSMTYPE_RELATION;
            break;
            }
          osmdata->members[osmdata->member_count].id= o5rid[rt]+= ri;
          osmdata->members[osmdata->member_count].role= rr;
          osmdata->member_count++;
          if(osmdata->member_count>=osmdata->member_max)
            realloc_members(osmdata);
        }  /* end   for all references of this relation */
      }  /* end   relation */

      /* read node key/val pairs */
      while(bufp<bufe) {  /* for all tags of this object */
        char* k,*v,*p;

        str_read(&bufp,&k,&v);
        if(strcmp(k,"created_by") && strcmp(k,"source")) {
          p= k;
          while(*p!=0) {
            if(*p==' ') *p= '_';
            /* replace all blanks in key by underlines */
            p++;
            }
          addItem(&(osmdata->tags),k,v,0);
          }
      }  /* end   for all tags of this object */

      /* write object into database */
      switch(otype) {
      case 0:  /* node */
        if(osmdata->action==ACTION_CREATE)
          osmdata->out->node_add(osmdata->osm_id,
            osmdata->node_lat,osmdata->node_lon,&(osmdata->tags));
        else /* ACTION_MODIFY */
          osmdata->out->node_modify(osmdata->osm_id,
            osmdata->node_lat,osmdata->node_lon,&(osmdata->tags));
        break;
      case 1:  /* way */
        if(osmdata->action==ACTION_CREATE)
          osmdata->out->way_add(osmdata->osm_id,
            osmdata->nds,osmdata->nd_count,&(osmdata->tags));
        else /* ACTION_MODIFY */
          osmdata->out->way_modify(osmdata->osm_id,
            osmdata->nds,osmdata->nd_count,&(osmdata->tags));
        break;
      case 2:  /* relation */ 
        if(osmdata->action==ACTION_CREATE)
          osmdata->out->relation_add(osmdata->osm_id,
            osmdata->members,osmdata->member_count,&(osmdata->tags));
        else /* ACTION_MODIFY */
          osmdata->out->relation_modify(osmdata->osm_id,
            osmdata->members,osmdata->member_count,&(osmdata->tags));
        break;
      default: ;
        }

      /* reset temporary storage lists */
      resetList(&(osmdata->tags));

    }  /* end   not a delete request */

  }  /* end   read input file */

  /* close the input file */
  printStatus(osmdata);
  read_close();
  return 0;
}  /* streamFileO5m() */

