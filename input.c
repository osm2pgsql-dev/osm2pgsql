#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <zlib.h>
#include <bzlib.h>

#include "sanitizer.h"
#include "input.h"

struct Input {
    char *name;
    enum { plainFile, gzipFile, bzip2File } type;
    void *fileHandle;
    int eof;
    char buf[4096];
    int buf_ptr, buf_fill;
};

int readFile(void *context, char * buffer, int len)
{
    struct Input *ctx = context;
    void *f = ctx->fileHandle;
    int l = 0;

    if (ctx->eof || (len == 0))
        return 0;
 
    switch(ctx->type) {
        case plainFile:
            l = read(*(int *)f, buffer, len);
            break;
        case gzipFile:
            l = gzread((gzFile)f, buffer, len);
            break;
        case bzip2File:
            l = BZ2_bzread((BZFILE *)f, buffer, len);
            break;
        default:
            fprintf(stderr, "Bad file type\n");
            break;
    }

    if (l < 0) {
        fprintf(stderr, "File reader received error %d\n", l);
        l = 0;
    }
    if (!l)
        ctx->eof = 1;

    return l;
}

char inputGetChar(void *context)
{
    struct Input *ctx = context;

    if (ctx->buf_ptr == ctx->buf_fill) {
        ctx->buf_fill = readFile(context, &ctx->buf[0], sizeof(ctx->buf));
        ctx->buf_ptr = 0;
        if (ctx->buf_fill == 0)
            return 0;
        if (ctx->buf_fill < 0) {
            perror("Error while reading file");
            exit(1);
        }
    }
    //readFile(context, &c, 1);
    return ctx->buf[ctx->buf_ptr++];
}

int inputEof(void *context)
{
    return ((struct Input *)context)->eof;
}


void *inputOpen(const char *name)
{
    const char *ext = strrchr(name, '.');
    struct Input *ctx = malloc (sizeof(*ctx));

    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(*ctx));

    ctx->name = strdup(name);

    if (ext && !strcmp(ext, ".gz")) {
        ctx->fileHandle = (void *)gzopen(name, "rb");
        ctx->type = gzipFile;
    } else if (ext && !strcmp(ext, ".bz2")) {
        ctx->fileHandle = (void *)BZ2_bzopen(name, "rb");
        ctx->type = bzip2File;
    } else {
        int *pfd = malloc(sizeof(pfd));
        if (pfd) {
            if (!strcmp(name, "-")) {
                *pfd = STDIN_FILENO;
            } else {
                int flags = O_RDONLY;
#ifdef O_LARGEFILE
                flags |= O_LARGEFILE;
#endif
                *pfd = open(name, flags);
                if (*pfd < 0) {
                    free(pfd);
                    pfd = NULL;
                }
            }
        }
        ctx->fileHandle = (void *)pfd;
        ctx->type = plainFile;
    }
    if (!ctx->fileHandle) {
        fprintf(stderr, "error while opening file %s\n", name);
        exit(10);
    }
    ctx->buf_ptr = 0;
    ctx->buf_fill = 0;
    return (void *)ctx;
}

int inputClose(void *context)
{
    struct Input *ctx = context;
    void *f = ctx->fileHandle;

    switch(ctx->type) {
        case plainFile:
            close(*(int *)f);
            free(f);
            break;
        case gzipFile:
            gzclose((gzFile)f);
            break;
        case bzip2File:
            BZ2_bzclose((BZFILE *)f);
            break;
        default:
            fprintf(stderr, "Bad file type\n");
            break;
    }

    free(ctx->name);
    free(ctx);
    return 0;
}
