/*
 * Copyright (c) 2000, Red Hat, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     A copy of the GNU General Public License can be found at
 *     http://www.gnu.org/
 *
 * Written by Andrej Borsenkow <Andrej.Borsenkow@mow.siemens.ru>
 * based on work and suggestions of DJ Delorie
 *
 */

/* The purpose of this file is to ask the user where they want the
 * locally downloaded package files to be cached
 */

#if 0
static const char *cvsid =
  "\n%%% $Id$\n";
#endif

#include "localdir.h"

#include "LogSingleton.h"
#include "LogFile.h"
#include "win32.h"

#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "dialog.h"
#include "resource.h"
#include "state.h"
#include "msg.h"
#include "mount.h"
#include "io_stream.h"
#include "mkdir.h"

#include "UserSettings.h"

#include "getopt++/StringOption.h"

#include "threebar.h"
extern ThreeBarProgressPage Progress;
extern LogFile * theLog;

static StringOption LocalDirOption ("", 'l', "local-package-dir", "Local package directory", false);

static ControlAdjuster::ControlInfo LocaldirControlsInfo[] = {
  { IDC_LOCALDIR_GRP,       CP_STRETCH,   CP_TOP },
  { IDC_LOCAL_DIR,          CP_STRETCH,   CP_TOP },
  { IDC_LOCAL_DIR_BROWSE,   CP_RIGHT,     CP_TOP },
  {0, CP_LEFT, CP_TOP}
};

LocalDirSetting::LocalDirSetting ()
{
  const char *fg_ret;
  if (std::string (LocalDirOption).size ())
    local_dir = std::string (LocalDirOption);
  else if ((fg_ret = UserSettings::instance().get ("last-cache")))
    local_dir = std::string (fg_ret);
}

void
LocalDirSetting::save ()
{
  UserSettings::instance().set ("last-cache", local_dir);
  if (source == IDC_SOURCE_DOWNLOAD || !get_root_dir ().size())
    {
      const char *sep = isdirsep (local_dir[local_dir.size () - 1]) ? "" : "\\";
      theLog->clearFiles();
      theLog->setFile (LOG_BABBLE, local_dir + sep + "setup.log.full", false);
      theLog->setFile (0, local_dir + sep + "setup.log", true);
    }
  else
    {
      theLog->clearFiles();
      mkdir_p (1, cygpath ("/var/log").c_str (), 01777);
      theLog->setFile (LOG_BABBLE, cygpath ("/var/log/setup.log.full"), false);
      theLog->setFile (0, cygpath ("/var/log/setup.log"), true);
    }
}

static void
check_if_enable_next (HWND h)
{
  EnableWindow (GetDlgItem (h, IDOK), local_dir.size() != 0);
}

static void
load_dialog (HWND h)
{
  char descText[1000];
  if (source != IDC_SOURCE_CWD)
    {
      LoadString (hinstance, IDS_LOCAL_DIR_DOWNLOAD, descText, sizeof (descText));
    }
  else
    {
      LoadString (hinstance, IDS_LOCAL_DIR_INSTALL, descText, sizeof (descText));
    }
  eset (h, IDC_LOCAL_DIR_DESC, descText);
  eset (h, IDC_LOCAL_DIR, local_dir);
  check_if_enable_next (h);
}

static void
save_dialog (HWND h)
{
  local_dir = egetString (h, IDC_LOCAL_DIR);
}

// returns non-zero if refused or error, 0 if accepted and created ok.
static int
offer_to_create (HWND h, const char *dirname)
{
  if (!dirname || !*dirname)
    return -1;

  if (!unattended_mode)
    {
      char msgText[MAX_PATH + 100];
      char fmtString[100];
      DWORD ret;

      LoadString (hinstance, IDS_MAYBE_MKDIR, fmtString, sizeof fmtString);
      snprintf (msgText, sizeof msgText, fmtString, dirname);

      ret = MessageBox (h, msgText, 0, MB_ICONSTOP | MB_YESNO);
      if (ret == IDNO)
	return -1;
    }

  int rv = mkdir_p (true, dirname, 0755);

  if (rv)
    note (h, IDS_CANT_MKDIR, dirname);

  return rv;
}

static int CALLBACK
browse_cb (HWND h, UINT msg, LPARAM lp, LPARAM data)
{
  static CHAR dirname[MAX_PATH];
  switch (msg)
    {
    case BFFM_INITIALIZED:
      if (local_dir.size())
	SendMessage (h, BFFM_SETSELECTION, TRUE, (LPARAM) local_dir.c_str());
      break;
    case BFFM_SELCHANGED:
      // Make a note of each new dir we successfully select, so that
      // we know where to create the new directory if an invalid name
      // is entered in the text box.
      LPITEMIDLIST pidl = reinterpret_cast<LPITEMIDLIST>(lp);
      SHGetPathFromIDList (pidl, dirname);
      break;
    case BFFM_VALIDATEFAILED:
      // See if user wants to create a dir in the last successfully-selected.
      CHAR tempname[MAX_PATH];
      snprintf (tempname, sizeof tempname, "%s\\%s", dirname, reinterpret_cast<LPTSTR>(lp));
      if (!offer_to_create (h, tempname))
	{
	  SendMessage (h, BFFM_SETSELECTION, TRUE, reinterpret_cast<LPARAM>(tempname));
	  return -1;
	}
      // Reset to original directory instead.
      SendMessage (h, BFFM_SETSELECTION, TRUE, reinterpret_cast<LPARAM>(dirname));
      return -1;
    }
  return 0;
}

