#ifndef FILEZILLA_INTERFACE_QUEUE_HEADER
#define FILEZILLA_INTERFACE_QUEUE_HEADER

#include "aui_notebook_ex.h"
#include "listctrlex.h"
#include "edithandler.h"

#include <libfilezilla/optional.hpp>

enum class QueuePriority : unsigned char {
	lowest,
	low,
	normal,
	high,
	highest,

	count
};

enum class QueueItemType {
	Server,
	File,
	Folder,
	Status
};

enum class TransferDirection
{
	both,
	download,
	upload
};

namespace pugi { class xml_node; }
class CQueueItem
{
public:
	virtual ~CQueueItem();

	virtual void SetPriority(QueuePriority priority);

	virtual void AddChild(CQueueItem* pItem);
	virtual unsigned int GetChildrenCount(bool recursive) const;
	virtual CQueueItem* GetChild(unsigned int item, bool recursive = true);
	CQueueItem* GetParent() { return m_parent; }
	const CQueueItem* GetParent() const { return m_parent; }
	void SetParent(CQueueItem* parent) { m_parent = parent; }

	virtual bool RemoveChild(CQueueItem* pItem, bool destroy = true, bool forward = true); // Removes a child item with is somewhere in the tree of children.
	virtual bool TryRemoveAll() = 0; // Removes a inactive childrens, queues active children for removal. Returns true if item itself can be removed
	CQueueItem* GetTopLevelItem();
	const CQueueItem* GetTopLevelItem() const;
	int GetItemIndex() const; // Return the visible item index relative to the topmost parent item.
	virtual void SaveItem(pugi::xml_node&) const {}

	virtual QueueItemType GetType() const = 0;

	fz::datetime GetTime() const { return m_time; }
	void UpdateTime() { m_time = fz::datetime::now(); }

	int GetRemovedAtFront() const { return m_removed_at_front; }

protected:
	CQueueItem(CQueueItem* parent = 0);

	CQueueItem* m_parent;

	friend class CServerItem;

	fz::datetime m_time;

private:
	std::vector<CQueueItem*> m_children;

	// Number of items removed at front of list
	// Increased instead of calling slow m_children.erase(0),
	// resetted on insert.
	int m_removed_at_front{};
};

class CFileItem;
class CServerItem final : public CQueueItem
{
public:
	CServerItem(Site const& site);
	virtual ~CServerItem();
	virtual QueueItemType GetType() const override { return QueueItemType::Server; }

	Site const& GetSite() const { return site_; }
	ProtectedCredentials& GetCredentials() {return site_.credentials; }
	wxString GetName() const;

	virtual void AddChild(CQueueItem* pItem) override;
	virtual unsigned int GetChildrenCount(bool recursive) const override;
	virtual CQueueItem* GetChild(unsigned int item, bool recursive = true) override;

	CFileItem* GetIdleChild(bool immadiateOnly, TransferDirection direction);

	virtual bool RemoveChild(CQueueItem* pItem, bool destroy = true, bool forward = true) override; // Removes a child item with is somewhere in the tree of children
	virtual bool TryRemoveAll() override;

	int64_t GetTotalSize(int& filesWithUnknownSize, int& queuedFiles) const;

	void QueueImmediateFiles();
	void QueueImmediateFile(CFileItem* pItem);

	void Save(pugi::xml_node& element, COptionsBase & options, login_manager & lim) const;

	void SetDefaultFileExistsAction(CFileExistsNotification::OverwriteAction action, const TransferDirection direction);

	void DetachChildren();

	virtual void SetPriority(QueuePriority priority) override;

	void SetChildPriority(CFileItem* pItem, QueuePriority oldPriority, QueuePriority newPriority);

	int m_activeCount;

	const std::vector<CQueueItem*>& GetChildren() const { return m_children; }

	void Sort(int col, bool reverse);

protected:
	void AddFileItemToList(CFileItem* pItem);
	void RemoveFileItemFromList(CFileItem* pItem, bool forward);

	Site site_;

	// array of item lists, sorted by priority. Used by scheduler to find
	// next file to transfer
	// First index specifies whether the item is queued (0) or immediate (1)
	std::deque<CFileItem*> m_fileList[2][static_cast<int>(QueuePriority::count)];

	friend class CQueueItem;

	int m_visibleOffspring{}; // Visible offspring over all sublevels
	int m_maxCachedIndex{-1};

	struct t_cacheItem
	{
		int index;
		int child;
	};
	std::vector<t_cacheItem> m_lookupCache;
};

