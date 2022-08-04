﻿/*****************************************************************************\
*                                                                             *
*   Filename	    2clip.c						      *
*									      *
*   Contents	    Pipe the output of a console application to the clipboard *
*									      *
*   Notes:	    							      *
*									      *
*   History								      *
*    1997-05-09 JFL Adapted from Windows Developer's Journal sample.          *
*		    Changed variables names to hungarian convention.	      *
*		    Added loop to wait for the completion of the input task.  *
*		    Added command line analysis, for the /? argument.	      *
*		    Version 1.0.					      *
*    2004-08-11 JFL Added support for input sizes > 64 KB.		      *
*		    Version 1.1.					      *
*    2008-03-12 JFL Removed the complaint about an empty input buffer.        *
*		    Version 1.1a.					      *
*    2012-03-01 JFL Changed this to a console program.			      *
*		    Version 1.2.					      *
*    2012-10-18 JFL Added my name in the help. Version 1.2.1.                 *
*    2014-03-31 JFL Added support for local code pages.                       *
*		    Added options -A, -O , -u, -U to override the default CP. *
*		    Version 1.3.					      *
*    2014-04-18 JFL Fixed a bug which prevented work in code page 1252.       *
*		    Version 1.3.1.					      *
*    2014-11-14 JFL Change NUL characters to spaces, to avoid truncating the  *
*		    clipboard data at the first NUL in the input stream.      *
*		    Removed the sz prefix in buffer variables names.	      *
*		    Version 1.3.2.					      *
*    2014-12-04 JFL Added my name and email in the help.                      *
*    2017-12-05 JFL Added options -h and -r, for HTML and RTF. Version 1.4.   *
*    2018-01-08 JFL Remove the UTF8 BOM when writing RTF. Version 1.4.1.      *
*    2018-08-31 JFL Added the -d debug option. Version 1.4.2.		      *
*    2019-04-18 JFL Use the version strings from the new stversion.h. V.1.4.3.*
*    2019-06-12 JFL Added PROGRAM_DESCRIPTION definition. Version 1.4.4.      *
*    2019-10-31 JFL Added option -z to stop input on Ctrl-Z. Version 1.5.     *
*    2020-04-25 JFL Rewrote ReportWin32Error as PrintWin32Error(); Added new  *
*		    PrintCError(), and use them instead of COMPLAIN().        *
*		    Converted the source to UTF-8 and output help as Unicode. *
*		    Added option --lock in debug mode. Version 1.6.	      *
*    2020-08-29 JFL Merged in changes from another PrintWin32Error(). V 1.6.1.*
*                   Added option -8 as an alias for option -U to input UTF-8, *
*                   and option -16 as an alias for option -u to input UTF-16. *
*    2020-08-30 JFL Added the autodetection of UTF encodings.                 *
*		    Version 1.7.					      *
*    2022-02-24 JFL Fixed the input pipe and redirection detection.           *
*		    Version 1.7.1.					      *
*    2022-06-27 JFL Added option -N to remove the final CRLF. Version 1.8.    *
*                   							      *
*         © Copyright 2016 Hewlett Packard Enterprise Development LP          *
* Licensed under the Apache 2.0 license - www.apache.org/licenses/LICENSE-2.0 *
\*****************************************************************************/

#define PROGRAM_DESCRIPTION "Copy text from stdin to the Windows clipboard"
#define PROGRAM_NAME    "2clip"
#define PROGRAM_VERSION "1.8"
#define PROGRAM_DATE    "2022-06-27"

#define _UTF8_SOURCE	/* Tell MsvcLibX that this program generates UTF-8 output */

#ifndef _WIN32
#error "Unsuported OS. This program is Windows-specific."
#endif

#define _CRT_SECURE_NO_WARNINGS 1
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <conio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
/* SysToolsLib include files */
#include "debugm.h"	/* SysToolsLib debug macros */
#include "stversion.h"	/* SysToolsLib version strings. Include last. */

// My favorite string comparison routines.
#define streq(s1, s2) (!lstrcmp(s1, s2))     /* Test if strings are equal */

#define BLOCKSIZE (4096)	// Number of characters that will be allocated in each loop.

/* Local definitions */

