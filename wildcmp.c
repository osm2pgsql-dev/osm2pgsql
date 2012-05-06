/* Wildcard matching.

   heavily based on wildcmp.c copyright 2002 Jim Kent
   
*/
#include <ctype.h>
#include "wildcmp.h"

static int subMatch(char *str, char *wild)
/* Returns number of characters that match between str and wild up
 * to the next wildcard in wild (or up to end of string.). */
{
int len = 0;

for(;;)
    {
    if(toupper(*str++) != toupper(*wild++) )
        return(0);
    ++len;
    switch(*wild)
        {
        case 0:
        case '?':
        case '*':
            return(len);
        }
    }
}

int wildMatch(char *wildCard, char *string)
/* does a case sensitive wild card match with a string.
 * * matches any string or no character.
 * ? matches any single character.
 * anything else etc must match the character exactly.

returns NO_MATCH, FULL_MATCH or WC_MATCH defined in wildcmp.h
 
*/
{
int matchStar = 0;
int starMatchSize;
int wildmatch=0;

for(;;)
    {
NEXT_WILD:
    switch(*wildCard)
	{
	case 0: /* end of wildcard */
	    {
	    if(matchStar)
		{
		while(*string++)
                    ;
		return wildmatch ? WC_MATCH : FULL_MATCH;
                }
            else if(*string)
		return NO_MATCH;
            else {
                return wildmatch ? WC_MATCH : FULL_MATCH;
                }
	    }
	case '*':
	    wildmatch = 1;
	    matchStar = 1;
	    break;
	case '?': /* anything will do */
	    wildmatch = 1;
	    {
	    if(*string == 0)
	        return NO_MATCH; /* out of string, no match for ? */
	    ++string;
	    break;
	    }
	default:
	    {
	    if(matchStar)
    	        {
		for(;;)
		    {
		    if(*string == 0) /* if out of string no match */
		        return NO_MATCH;

		    /* note matchStar is re-used here for substring
		     * after star match length */
		    if((starMatchSize = subMatch(string,wildCard)) != 0)
		        {
			string += starMatchSize;
			wildCard += starMatchSize;
			matchStar = 0;
			goto NEXT_WILD;
		        }
		    ++string;
		    }
	        }

	    /* default: they must be equal or no match */
	    if(toupper(*string) != toupper(*wildCard))
		return NO_MATCH;
	    ++string;
	    break;
	    }
	}
    ++wildCard;
    }
}