struct t_EngineData;

namespace queue_flags
{
	auto constexpr queued = static_cast<transfer_flags>(0x01);
	auto constexpr active = static_cast<transfer_flags>(0x02);
	auto constexpr made_progess = static_cast<transfer_flags>(0x04);
	auto constexpr remove = static_cast<transfer_flags>(0x08);
	auto constexpr mask = static_cast<transfer_flags>(0x0f);
}

class CFileItem : public CQueueItem
{
public:
	CFileItem(CServerItem* parent, transfer_flags const& flags,
		std::wstring const& sourceFile, std::wstring const& targetFile,
		CLocalPath const& localPath, CServerPath const& remotePath, int64_t size,
		std::wstring const& extraFlags, std::string const& persistentState = {});

	virtual ~CFileItem();

	virtual void SetPriority(QueuePriority priority) override;
	void SetPriorityRaw(QueuePriority priority);
	QueuePriority GetPriority() const;

	struct extra_data {
		std::wstring targetFile_;
		std::wstring extraFlags_;
		std::string persistentState_;
	};

	std::wstring const& GetLocalFile() const { return !Download() ? GetSourceFile() : (extra_data_ && !extra_data_->targetFile_.empty() ? extra_data_->targetFile_ : m_sourceFile); }
	std::wstring const& GetRemoteFile() const { return Download() ? GetSourceFile() : (extra_data_ && !extra_data_->targetFile_.empty() ? extra_data_->targetFile_ : m_sourceFile); }
	std::wstring const& GetSourceFile() const { return m_sourceFile; }
	fz::sparse_optional<extra_data> const& GetExtraData() const { return extra_data_; }
	CLocalPath const& GetLocalPath() const { return m_localPath; }
	CServerPath const& GetRemotePath() const { return m_remotePath; }
	int64_t GetSize() const { return m_size; }
	void SetSize(int64_t size) { m_size = size; }
	inline bool Download() const { return flags_ & transfer_flags::download; }

	inline transfer_flags flags() const { return flags_; }

	inline bool queued() const { return flags_ & queue_flags::queued; }
	inline void set_queued(bool q)
	{
		if (q) {
			flags_ |= queue_flags::queued;
		}
		else {
			flags_ -= queue_flags::queued;
		}
	}

	inline bool pending_remove() const { return flags_ & queue_flags::remove; }
	inline void set_pending_remove(bool remove)
	{
		if (remove) {
			flags_ |= queue_flags::remove;
		}
		else {
			flags_ -= queue_flags::remove;
		}
	}

	virtual QueueItemType GetType() const override { return QueueItemType::File; }

	bool IsActive() const { return flags_ & queue_flags::active; }
	virtual void SetActive(bool active);

	virtual void SaveItem(pugi::xml_node& element) const override;

	// Removes inactive children, queues active children for removal.
	// Returns true if item can be removed itself
	virtual bool TryRemoveAll() override final;

	void SetTargetFile(std::wstring const& file);

	void set_persistent_state(std::string && state);
	void clear_persistent_state();

	enum class Status : unsigned char {
		none,
		incorrect_password,
		timeout,
		disconnecting,
		disconnected,
		connecting,
		connection_failed,
		interrupted,
		wait_browsing,
		wait_password,
		local_file_unwriteable,
		could_not_start,
		transferring,
		creating_dir
	};

	wxString const& GetStatusMessage() const;
	void SetStatusMessage(Status status);

	CEditHandler::fileType m_edit{CEditHandler::none};
	CFileExistsNotification::OverwriteAction m_defaultFileExistsAction{CFileExistsNotification::unknown};
	CFileExistsNotification::OverwriteAction m_onetime_action{CFileExistsNotification::unknown};
	QueuePriority m_priority{QueuePriority::normal};

protected:
	transfer_flags flags_;
	Status m_status{};

public:
	unsigned char m_errorCount{};
	t_EngineData* m_pEngineData{};

	inline bool made_progress() const { return flags_ & queue_flags::made_progess; }
	inline void set_made_progress(bool made_progress)
	{
		if (made_progress) {
			flags_ |= queue_flags::made_progess;
		}
		else {
			flags_ -= queue_flags::made_progess;
		}
	}

protected:
	std::wstring const m_sourceFile;
	fz::sparse_optional<extra_data> extra_data_;
	CLocalPath const m_localPath;
	CServerPath const m_remotePath;
	int64_t m_size{};
};

