#include "postgres.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"
#include <utfasciitable.h>

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

Datum transliteration( PG_FUNCTION_ARGS );

PG_FUNCTION_INFO_V1( transliteration );
Datum
transliteration( PG_FUNCTION_ARGS )
{
	static char * ascii = UTFASCII;
	static uint16 asciilookup[65536] = UTFASCIILOOKUP;
	char * asciipos;

	text *source;
	unsigned char *sourcedata;
	int sourcedatalength;

        unsigned int c1,c2,c3,c4;
	unsigned int * wchardata;
	unsigned int * wchardatastart;

	text *result;
	unsigned char *resultdata;
	int resultdatalength;
	int iLen;

	if (GetDatabaseEncoding() != PG_UTF8) 
	{
		ereport(ERROR,
                                        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                                         errmsg("requires UTF8 database encoding")));
	}

	if (PG_ARGISNULL(0))
	{
		PG_RETURN_NULL();
	}

	// The original string
	source = PG_GETARG_TEXT_P(0);
	sourcedata = (unsigned char *)VARDATA(source);
	sourcedatalength = VARSIZE(source) - VARHDRSZ;

	// Intermediate wchar version of string
	wchardatastart = wchardata = (unsigned int *)palloc((sourcedatalength+1)*sizeof(int));

	// Based on pg_utf2wchar_with_len from wchar.c
        while (sourcedatalength > 0 && *sourcedata)
        {
                if ((*sourcedata & 0x80) == 0)
                {
                        *wchardata = *sourcedata++;
			wchardata++;
                        sourcedatalength--;
                }
                else if ((*sourcedata & 0xe0) == 0xc0)
                {
                        if (sourcedatalength < 2) break;
                        c1 = *sourcedata++ & 0x1f;
                        c2 = *sourcedata++ & 0x3f;
                        *wchardata = (c1 << 6) | c2;
			wchardata++;
                        sourcedatalength -= 2;
                }
                else if ((*sourcedata & 0xf0) == 0xe0)
                {
                        if (sourcedatalength < 3) break;
                        c1 = *sourcedata++ & 0x0f;
                        c2 = *sourcedata++ & 0x3f;
                        c3 = *sourcedata++ & 0x3f;
                        *wchardata = (c1 << 12) | (c2 << 6) | c3;
			wchardata++;
                        sourcedatalength -= 3;
                }
                else if ((*sourcedata & 0xf8) == 0xf0)
                {
                        if (sourcedatalength < 4) break;
                        c1 = *sourcedata++ & 0x07;
                        c2 = *sourcedata++ & 0x3f;
                        c3 = *sourcedata++ & 0x3f;
                        c4 = *sourcedata++ & 0x3f;
                        *wchardata = (c1 << 18) | (c2 << 12) | (c3 << 6) | c4;
			wchardata++;
                        sourcedatalength -= 4;
                }
                else if ((*sourcedata & 0xfc) == 0xf8)
                {
			/* table does not extend beyond 4 char long, just skip */
			if (sourcedatalength < 5) break;
			sourcedatalength -= 5;
		}
                else if ((*sourcedata & 0xfe) == 0xfc)
                {
			/* table does not extend beyond 4 char long, just skip */
			if (sourcedatalength < 6) break;
			sourcedatalength -= 6;
		}
                else
                {
			/* assume lenngth 1, silently drop bogus characters */
                        sourcedatalength--;
                }
        }
        *wchardata = 0;

	/* calc the length of transliteration string */
	resultdatalength = 0;
	wchardata = wchardatastart;
	while(*wchardata)
	{
		if (*(asciilookup + *wchardata) > 0) resultdatalength += *(ascii + *(asciilookup + *wchardata));
		wchardata++;
	}

	/* allocate & create the result */
	result = (text *)palloc(resultdatalength + VARHDRSZ);
	SET_VARSIZE(result, resultdatalength + VARHDRSZ);
	resultdata = (unsigned char *)VARDATA(result);

	wchardata = wchardatastart;
	while(*wchardata)
	{
		if (*(asciilookup + *wchardata) > 0)
		{
			asciipos = ascii + *(asciilookup + *wchardata);
			for(iLen = *asciipos; iLen > 0; iLen--)
			{
				asciipos++;
				*resultdata = *asciipos;
				resultdata++;
			}
		}
		else
		{
			ereport( WARNING, ( errcode( ERRCODE_SUCCESSFUL_COMPLETION ),
		              errmsg( "missing char: %i\n", *wchardata )));
			
		}
		wchardata++;
	}
//	*resultdata = 0;

	pfree(wchardatastart);

	PG_RETURN_TEXT_P(result);
}