#define CP_NULL ((UINT)-1) /* No code page. Can't use 0 as CP_ACP is 0 */
#define CP_AUTODETECT ((UINT)-2) /* For autodetecting the code page */
#define CP_UTF16    1200
#define CP_UTF16LE  1200
#define CP_UTF16BE  1201
#define CP_UTF32   12000
#define CP_UTF32LE 12000
#define CP_UTF32BE 12001

/* Global variables */

DEBUG_GLOBALS

/* Function prototypes */

void usage(void);
int IsSwitch(char *pszArg);
int PrintCError(const char *pszExplanation, ...);
int PrintWin32Error(const char *pszExplanation, ...);
int ToClip(const char* pBuffer, size_t lBuf, UINT cf);
int ToClipW(const WCHAR* pwBuffer, size_t nWChars);
int is_pipe(FILE *f);		/* Check if a file handle is a pipe */
UINT DetectUTF(const char *pBuffer, size_t nBufSize);

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:	    main						      |
|									      |
|   Description:    Program main routine				      |
|									      |
|   Parameters:     int argc		    Number of arguments 	      |
|		    char *argv[]	    List of arguments		      |
|									      |
|   Returns:	    The return code to pass to the OS.			      |
|									      |
|   Notes:	    This routine is renamed main_, else WinMain would not be  |
|		    processed.						      |
|									      |
|   History:								      |
|									      |
|    2001-09-18 JFL Created this routine.				      |
|    2008-03-12 JFL Removed the complaint about an empty input buffer.        |
|    2014-03-31 JFL Added the conversion to Unicode.                          |
*									      *
\*---------------------------------------------------------------------------*/

