#include <stdlib.h>
#include "win32.h"
#include "shlobj.h"
#include "mklink2.h"

#if 0
static const char *cvsid =
  "\n%%% $Id$\n";
#endif

/* This part of the code must be in C because the C++ interface to COM
doesn't work. */

void
make_link_2 (char *exepath, char *args, char *icon, char *lname)
{
  IShellLink *sl;
  IPersistFile *pf;
  WCHAR widepath[_MAX_PATH];

  CoCreateInstance (&CLSID_ShellLink, NULL,
		    CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID *) & sl);
  sl->lpVtbl->QueryInterface (sl, &IID_IPersistFile, (void **) &pf);

  sl->lpVtbl->SetPath (sl, exepath);
  sl->lpVtbl->SetArguments (sl, args);
  sl->lpVtbl->SetIconLocation (sl, icon, 0);

  MultiByteToWideChar (CP_ACP, 0, lname, -1, widepath, _MAX_PATH);
  pf->lpVtbl->Save (pf, widepath, TRUE);

  pf->lpVtbl->Release (pf);
  sl->lpVtbl->Release (sl);
}

#define SYMLINK_COOKIE "!<symlink>"

/* Predicate: file is not currently in existence.
 * A file race can occur otherwise.
 */
int
mkcygsymlink (const char *from, const char *to)
{
  char buf[512];
  unsigned long w;
  HANDLE h = CreateFileA (from, GENERIC_WRITE, 0, 0, CREATE_NEW,
		   FILE_ATTRIBUTE_NORMAL, 0);
  if (h == INVALID_HANDLE_VALUE)
    return 1;
  strcpy (buf, SYMLINK_COOKIE);
  strcat (buf, to);
  if (WriteFile (h, buf, strlen (buf) + 1, &w, NULL))
    {
      CloseHandle (h);
      SetFileAttributesA (from, FILE_ATTRIBUTE_SYSTEM);
      return 0;
    }
  CloseHandle (h);
  DeleteFileA (from);
  return 1;
}
