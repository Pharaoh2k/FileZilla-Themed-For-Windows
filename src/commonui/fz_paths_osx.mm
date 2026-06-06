#include <Foundation/NSFileManager.h>
#include <Foundation/NSURL.h>

#include <string.h>
#include <string>

std::string const& GetTempDirImpl()
{
	static std::string const dir = []() {
		NSString* d = NSTemporaryDirectory();
		return std::string(d ? d.UTF8String : "");
	}();
	return dir;
}

char const* GetDownloadDirImpl()
{
	static char const* path = 0;
	if (!path) {
		NSURL* url = [[NSFileManager defaultManager] URLForDirectory:NSDownloadsDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:NO error:nil];
		if (url) {
			path = strdup(url.path.UTF8String);
		}
		else {
			path = "";
		}
	}
	return path;
}