int main(int argc, char *argv[]) {
  size_t  nTotal = 0;		// Total number of characters read.
  char   *pBuffer;
  int i;
  UINT codepage = CP_AUTODETECT;
  UINT uConsoleCP = GetConsoleOutputCP();
  UINT uWindowsCP = GetACP();	/* From HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage\ACP */
  UINT type = 0;
  int iDone;
  int iRet;
  int isHTML = FALSE;
  int isRTF = FALSE;
  int iCtrlZ = FALSE;		/* If true, stop input on a Ctrl-Z */
  int iTrimFinalCRLF = FALSE;	/* If true, remove the CRLF in the end of the input */

  /* Process arguments */
  for (i=1; i<argc; i++) {
    char *arg = argv[i];
    if (IsSwitch(arg)) {	    /* It's a switch */
      if (streq(arg+1, "?")) {		/* -?: Help */
	usage();                            /* Display help */
	return 0;
      }
      if (streq(arg+1, "8")) {		/* -8: Assume the input is UTF-8 text */
	codepage = CP_UTF8;
	continue;
      }
      if (streq(arg+1, "16")) {		/* -16: Assume the input is UTF-16 text */
	codepage = CP_NULL;
	continue;
      }
      if (streq(arg+1, "A")) {		/* Assume the input is ANSI text */
	codepage = CP_ACP;
	continue;
      }
      DEBUG_CODE(
      if (streq(arg+1, "d")) {		/* -d: Debug */
	iDebug = 1;
	continue;
      }
      )
      if (streq(arg+1, "h")) {		/* Register the input as HTML */
	type = RegisterClipboardFormat("HTML Format");
	isHTML = TRUE;
	continue;
      }
      DEBUG_CODE(
      if (streq(arg+1, "-lock")) {	/* --lock: Lock the clipboard, for testing access conflicts */
      	HWND hWND = FindWindow("ConsoleWindowClass", NULL);
	if (OpenClipboard(hWND)) {	  /* Give clipboard ownership to the console window */
	  printf("Clipboard locked. Press ESC to exit.\n");
	  while ((i = _getch()) != EOF) if (i == '\x1B') break;
	  CloseClipboard();
	  printf("Clipboard unlocked.\n");
	  exit(0);
	} else {
	  PrintWin32Error("Can't lock the clipboard");
	  exit(1);
	}
      }
      )
      if (streq(arg+1, "N")) {		/* Trim the final CRLF */
	iTrimFinalCRLF = TRUE;
	continue;
      }
      if (streq(arg+1, "O")) {		/* Assume the input is OEM text */
	codepage = CP_OEMCP;
	continue;
      }
      if (streq(arg+1, "r")) {		/* Register the input as RTF */
	type = RegisterClipboardFormat("Rich Text Format");
	isRTF = TRUE;
	continue;
      }
      if (streq(arg+1, "u")) {		/* Assume the input is Unicode */
	codepage = CP_UTF16;
	continue;
      }
      if (streq(arg+1, "U")) {		/* Assume the input is UTF-8 text */
	codepage = CP_UTF8;
	continue;
      }
      if (streq(arg+1, "V")) {	/* Display version */
	puts(DETAILED_VERSION);
	exit(0);
      }
      if (streq(arg+1, "z")) {		/* -z: Stop input on Ctrl-Z */
	iCtrlZ = TRUE;
	continue;
      }
      fprintf(stderr, PROGRAM_NAME ".exe: Unsupported switch %s ignored.\n", arg);
      continue;
    }
    fprintf(stderr, PROGRAM_NAME ".exe: Unexpected argument %s ignored.\n", arg);
  }

  /* Go for it */
  DEBUG_PRINTF(("The selected code page is %d\n", codepage));

  /* Force stdin to untranslated */
  _setmode( _fileno( stdin ), _O_BINARY );

  pBuffer = (char*)malloc(0);
  while (!feof(stdin)) {
    pBuffer = realloc(pBuffer, nTotal + BLOCKSIZE);
    if (!pBuffer) {
      PrintCError("Can't read all input");
      exit(1);
    }
    if (!iCtrlZ) {
      nTotal += fread(pBuffer+nTotal, 1, BLOCKSIZE, stdin);
    } else { /* Read characters 1 by 1, to avoid blocking if the EOF character is not on a BLOCKSIZE boundary */
      int nRead;
      for (nRead = 0; nRead < BLOCKSIZE; nRead++) {
	char c;
	if (!fread(&c, 1, 1, stdin)) break;
	if (c == '\x1A') break; /* We got a SUB <==> EOF character */
	pBuffer[nTotal+nRead] = c;
      }
      nTotal += nRead;
      if (nRead < BLOCKSIZE) break;
    }
    Sleep(0);	    // Release end of time-slice
  }
  if (iTrimFinalCRLF) { /* Optionally remove the final CRLF */
    if (nTotal && (pBuffer[nTotal-1] == '\n')) nTotal -= 1;
    if (nTotal && (pBuffer[nTotal-1] == '\r')) nTotal -= 1;
  }
  if (nTotal > 0) {
    WCHAR *pwUnicodeBuf = (WCHAR *)pBuffer;
    if (type) {	/* This is a specific data type */
      char header[256];
      if (isHTML) { // https://msdn.microsoft.com/en-us/library/aa767917.aspx
      	char *pszFormat = "Version:0.9\r\nStartHTML:%d\r\nEndHTML:%d\r\nStartFragment:%d\r\nEndFragment:%d\r\n";
      	int nHead = 50; /* Initial estimate for the number of characters in the header. (After formatting!) */
      	char *pc;
      	int iFragment = 0;
      	int iEndFragment = (int)nTotal;
	if (((pc = strstr(pBuffer, "<body")) != NULL) && ((pc = strstr(pc, ">")) != NULL)) iFragment = (int)(pc+1-pBuffer);
	if ((pc = strstr(pBuffer, "</body")) != NULL) iEndFragment = (int)(pc-pBuffer);
      	for (i = 0; i != nHead; nHead = i) { // Repeat until the string length stabilizes
	  i = sprintf(header, pszFormat, nHead, nHead+nTotal, nHead+iFragment, nHead+iEndFragment);
	}
	pBuffer = realloc(pBuffer, nTotal + nHead);
	memmove(pBuffer+nHead, pBuffer, nTotal);
	memmove(pBuffer, header, nHead);
	nTotal += nHead;
      } else if (isRTF) { // We must remove the BOM, which confuses MS Word
      	if ((nTotal >= 3) && !strncmp(pBuffer, "\xEF\xBB\xBF", 3)) {
      	  pBuffer += 3;		// Skip the UTF8 BOM
      	  nTotal -= 3;
      	}
      }
      iDone = ToClip(pBuffer, nTotal, type);
    } else {	/* This is text. Convert it to Unicode to avoid codepage issues */
      /* Use heuristics to select a codepage, if none was provided */
      if (codepage == CP_AUTODETECT) {
	codepage = DetectUTF(pBuffer, nTotal); /* Is this a UTF encoding */
	if (!codepage) { /* If not, assume it's either the Windows or the console CP */
	  if (is_pipe(stdin) || isatty(_fileno(stdin))) {
	    codepage = uConsoleCP;
	    DEBUG_PRINTF(("It's not UTF, and coming from the console or a pipe => Using the current console code page %u\n", codepage));
	  } else {
	    codepage = uWindowsCP;
	    DEBUG_PRINTF(("It's not UTF, and coming from a file => Using the Windows system code page %u\n", codepage));
	  }
	}
      }
      if (codepage != CP_UTF16) {
	pwUnicodeBuf = (WCHAR *)malloc(2*(nTotal));
	if (!pwUnicodeBuf) {
	  PrintCError("Can't convert the intput to Unicode");
	  exit(1);
	}
	nTotal = MultiByteToWideChar(codepage,		/* CodePage, (CP_ACP, CP_OEMCP, CP_UTF8, ...) */
				     0,			/* dwFlags, */
				     pBuffer,		/* lpMultiByteStr, */
				     (int)nTotal,		/* cbMultiByte, */
				     pwUnicodeBuf,	/* lpWideCharStr, */
				     (int)nTotal		/* cchWideChar, */
				     );
	if (!nTotal) {
	  PrintWin32Error("Can't convert the intput to Unicode");
	  exit(1);
	}
      } else {
	nTotal /= 2;	/* # of wide unicode characters */
      }
      iDone = ToClipW(pwUnicodeBuf, nTotal);
    }
    iRet = !iDone; /* Return exit code 0 is the copy was done; 1 if it was not */
  } else {
    /* This is actually not a bug: The input CAN be empty */
    iRet = 0;
  }

  return iRet;
}

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:       usage                                                     |
|                                                                             |
|   Description:    Display a brief help for this program                     |
|                                                                             |
|   Parameters:     None                                                      |
|                                                                             |
|   Returns:        None                                                      |
|                                                                             |
|   Notes:	    Use tabulations in the help string to ensure alignement   |
|		    in the message box. 				      |
|                                                                             |
|   Updates:                                                                  |
|                                                                             |
|    1997-05-09 JFL Created this routine				      |
*                                                                             *
\*---------------------------------------------------------------------------*/

