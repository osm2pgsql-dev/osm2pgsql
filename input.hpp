#ifndef INPUT_H
#define INPUT_H

#include <libxml/xmlreader.h>
struct Input;

int readFile(struct Input *context, char * buffer, int len);
int inputClose(struct Input *context);
struct Input *inputOpen(const char *name);
char inputGetChar(struct Input *context);
int inputEof(struct Input *context);
xmlTextReaderPtr inputUTF8(const char *name);

#endif
