#ifndef FILEZILLA_INTERFACE_RECENTSERVERLIST_HEADER
#define FILEZILLA_INTERFACE_RECENTSERVERLIST_HEADER

#include "xmlfunctions.h"

#include <deque>

class login_manager;
class COptionsBase;
class CRecentServerList
{
public:
	static void SetMostRecentServer(Site const& site, login_manager & lim, COptionsBase & options);
	static void SetMostRecentServers(std::deque<Site> const& sites, login_manager & lim, COptionsBase & options, bool lockMutex = true);
	static const std::deque<Site> GetMostRecentServers(bool lockMutex = true);
	static void Clear();
};

#endif