void usage(void) {
  UINT cpANSI = GetACP();
  UINT cpOEM = GetOEMCP();
  UINT cpCurrent = GetConsoleOutputCP();

  printf(
PROGRAM_NAME_AND_VERSION " - " PROGRAM_DESCRIPTION "\n\
\n\
Usage:\n\
\n\
    <command> | " PROGRAM_NAME " [switches]\n\
\n\
Switches:\n\
  -?        Display this help screen\n\
  -A        Assume input is ANSI text (Windows System code page %u)\n\
"
#ifdef _DEBUG
"\
  -d        Output debug information.\n\
"
#endif
"\
  -h        Register input as HTML\n\
"
#ifdef _DEBUG
"\
  --lock    Lock the clipboard, for testing access conflicts.\n\
"
#endif
"\
  -O        Assume input is OEM text (Code page %u)\n\
  -r        Register input as RTF\n\
  -u|-16    Assume input is UTF-16 text (Unicode)\n\
  -U|-8     Assume input is UTF-8 text (Code page 65001)\n\
  -V        Display the program version\n\
  -z        Stop input on a Ctrl-Z (aka. SUB or EOF) character\n\
\n\
Default input encoding: UTF-8 or UTF-16 if valid; Else for data coming through\n\
a pipe the current console code page (Code page %u); Else for files the Windows\n\
System Code Page (Code page %u).\n\
\n"
"Author: Jean-François Larvoire"
" - jf.larvoire@hpe.com or jf.larvoire@free.fr\n"
, cpANSI, cpOEM, cpCurrent, cpANSI);

  return;
}

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function	    IsSwitch						      |
|									      |
|   Description     Test if a command line argument is a switch.	      |
|									      |
|   Parameters      char *pszArg					      |
|									      |
|   Returns	    TRUE or FALSE					      |
|									      |
|   Notes								      |
|									      |
|   History								      |
|    1997-03-04 JFL Created this routine				      |
|    2016-08-25 JFL "-" alone is NOT a switch.				      |
*									      *
\*---------------------------------------------------------------------------*/

