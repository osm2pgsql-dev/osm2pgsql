#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <zlib.h>
#include <bzlib.h>

#include "sanitizer.h"
#include "input.h"

int sanitizerClose(void *context);
int sanitizerProcess(void *context, char *buffer, int len);


/* UTF8sanitizer algorithm has some nasty edge cases when trying to operate
 * in a 'block at a time' mode. For example, in the following scenario:
 *
 * INPUT sequence is 2 buffers with a 6 byte char starting at X1
 *
 * [   len = 5   ]   [len = 1]
 * X1 X2 X3 X4 X5   X6
 *
 * OUTPUT: nothing is generated for first buffer 
 * This will itself cause caller to assume EOF (hopefully normal reader will read >> 5 bytes).
 * subsequent read of len=1 whille return all 6 bytes potentially causing output buffer overflow (and overwriting input data)
 *
 * The solution is to provice a small output buffer to hold anything bigger than a single byte
 *
 */


struct Context {
    long long line;
    long long chars1, chars2, chars3, chars4, chars5, chars6;
    int state, current_size;
    int long_char[6];
    int out_char[10];
    int pend;
    int verbose;
    void *file;
};


int sanitizerClose(void *context)
{
    struct Context *ctx = context;
    int r = inputClose(ctx->file);

    if (ctx->verbose) {
        fprintf(stderr, "Summary:\n");
        fprintf(stderr, "chars1: %lld\n", ctx->chars1);
        fprintf(stderr, "chars2: %lld\n", ctx->chars2);
        fprintf(stderr, "chars3: %lld\n", ctx->chars3);
        fprintf(stderr, "chars4: %lld\n", ctx->chars4);
        fprintf(stderr, "chars5: %lld\n", ctx->chars5);
        fprintf(stderr, "chars6: %lld\n", ctx->chars6);
        fprintf(stderr, "lines : %lld\n", ctx->line);
    }

    free(ctx);
    return r;
}

xmlTextReaderPtr sanitizerOpen(const char *name)
{
    struct Context *ctx = malloc (sizeof(*ctx));

    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->verbose = 0;
    ctx->state = 1;
    ctx->pend = 0;

    ctx->file = inputOpen(name);
    if (!ctx->file) {
        fprintf(stderr, "Input reader create failed\n");
        return NULL;
    }

    return xmlReaderForIO(sanitizerProcess, sanitizerClose, (void *)ctx, NULL, NULL, 0);
}


int sanitizerProcess(void *context, char *buffer, int len) 
{
  struct Context *ctx = context;
  int current_char, i, out = 0;

  while (out < len) {
      if (ctx->pend) {
          buffer[out++] = ctx->out_char[--ctx->pend];
          continue;
      }

      current_char=inputGetChar(ctx->file);
      if (inputEof(ctx->file))
          break;
 
      if ((current_char & 128) == 0) {
          //Handle_ASCII_char();
          if (current_char == '\n') 
              ctx->line++;
          else
              ctx->chars1++;
          if (ctx->state != 1) {
              if (ctx->verbose)
                  fprintf(stderr, "Error at line %lld\n", ctx->line);
              buffer[out++] = '_';
              ctx->state = 1;
          } 
          //  buffer[out++] = current_char;
          ctx->out_char[ctx->pend++] = current_char;
      } else if ((current_char & (128+64)) == 128) {
          // Handle_continue_char();
          if(ctx->state > 1) {
              ctx->state--;
              if(ctx->state==1) {
                  // long char finished
                  //for(i=1; i<ctx->current_size; i++) {
                  //  buffer[out++] = ctx->long_char[i-1];
                  //}
                  //buffer[out++] = current_char;
                  ctx->out_char[ctx->pend++] = current_char;
                  for(i=ctx->current_size-1; i>0; i--) {
                      ctx->out_char[ctx->pend++] = ctx->long_char[i-1];
                  }
              }
          } else {
              if (ctx->verbose) 
                  fprintf(stderr, "Error at line %lld\n", ctx->line);
              buffer[out++] = '_';
              ctx->state=1;
          }
      } else if ((current_char & (128+64+32)) == (128+64)) {
          //Handle_two_bytes();
          ctx->state=2;
          ctx->chars2++;
          ctx->current_size=2;
      } else if ((current_char & (128+64+32+16)) == (128+64+32)) {
          //Handle_three_bytes();
          ctx->state=3;
          ctx->chars3++;
          ctx->current_size=3;
      } else if ((current_char & (128+64+32+16+8)) == (128+64+32+16)) {
          //Handle_four_bytes();
          ctx->state=4;
          ctx->chars4++;
          ctx->current_size=4;
      } else if ((current_char & (128+64+32+16+8+4)) == (128+64+32+16+8)) {
          //Handle_five_bytes();
          ctx->state=5;
          ctx->chars5++;
          ctx->current_size=5;
      } else if ((current_char & (128+64+32+16+8+4+2)) == (128+64+32+16+8+4)) {
          //Handle_six_bytes();
          ctx->state=6;
          ctx->chars6++;
          ctx->current_size=6;
      }
      if(ctx->state>1) {
          ctx->long_char[ctx->current_size-ctx->state]=current_char;
      }
  }
  return out;
}
