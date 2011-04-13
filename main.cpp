﻿/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "TrayIcon.h"
#include "ConfigPath.h"
#include "main.h"
#include "stringtools.h"
#include "TaskBarBaloon.h"
#include <iostream>
#include <wx/stdpaths.h>
#include <wx/dir.h>
#include <wx/filename.h>


#include <wx/apptrait.h>
/*#if wxUSE_STACKWALKER && defined( __WXDEBUG__ )
// silly workaround for the link error with debug configuration:
// \src\common\appbase.cpp
wxString wxAppTraitsBase::GetAssertStackTrace()
{
   return wxT("");
}
#endif*/

#ifdef _WIN32
#include <windows.h>
#endif

TrayIcon *tray;
MyTimer *timer;
int icon_type=0;
wxString last_status;
unsigned int incr_update_intervall=2*60*60+10*60;
bool incr_update_done=false;
int working_status=0;

#ifndef DD_RELEASE
IMPLEMENT_APP_NO_MAIN(MyApp)
#endif

class TheFrame : public wxFrame {
public:
    TheFrame(void) : wxFrame(NULL, -1, wxT("UrBackupGUI")) { }
};


bool MyApp::OnInit()
{
#ifdef _WIN32
#ifndef _DEBUG
	wchar_t buf[MAX_PATH];
	GetModuleFileNameW(NULL, buf, MAX_PATH);
	SetCurrentDirectoryW(ExtractFilePath(buf).c_str() );
#endif
#endif
	wxLanguage lang=wxLANGUAGE_ENGLISH;
	wxLanguage sysdef=(wxLanguage)wxLocale::GetSystemLanguage();
	switch(sysdef)
	{
	case wxLANGUAGE_GERMAN:
	case wxLANGUAGE_GERMAN_AUSTRIAN:
	case wxLANGUAGE_GERMAN_BELGIUM:
	case wxLANGUAGE_GERMAN_LIECHTENSTEIN:
	case wxLANGUAGE_GERMAN_LUXEMBOURG:
	case wxLANGUAGE_GERMAN_SWISS:
		lang=wxLANGUAGE_GERMAN;
		break;
	}

	m_locale.Init(lang, wxLOCALE_DONT_LOAD_DEFAULT);
	m_locale.AddCatalog("trans");



	this->SetTopWindow(new TheFrame);
	wxImage::AddHandler(new wxPNGHandler);

	tray=new TrayIcon;
	bool b=tray->SetIcon(wxIcon(wxT("backup-ok.ico"), wxBITMAP_TYPE_ICO), wxT("UrBackup Client"));
	if(!b)
	{
		std::cout << "Setting icon failed." << std::endl;
	}

	timer=new MyTimer;

	timer->Notify();
	timer->Start(60000);

    return true;
}

int MyApp::OnExit()
{
	exit(0);
	return 0;
}