int IsSwitch(char *pszArg) {
  switch (*pszArg) {
    case '-':
#if defined(_WIN32) || defined(_MSDOS)
    case '/':
#endif
      return (*(short*)pszArg != (short)'-'); /* "-" is NOT a switch */
    default:
      return FALSE;
  }
}

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function	    PrintCError						      |
|									      |
|   Description     Print C library error messages with a consistent format   |
|									      |
|   Parameters      char *pszFormat	The error context, or NULL            |
|		    ...			Optional arguments		      |
|		    							      |
|   Returns	    The number of characters written			      |
|									      |
|   Notes	    							      |
|		    							      |
|   History								      |
|    2020-04-25 JFL Created this routine				      |
*									      *
\*---------------------------------------------------------------------------*/

int PrintCError(const char *pszFormat, ...) {
  va_list vl;
  int n;
  int e = errno; /* The initial C errno when this routine starts */

  n = fprintf(stderr, PROGRAM_NAME EXE_SUFFIX ": Error: ");

  if (pszFormat) {
    va_start(vl, pszFormat);
    n += vfprintf(stderr, pszFormat, vl);
    n += fprintf(stderr, ". ");
    va_end(vl);
  }

  n += fprintf(stderr, "%s.\n", strerror(e));

  return n;
}

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function	    PrintWin32Error					      |
|                                                                             |
|   Description     Display a message with the last WIN32 error.	      |
|                   							      |
|   Parameters      char *pszFormat	The error context, or NULL            |
|		    ...			Optional arguments		      |
|                   							      |
|   Returns	    The number of characters written        		      |
|                   							      |
|   Notes	    							      |
|                   							      |
|   History								      |
|    1997-03-25 JFL Created another PrintWin32Error routine.		      |
|    1998-11-19 JFL Created routine ReportWin32Error.			      |
|    2005-06-10 JFL Added the message description, as formatted by the OS.    |
|    2010-10-08 JFL Adapted to a console application, output to stderr.       |
|    2015-01-15 JFL Converted PrintWin32Error to using UTF-8.		      |
|    2018-11-02 JFL Allow pszExplanation to be NULL.			      |
|                   Make sure WIN32 msg and explanation are on the same line. |
|    2019-09-27 JFL Fixed a formatting error.                                 |
|                   Prepend the program name to the output.		      |
|    2020-04-25 JFL Redesigned to avoid using a fixed size buffer.	      |
|                   Reordered the output so that it sounds more natural.      |
|                   Renamed ReportWin32Error as PrintWin32Error.	      |
|    2020-08-28 JFL If no message found, display both dec. and hexa. codes.   |
|    2020-08-29 JFL Merged in 2015-01-15 UTF-8 support from the other routine.|
*                                                                             *
\*---------------------------------------------------------------------------*/

#define WIN32_ERROR_PREFIX PROGRAM_NAME EXE_SUFFIX ": Error: "
// #define WIN32_ERROR_PREFIX "Error: "