static void
browse (HWND h)
{
  BROWSEINFO bi;
  CHAR name[MAX_PATH];
  LPITEMIDLIST pidl;
  memset (&bi, 0, sizeof (bi));
  bi.hwndOwner = h;
  bi.pszDisplayName = name;
  bi.lpszTitle = (source != IDC_SOURCE_CWD) ? "Select download directory"
					    : "Select local package directory";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE
	      | ((source != IDC_SOURCE_CWD) ? (BIF_EDITBOX | BIF_VALIDATE) : 0);
  bi.lpfn = browse_cb;
  pidl = SHBrowseForFolder (&bi);
  if (pidl)
    {
      if (SHGetPathFromIDList (pidl, name))
	eset (h, IDC_LOCAL_DIR, name);
    }
}

static BOOL
dialog_cmd (HWND h, int id, HWND hwndctl, UINT code)
{
  switch (id)
    {

    case IDC_LOCAL_DIR:
      save_dialog (h);
      check_if_enable_next (h);
      break;

    case IDC_LOCAL_DIR_BROWSE:
      browse (h);
      break;
    }
  return 0;
}

LocalDirPage::LocalDirPage ()
{
  sizeProcessor.AddControlInfo (LocaldirControlsInfo);
}

bool
LocalDirPage::Create ()
{
  return PropertyPage::Create (NULL, dialog_cmd, IDD_LOCAL_DIR);
}

void
LocalDirPage::OnActivate ()
{
  load_dialog (GetHWND ());
}

long
LocalDirPage::OnNext ()
{
  HWND h = GetHWND ();

  save_dialog (h);
  LocalDirSetting::save ();
  log (LOG_PLAIN) << "Selected local directory: " << local_dir << endLog;
  
  bool trySetCurDir = true;
  while (trySetCurDir)
    {
      trySetCurDir = false;
      if (SetCurrentDirectoryA (local_dir.c_str()))
	{
	  if (source == IDC_SOURCE_CWD)
	    {
	      if (do_fromcwd (GetInstance (), GetHWND ()))
		{
		  Progress.SetActivateTask (WM_APP_START_SETUP_INI_DOWNLOAD);
		  return IDD_INSTATUS;
		}
	      return IDD_CHOOSE;
	    }
	}
      else if ((GetLastError () == ERROR_FILE_NOT_FOUND)
		|| (GetLastError () == ERROR_PATH_NOT_FOUND))
	{
	  if (source == IDC_SOURCE_CWD && unattended_mode)
	    return IDD_CHOOSE;
	  else if (source == IDC_SOURCE_CWD)
	    {
	      // Check the user really wants only to uninstall.
	      char msgText[1000];
	      LoadString (hinstance, IDS_NO_CWD, msgText, sizeof (msgText));
	      char msg[1000 + MAX_PATH];
	      snprintf (msg, sizeof (msg), msgText, local_dir.c_str ());
	      int ret = MessageBox (h, msg, 0, MB_ICONEXCLAMATION | MB_OKCANCEL);
	      return (ret == IDOK) ? IDD_CHOOSE : -1;
	    }
	  else if (offer_to_create (GetHWND (), local_dir.c_str ()))
	    return -1;
	  trySetCurDir = true;
	}
      else
	{
	  DWORD err = GetLastError ();
	  char msgText[1000];
	  LoadString (hinstance, IDS_ERR_CHDIR, msgText, sizeof (msgText));
	  char* buf;
	  char msg[1000];
	  if (FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM | 
	    FORMAT_MESSAGE_ALLOCATE_BUFFER, 0, err, LANG_NEUTRAL,
	    (LPSTR)&buf, 0, 0) != 0)
	    {
	      snprintf (msg, sizeof (msg), msgText, local_dir.c_str(), 
	        buf, err);
	      LocalFree (buf);
	    }
	    else
	      snprintf (msg, sizeof (msg), msgText, local_dir.c_str(), 
	        "(unknown error)", err);
	  log (LOG_PLAIN) << msg << endLog;
	  int ret = MessageBox (h, msg, 0, MB_ICONEXCLAMATION | 
	    MB_ABORTRETRYIGNORE);
	  
	  if ((ret == IDABORT) || (ret == IDCANCEL))
	    return -1;
	  else
	    trySetCurDir = (ret == IDRETRY);
	}
    }

  return 0;
}

long
LocalDirPage::OnBack ()
{
  save_dialog (GetHWND ());
  return 0;
}