void MyTimer::Notify()
{
	static bool working=false;
	if(working==true)
	{
		return;
	}
	if(Connector::isBusy())
	{
		return;
	}
	working=true;

	wxStandardPaths sp;
	static wxString cfgDir=sp.GetUserDataDir();
	static long starttime=wxGetLocalTime();
	static long startuptime_passed=0;
	static long lastbackuptime=-5*60*1000;
	static long lastversioncheck=starttime;

	if(!wxDir::Exists(cfgDir) )
	{
		wxFileName::Mkdir(cfgDir);
	}

	if(startuptime_passed==0)
	{
		startuptime_passed=atoi(getFile((cfgDir+wxT("/passedtime.cfg") ).ToUTF8().data() ).c_str() );
		startuptime_passed+=atoi(getFile((cfgDir+wxT("/passedtime_new.cfg") ).ToUTF8().data() ).c_str() );
		writestring(nconvert(startuptime_passed), (cfgDir+wxT("/passedtime.cfg") ).ToUTF8().data() );
		lastbackuptime=atoi(getFile((cfgDir+wxT("/lastbackuptime.cfg") ).ToUTF8().data() ).c_str() );
		if(lastbackuptime==0)
			lastbackuptime=-5*60*1000;
		std::string update_intv=getFile((cfgDir+wxT("/incr_updateintervall.cfg") ).ToUTF8().data() );
		if(!update_intv.empty())
			incr_update_intervall=atoi(update_intv.c_str());
	}

	long ct=wxGetLocalTime();

	if(ct-lastversioncheck>300)
	{
		std::string n_version=getFile("version.txt");
		std::string c_version=getFile("curr_version.txt");
		if(n_version.empty())n_version="0";
		if(c_version.empty())c_version="0";

		if( atoi(n_version.c_str())>atoi(c_version.c_str()))
		{
			TaskBarBaloon *tbb=new TaskBarBaloon(_("UrBackup: Update verfügbar"), _("Eine neue Version von UrBackup ist verfügbar. Klicken Sie hier um diese zu installieren"));
			tbb->showBaloon(80000);
		}
		ct=wxGetLocalTime();
		lastversioncheck=ct;
	}

	long passed=( ct-starttime );

	writestring(nconvert(passed), (cfgDir+wxT("/passedtime_new.cfg") ).ToUTF8().data() );

	wxString status_text;
	SStatus status=Connector::getStatus();

	if(Connector::hasError() )
	{
		if(icon_type!=4)
		{
			last_status=_("Keine Verbindung zum Backupserver möglich");
			if(tray!=NULL)
				tray->SetIcon(wxIcon(wxT("backup-bad.ico"), wxBITMAP_TYPE_ICO), last_status);
			icon_type=4;
		}
		working=false;
		return;
	}

	int last_icon_type=icon_type;
	
	if(status.status==wxT("DONE") )
	{
		writestring(nconvert(startuptime_passed+passed), (cfgDir+wxT("/lastbackuptime.cfg") ).ToUTF8().data() );
		lastbackuptime=startuptime_passed+passed;
		icon_type=0;
		working_status=0;
	}
	else if(status.status==wxT("INCR") )
	{
		status_text+=_("Inkrementelles Backup läuft. ");
		if(!status.pcdone.empty())
		{
			status_text+=status.pcdone;
			status_text+=wxT("% fertig. ");
		}
		icon_type=1;
		working_status=1;

	}
	else if(status.status==wxT("FULL") )
	{
		status_text+=_("Volles Backup läuft. ");
		if(!status.pcdone.empty())
		{
			status_text+=status.pcdone;
			status_text+=wxT("% fertig. ");
		}
		icon_type=1;
		working_status=2;
	}
	else if(status.status==wxT("INCRI") )
	{
		status_text+=_("Inkrementelles Image-Backup läuft. ");
		if(!status.pcdone.empty())
		{
			status_text+=status.pcdone;
			status_text+=wxT("% fertig. ");
		}
		icon_type=1;
		working_status=3;
	}
	else if(status.status==wxT("FULLI") )
	{
		status_text+=_("Volles Image-Backup läuft. ");
		if(!status.pcdone.empty())
		{
			status_text+=status.pcdone;
			status_text+=wxT("% fertig. ");
		}
		icon_type=1;
		working_status=4;
	}
	else if(startuptime_passed+passed-(long)incr_update_intervall>lastbackuptime)
	{	
		status_text+=_("Kein aktuelles Backup. ");
		icon_type=2;
		working_status=0;
	}
	else
	{
		icon_type=0;
		working_status=0;
	}

	if(!status.lastbackupdate.Trim().empty() )
		status_text+=_("Letztes Backup am ")+status.lastbackupdate;

	if( icon_type<3 && incr_update_done==false)
	{
		unsigned int n_incr_update_intervall=Connector::getIncrUpdateIntervall();
		if(!Connector::hasError() && n_incr_update_intervall!=0)
		{
			incr_update_done=true;
			incr_update_intervall=n_incr_update_intervall;
			writestring(nconvert(incr_update_intervall), (cfgDir+wxT("/incr_updateintervall.cfg") ).ToUTF8().data() );
		}
	}

	if(status.pause && icon_type==1)
	{
		icon_type=3;
	}

	if(icon_type!=last_icon_type || last_status!=status_text)
	{
		last_status=status_text;
		switch(icon_type)
		{
		case 0:
			if(tray!=NULL)
				tray->SetIcon(wxIcon(wxT("backup-ok.ico"), wxBITMAP_TYPE_ICO), status_text);
			if(timer!=NULL)
				timer->Start(60000);
			break;
		case 1:
			if(tray!=NULL)
				tray->SetIcon(wxIcon(wxT("backup-progress.ico"), wxBITMAP_TYPE_ICO), status_text);
			if(timer!=NULL)
				timer->Start(10000);
			break;
		case 2:
			if(tray!=NULL)
				tray->SetIcon(wxIcon(wxT("backup-bad.ico"), wxBITMAP_TYPE_ICO), status_text);
			if(timer!=NULL)
				timer->Start(60000);
			break;
		case 3:
			if(tray!=NULL)
				tray->SetIcon(wxIcon(wxT("backup-progress-pause.ico"), wxBITMAP_TYPE_ICO), status_text);

			if(timer!=NULL)
				timer->Start(60000);
		}
	}

	working=false;
}

#ifndef DD_RELEASE
int main(int argc, char *argv[])
{
	wxEntry(argc, argv);
}
#else
IMPLEMENT_APP(MyApp)
#endif