int PrintWin32Error(const char *pszFormat, ...) {
  va_list vl;
  int n = 0;
  size_t l;
  LPWSTR lpwMsgBuf;
  LPSTR  lpMsgBuf;
  DWORD dwError = GetLastError(); // The initial WIN32 error when this routine starts

#if defined(WIN32_ERROR_PREFIX)
  n += fprintf(stderr, "%s", WIN32_ERROR_PREFIX);
#endif

  if (pszFormat) {
    va_start(vl, pszFormat);
    n += vfprintf(stderr, pszFormat, vl);
    n += fprintf(stderr, ". ");
    va_end(vl);
  }

  l = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		     FORMAT_MESSAGE_FROM_SYSTEM | 
		     FORMAT_MESSAGE_IGNORE_INSERTS,
		     NULL,
		     dwError,
		     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		     (LPWSTR)&lpwMsgBuf, // Address of the allocated buffer address
		     0,
		     NULL);
  if (l) {
    // Remove the trailing dot and CRLF, if any
    // (The CR causing problems later on, as the LF would be expanded with a second CR)
    if (l && (lpwMsgBuf[l-1] == L'\n')) lpwMsgBuf[--l] = L'\0';
    if (l && (lpwMsgBuf[l-1] == L'\r')) lpwMsgBuf[--l] = L'\0';
    if (l && (lpwMsgBuf[l-1] == L'.')) lpwMsgBuf[--l] = L'\0';
    // Convert the trimmed message to UTF-8
    l = 2*(l+1); // Worst case increase for UTF16 to UTF8 conversion
    lpMsgBuf = (char *)LocalAlloc(LPTR, l);
    if (lpMsgBuf) {
      l = WideCharToMultiByte(CP_UTF8, 0, lpwMsgBuf, -1, lpMsgBuf, (int)l, NULL, NULL);
      if (l) n += fprintf(stderr, "%s.\n", lpMsgBuf);
      LocalFree(lpMsgBuf); // Free the UTF-8 buffer
    }
    LocalFree(lpwMsgBuf); // Free the UTF-16 buffer
  }
  if (!l) { // We did not find a description string for this error code, or could not display it
    n += fprintf(stderr, "Win32 error %lu (0x%08lX).\n", dwError, dwError);
  }

  return n;
}

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:	    ToClip						      |
|									      |
|   Description:    Copy a buffer of text to the clipboard		      |
|									      |
|   Parameters:     const char *pBuffer     The data buffer to copy	      |
|		    size_t lBuf		    The number of bytes to copy       |
|		    UINT cf		    Clipboard Format. Dflt: CF_TEXT   |
|									      |
|   Returns:	    TRUE if success, or FALSE if FAILURE.		      |
|									      |
|   History:								      |
|    1997-05-09 JFL Adapted from Windows Developer's Journal sample           |
|    2004-08-11 JFL Fixed a bug that caused the last character to be lost.    |
|    2014-11-14 JFL Change NUL characters to spaces, to avoid truncating the  |
|		    clipboard data at the first NUL in the input stream.      |
|    2017-12-05 JFL Added the cf argument.                                    |
*									      *
\*---------------------------------------------------------------------------*/

int ToClip(const char* pBuffer, size_t lBuf, UINT cf)
    {
    int iResult = FALSE;	/* assume failure */
    if (!cf) cf = CF_TEXT;	/* If not set, use the default Clipboard Format */

    if (OpenClipboard(NULL))
        {
	if (EmptyClipboard())
            {
            HGLOBAL hClipData = GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE, lBuf+1);
            if (hClipData)
                {
                size_t n;
		char* pGlobalBuf = (char *)GlobalLock(hClipData);
		CopyMemory(pGlobalBuf, pBuffer, lBuf);
		for (n=0; n<lBuf; n++) { /* Change NUL characters to spaces */
		  if (!pGlobalBuf[n]) pGlobalBuf[n] = pGlobalBuf[n] = ' ';
		}
		pGlobalBuf[lBuf++] = '\0'; // ~~JFL 2004-08-11 Append a NUL.
                GlobalUnlock(hClipData);
                if (SetClipboardData(cf, hClipData))
		    iResult = TRUE; /* finally, success! */
		else
		    PrintWin32Error("Failed to write to the clipboard");
                CloseClipboard();
                }
            else
                PrintWin32Error("Insufficient memory for the clipboard");
            }
        else
            PrintWin32Error("Could not empty the clipboard");
        }
    else
        PrintWin32Error("Could not open the clipboard");

    return iResult;
    }

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function:	    ToClipW						      |
|									      |
|   Description:    Copy a buffer of Unicode text to the clipboard	      |
|									      |
|   Parameters:     const WCHAR *pwBuffer	The data buffer to copy	      |
|		    size_t nWChars		The number of WCHARs to copy  |
|									      |
|   Returns:	    TRUE if success, or FALSE if FAILURE.		      |
|									      |
|   History:								      |
|    2014-03-31 JFL Created this routine, based on the ANSI ToClip().         |
|    2014-11-14 JFL Change NUL characters to spaces, to avoid truncating the  |
|		    clipboard data at the first NUL in the input stream.      |
*									      *
\*---------------------------------------------------------------------------*/

