#include "filezilla.h"
#include "dragdropmanager.h"

CDragDropManager* CDragDropManager::m_pDragDropManager = 0;

CDragDropManager::CDragDropManager()
{
}

CDragDropManager* CDragDropManager::Init()
{
	if (!m_pDragDropManager) {
		m_pDragDropManager = new CDragDropManager;
	}
	return m_pDragDropManager;
}

void CDragDropManager::Release()
{
	delete m_pDragDropManager;
	m_pDragDropManager = 0;
}