class CFolderItem final : public CFileItem
{
public:
	CFolderItem(CServerItem* parent, bool queued, CLocalPath const& localPath);
	CFolderItem(CServerItem* parent, bool queued, CServerPath const& remotePath, std::wstring const& remoteFile);

	virtual QueueItemType GetType() const override { return QueueItemType::Folder; }

	virtual void SaveItem(pugi::xml_node& element) const override;

	virtual void SetActive(bool active) override;
};

class CStatusItem final : public CQueueItem
{
public:
	CStatusItem() = default;
	virtual ~CStatusItem() = default;

	virtual QueueItemType GetType() const override { return QueueItemType::Status; }

	virtual bool TryRemoveAll() override { return true; }
};

class CQueue;
class TimeFormatter;
class CQueueViewBase : public wxListCtrlEx
{
public:

	enum ColumnId
	{
		colLocalName,
		colDirection,
		colRemoteName,
		colSize,
		colPriority,
		colTime,
		colTransferStatus,
		colErrorReason
	};

	CQueueViewBase(CQueue* parent, COptionsBase & options, TimeFormatter & time_formatter, login_manager & lim, int index, wxString const& title);
	virtual ~CQueueViewBase();

	// Gets item for given server or creates new if it doesn't exist
	CServerItem* CreateServerItem(Site const& site);

	virtual void InsertItem(CServerItem* pServerItem, CQueueItem* pItem);
	virtual bool RemoveItem(CQueueItem* pItem, bool destroy, bool updateItemCount = true, bool updateSelections = true, bool forward = true);

	// Has to be called after adding or removing items. Also updates
	// item count and selections.
	virtual void CommitChanges();

	wxString GetTitle() const { return m_title; }

	int GetFileCount() const { return m_fileCount; }

	void WriteToFile(pugi::xml_node element) const;

protected:

	void CreateColumns(std::vector<ColumnId> const& extraColumns = std::vector<ColumnId>());
	void AddQueueColumn(ColumnId id);

	// Gets item for given server
	CServerItem* GetServerItem(Site const& site);

	// Gets item with given index
	CQueueItem* GetQueueItem(unsigned int item) const;

	// Get index for given queue item
	int GetItemIndex(const CQueueItem* item);

	virtual wxString OnGetItemText(long item, long column) const;
	virtual wxString OnGetItemText(CQueueItem* pItem, ColumnId column) const;
	virtual int OnGetItemImage(long item) const;

	void RefreshItem(const CQueueItem* pItem);

	void DisplayNumberQueuedFiles();

	COptionsBase& options_;
	TimeFormatter& time_formatter_;
	login_manager& login_manager_;

	// Position at which insertions start and number of insertions
	int m_insertionStart{-1};
	unsigned int m_insertionCount{};

	int m_fileCount{};
	bool m_fileCountChanged{};

	// Selection management.
	void UpdateSelections_ItemAdded(int added);
	void UpdateSelections_ItemRangeAdded(int added, int count);
	void UpdateSelections_ItemRemoved(int removed);
	void UpdateSelections_ItemRangeRemoved(int removed, int count);

	int m_itemCount{};
	bool m_allowBackgroundErase{true};

	std::vector<CServerItem*> m_serverList;

	CQueue* m_pQueue;

	const int m_pageIndex;

	const wxString m_title;

	wxTimer m_filecount_delay_timer;

	std::vector<ColumnId> m_columns;

	DECLARE_EVENT_TABLE()
	void OnEraseBackground(wxEraseEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnEndColumnDrag(wxListEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnExport(wxCommandEvent&);
};

class CQueueView;
class CQueueViewFailed;
class CQueueViewSuccessful;

class CAsyncRequestQueue;
class CMainFrame;
class cert_store;
class login_manager;
class CQueue final : public wxAuiNotebookEx
{
public:
	CQueue(wxWindow* parent, CMainFrame* pMainFrame, CAsyncRequestQueue* pAsyncRequestQueue, login_manager & lim, cert_store & certStore);
	virtual ~CQueue() {}

	inline CQueueView* GetQueueView() { return m_pQueueView; }
	inline CQueueViewFailed* GetQueueView_Failed() { return m_pQueueView_Failed; }
	inline CQueueViewSuccessful* GetQueueView_Successful() { return m_pQueueView_Successful; }

	virtual void SetFocus();
protected:

	CQueueView* m_pQueueView{};
	CQueueViewFailed* m_pQueueView_Failed{};
	CQueueViewSuccessful* m_pQueueView_Successful{};
};

#include "QueueView.h"

#endif