int ToClipW(const WCHAR* pwBuffer, size_t nWChars)
    {
    int iResult = FALSE;     /* assume failure */

    if (OpenClipboard(NULL))
        {
	if (EmptyClipboard())
            {
            HGLOBAL hClipData = GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE, (nWChars+1)*sizeof(WCHAR));
            if (hClipData)
                {
                size_t n;
		WCHAR* pGlobalBuf = (WCHAR *)GlobalLock(hClipData);
		CopyMemory(pGlobalBuf, pwBuffer, nWChars*sizeof(WCHAR));
		for (n=0; n<nWChars; n++) { /* Change NUL characters to spaces */
		  if (!pGlobalBuf[n]) pGlobalBuf[n] = pGlobalBuf[n] = ' ';
		}
		pGlobalBuf[nWChars++] = '\0'; // ~~JFL 2004-08-11 Append a NUL.
                GlobalUnlock(hClipData);
                if (SetClipboardData(CF_UNICODETEXT, hClipData))
		    iResult = TRUE; /* finally, success! */
		else
		    PrintWin32Error("Failed to write to the clipboard");
                CloseClipboard();
                }
            else
                PrintWin32Error("Insufficient memory for the clipboard");
            }
        else
            PrintWin32Error("Could not empty the clipboard");
        }
    else
        PrintWin32Error("Could not open the clipboard");

    return iResult;
    }

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function	    is_pipe						      |
|									      |
|   Description     Check if a FILE is a pipe			 	      |
|									      |
|   Parameters	    FILE *f		    The file to test		      |
|									      |
|   Returns	    TRUE if the FILE is a pipe				      |
|									      |
|   Notes	    							      |
|		    							      |
|   History								      |
*									      *
\*---------------------------------------------------------------------------*/

int is_pipe(FILE *f) {
  int err;
  struct stat buf;			/* Use MSC 6.0 compatible names */
  int h;

  h = fileno(f);			/* Get the file handle */
  err = fstat(h, &buf);			/* Get information on that handle */
  if (err) return FALSE;		/* Cannot tell more if error */
  return (S_ISFIFO(buf.st_mode));	/* It's a FiFo */
}

/*---------------------------------------------------------------------------*\
*                                                                             *
|   Function	    DetectUTF						      |
|									      |
|   Description     Detect if a buffer contains a kind of UTF text encoding   |
|		    							      |
|   Parameters      const char *pBuffer		A data buffer with text	      |
|		    size_t nBufSize		The number of bytes in buffer |
|		    							      |
|   Returns	    0 | CP_UTF7 | CP_UTF8 | CP_UTF16 | CP_UTF16BE	      |
|		    							      |
|   History								      |
|    Long ago	JFL Implemented UTF-7/8/16 BOM detection in conv.c.	      |
|    2020-08-11 JFL Implemented the detection of UTF-8 and UTF-16 without BOM.|
|    2020-08-30 JFL Created this routine from code in conv.c.                 |
*									      *
\*---------------------------------------------------------------------------*/

