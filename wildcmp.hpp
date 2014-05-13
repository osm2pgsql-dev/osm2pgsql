#ifndef WILDCMP_H
#define WILDCMP_H

#define NO_MATCH 0
#define FULL_MATCH 1
#define WC_MATCH 2

int wildMatch(const char *wildCard, const char *string);

#endif
