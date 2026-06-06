#ifndef FILEZILLA_INTERFACE_DRAGDROPMANAGER_HEADER
#define FILEZILLA_INTERFACE_DRAGDROPMANAGER_HEADER

// wxWidgets doesn't provide any means to check on the type of objects
// while an object hasn't been dropped yet and is still being moved around
// At least on Windows, that appears to be a limitation of the native drag
// and drop system.

// As such, keep track on the objects.

#include "../commonui/site.h"
class wxDataObject;
class wxWindow;

class CDragDropManager final
{
public:
	static CDragDropManager* Get() { return m_pDragDropManager; }

	static CDragDropManager* Init();
	void Release();

	const wxWindow* pDragSource{};
	const wxWindow* pDropTarget{};

	CLocalPath localParent;

	Site site;
	CServerPath remoteParent;

	wxDataObject * dragDataObject{};

protected:
	CDragDropManager();
	virtual ~CDragDropManager() = default;

	static CDragDropManager* m_pDragDropManager;
};

#endif