UINT DetectUTF(const char *pBuffer, size_t nBufSize) {
  DEBUG_CODE(
  char *pszMsg = NULL;
  )
  UINT cp = 0;
  
  /* First look for a Unicode BOM: https://en.wikipedia.org/wiki/Byte_order_mark */
  if ((nBufSize >= 3) && !strncmp(pBuffer, "\xEF\xBB\xBF", 3)) { /* UTF-8 BOM */
    cp = CP_UTF8;
    DEBUG_CODE(pszMsg = "Found a UTF-8 BOM";)
  } else if ((nBufSize >= 4) && (!strncmp(pBuffer, "\x2B\x2F\x76", 3)) && strchr("\x38\x39\x2B\x2F", pBuffer[3])) { /* UTF-7 BOM */
    cp = CP_UTF7;
    DEBUG_CODE(pszMsg = "Found a UTF-7 BOM";)
  } else if ((nBufSize >= 2) && !strncmp(pBuffer, "\xFF\xFE", 2)) { /* UTF-16 BOM */
    cp = CP_UTF16;
    DEBUG_CODE(pszMsg = "Found a UTF-16 BOM";)
  } else if ((nBufSize >= 2) && !strncmp(pBuffer, "\xFE\xFF", 2)) { /* UTF-16 BE BOM */
    cp = CP_UTF16BE;
    DEBUG_CODE(pszMsg = "Found a UTF-16 BE BOM";)
  } else { /* No Unicode BOM. Try detecting UTF-8 or UTF-16 without BOM */
    size_t n;
    size_t nNonASCII = 0;
    size_t nOddNUL = 0;
    size_t nEvenNUL = 0;
    int isValidUTF8 = TRUE;
    for (n=0; n<nBufSize; n++) {
      char c = pBuffer[n];
      if (!c) {
	if (n & 1) {
	  nOddNUL += 1;
	} else {
	  nEvenNUL += 1;
	}
	continue;
      }
/* See https://en.wikipedia.org/wiki/UTF-8 */
#define IS_ASCII(c)     ((c&0x80) == 0)
#define IS_LEAD_BYTE(c) ((c&0xC0) == 0xC0)
#define IS_TAIL_BYTE(c) ((c&0xC0) == 0x80)
      if (IS_ASCII(c)) continue;
      nNonASCII += 1;
      if (isValidUTF8) { /* No need to keep validating if we already know it's invalid */
	if (IS_LEAD_BYTE(c)) {
	  int nTailBytesExpected = 0;
	  if ((c&0x20) == 0) {
	    nTailBytesExpected = 1;
	    if ((c == '\xC0') || (c == '\xC1')) isValidUTF8 = FALSE; /* Overlong encoding of 7-bits ASCII */
	  } else if ((c&0x10) == 0) {
	    nTailBytesExpected = 2;
	  } else if ((c&0x08) == 0) {
	    nTailBytesExpected = 3;
	    if ((c >= '\xF5') && (c <= '\xF7')) isValidUTF8 = FALSE; /* Encoding of invalid Unicode chars > \u10FFFF */
	  } else {	/* No valid Unicode character requires a 5-bytes or more encoding */
	    isValidUTF8 = FALSE;
	    continue;
	  }
	  /* Then make sure that the expected tail bytes are all there */
	  for ( ; nTailBytesExpected && (++n < nBufSize); nTailBytesExpected--) {
	    c = pBuffer[n];
	    if (!IS_ASCII(c)) nNonASCII += 1;
	    if (!IS_TAIL_BYTE(c)) { /* Invalid UTF-8 sequence */
	      isValidUTF8 = FALSE;
	      break;
	    }
	  }
	  if (nTailBytesExpected) isValidUTF8 = FALSE; /* Incomplete UTF-8 sequence at the end of the buffer */
	} else { /* Invalid UTF-8 tail byte not preceeded by a lead byte */
	  isValidUTF8 = FALSE;
	} /* End if (IS_LEAD_BYTE(c)) */
      } /* End if (isValidUTF8) */
    } /* End for each byte in pBuffer[] */
    /* Heuristics for identifying an encoding from the information gathered so far.
       Note that this choice is probabilistic. It may not be correct in all cases. */
    if (nEvenNUL + nOddNUL) { /* There are NUL bytes, so it's probably a kind of UTF-16 */
      /* TO DO: Try distinguishing UTF-16 LE and UTF-16 BE */
      cp = CP_UTF16; /* Assume it's UTF-16 LE for now, which is the default in Windows */
      DEBUG_CODE(pszMsg = "Detected UTF-16 without BOM";)
    } else if (nNonASCII && isValidUTF8) {
      cp = CP_UTF8; /* We've verified this is valid UTF-8 */
      DEBUG_CODE(pszMsg = "Detected UTF-8 without BOM";)
    } else {
      cp = 0; /* Default to the Windows encoding */
      DEBUG_CODE(pszMsg = "This is not a UTF encoding";)
    }
    DEBUG_PRINTF(("nOddNUL = %lu\n", (unsigned long)nOddNUL));
    DEBUG_PRINTF(("nEvenNUL = %lu\n", (unsigned long)nEvenNUL));
    DEBUG_PRINTF(("nNonASCII = %lu\n", (unsigned long)nNonASCII));
    DEBUG_PRINTF(("isValidUTF8 = %d\n", isValidUTF8));
  }

  DEBUG_PRINTF(("DetectUTF(): return %u; // %s\n", cp, pszMsg));
  return cp;
}
