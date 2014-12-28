/*######     Copyright (c) 1997-2015 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com #########################################################################################################
#                                                                                                                                                                                                                                            #
# This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation;  either version 3, or (at your option) any later version.          #
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.   #
# You should have received a copy of the GNU General Public License along with this program; If not, see <http://www.gnu.org/licenses/>                                                                                                      #
############################################################################################################################################################################################################################################*/

#pragma once

#include <el/stl/dynamic_bitset>
#include EXT_HEADER_SHARED_MUTEX

#include EXT_HEADER_FUTURE

#include <el/db/db-itf.h>

#if UCFG_LIB_DECLS
#	ifdef _DBLITE
#		define DBLITE_CLASS // AFX_CLASS_EXPORT
#	else
#		define DBLITE_CLASS
#		pragma comment(lib, "dblite")
#	endif
#else
#	define DBLITE_CLASS
#endif

#ifndef UCFG_DB_FREE_PAGES_BITSET
#	define UCFG_DB_FREE_PAGES_BITSET 1
#endif

#ifndef UCFG_DBLITE_DEBUG_VERBYTE
#	define UCFG_DBLITE_DEBUG_VERBYTE 0 //!!!D
#endif

#include "dblite-file-format.h"

namespace Ext { namespace DB { namespace KV {

using Ext::DB::CursorPos;
using Ext::DB::ITransactionable;

const int DB_MAX_VIEWS = 2048 << UCFG_PLATFORM_64,
		DB_NUM_DELETE_FROM_CACHE = 16;

const uint32_t DB_DEFAULT_PAGECACHE_SIZE = 1024 << (UCFG_PLATFORM_64 * 2);

const size_t DEFAULT_PAGE_SIZE = 4096;

const size_t DEFAULT_VIEW_SIZE = (DEFAULT_PAGE_SIZE * 64) << UCFG_PLATFORM_64;

const size_t MIN_KEYS = 4;
const size_t MAX_KEY_SIZE = 255;

struct PageHeader;
struct EntryDesc;
struct IndexedEntryDesc;
struct LiteEntry;
class BTree;
class DbTransactionBase;
class DbTable;
class KVStorage;
class PagedMap;

class MMView : public Object {
	typedef MMView class_type;
	
//!!!D	DBG_OBJECT_COUNTER(class_type);
public:
	typedef Interlocked interlocked_policy;

	KVStorage& Storage;
	MMView *Next, *Prev;		// for IntrusiveList<>

	MemoryMappedView View;
	uint32_t N;
	volatile bool Flushed;
	bool Removed;

	MMView(KVStorage& storage)
		:	Storage(storage)
		,	Flushed(false)
		,	Removed(false)
		,	Next(0)				// necessary to init
		,	Prev(0)
	{
	}

	~MMView();
};

struct IndexedBuf {
	const byte *P;
	uint16_t Size, Index;

	IndexedBuf(const byte *p = 0, uint16_t size = 0, uint16_t index = 0)
		:	P(p)
		,	Size(size)
		,	Index(index)
	{}
};

class PageObj : public Object {
public:
	typedef Interlocked interlocked_policy;

	KVStorage& Storage;
	void *Address;
	ptr<MMView> View;
	LiteEntry * volatile Entries;
	IndexedBuf OverflowCells[2];
	uint32_t N;
	byte Overflows;
	CBool Dirty, ReadOnly, Live;

	PageObj(KVStorage& storage);
	~PageObj();
};


class Page : public Pimpl<PageObj> {
	typedef Page class_type;
	typedef Pimpl<PageObj> base;
public:
	Page() {
	}

	Page(PageObj *obj) {
		m_pimpl = obj;
	}

	Page(const Page& v)
		:	base(v)
	{}

	Page(EXT_RV_REF(Page) rv)
		:	base(static_cast<EXT_RV_REF(base)>(rv))
	{}	

	Page& operator=(const Page& v) {
		base::operator=(v);
		return *this;
	}

	Page& operator=(EXT_RV_REF(Page) rv) {
		base::operator=(static_cast<EXT_RV_REF(base)>(rv));
		return *this;
	}

	uint32_t get_N() const { return m_pimpl->N; }
	DEFPROP_GET(uint32_t, N);

	bool get_Dirty() const { return m_pimpl->Dirty; }
	DEFPROP_GET(bool, Dirty);

	void *get_Address() const noexcept { return m_pimpl->Address; }
	DEFPROP_GET(void *, Address);

	__forceinline PageHeader& Header() const noexcept { return *(PageHeader*)m_pimpl->Address; }

	byte get_Overflows() const { return m_pimpl->Overflows; }
	DEFPROP_GET(byte, Overflows);

	__forceinline bool get_IsBranch() const noexcept { return Header().Flags & PageHeader::FLAG_BRANCH; }
	DEFPROP_GET(bool, IsBranch);

	LiteEntry *Entries(byte keySize) const;
	void ClearEntries() const;
	size_t SizeLeft(byte keySize) const;
	int FillPercent(byte keySize);
	bool IsUnderflowed(byte keySize);
};

}}}
EXT_DEF_HASH(Ext::DB::KV::Page)
namespace Ext { namespace DB { namespace KV {

int NumKeys(const Page& page);

struct DbHeader;

typedef unordered_set<uint32_t> CPgNos;
typedef set<uint32_t> COrderedPgNos;

class MappedFile : public Object {
public:
	Ext::MemoryMappedFile MemoryMappedFile;
};

class DBLITE_CLASS KVStorage {
	typedef KVStorage class_type;
public:
	static const size_t HeaderPageSize = 4096;
	static const size_t MAX_KEY_SIZE = 254;

	uint64_t FileLength;
	uint64_t FileIncrement;
	const size_t ViewSize;
	size_t PageSize;
	uint32_t PageCount;

	path FilePath;
	File DbFile;


//!!!R	Blob m_blobDbHeader;
	AlignedMem m_alignedDbHeader;
	DbHeader& DbHeaderRef() { return *(DbHeader*)m_alignedDbHeader.get(); }

	//-----------------------------
	recursive_mutex MtxViews; // used in ~PageObj()

	list<ptr<MappedFile>> Mappings;

	enum class ViewMode {
		Window,
		Full
	};

	const ViewMode m_viewMode;
	list<ptr<MMView>> m_fullViews;

	ViewMode m_accessViewMode;

	void* volatile ViewAddress;

	typedef vector<PageObj*> COpenedPages;
	COpenedPages OpenedPages;

//!!!R	LruCache<Page> PageCache;
	uint32_t PageCacheSize, NewPageCount;

	typedef unordered_map<uint32_t, ptr<MMView>> CViews;
	CViews Views;


	//------------------
	mutex MtxRoot;

	uint64_t LastTransactionId;
	Page MainTableRoot;			// after OpenedPages
	//---------------------


	mutex MtxWrite;

	//-----------------------------
	mutex MtxFreePages;

#if UCFG_DB_FREE_PAGES_BITSET
	typedef dynamic_bitset<byte> CFreePagesBitset;
	CFreePagesBitset FreePagesBitset;
#endif

	typedef COrderedPgNos CFreePages;
	CFreePages FreePages;
	typedef CPgNos CReleasedPages;
	CReleasedPages ReleasedPages, ReaderLockedPages;
	dynamic_bitset<> AllocatedSinceCheckpointPages;

//!!!R	int32_t ReaderRefCount;
//!!!R	unordered_set<DbTransaction*> Readers;
	//-----------------------------

	String AppName;
	Version UserVersion;

	String FrontEndName;
	Version FrontEndVersion;

	TimeSpan CheckpointPeriod;
	bool Durability;
	bool UseFlush;					// setting it to FALSE is very dangerous
	bool ProtectPages;
	CBool AsyncClose;
	CBool ReadOnly;

	enum class OpenState {
		Closed,
		Closing,
		Opened
	};

	KVStorage();
	~KVStorage();
	void Create(const path& filepath);
	void Open(const path& filepath);
	void Close(bool bLock = true);
	void Vacuum();
	bool Checkpoint() { return DoCheckpoint(); }

	virtual Page OpenPage(uint32_t pgno);
	Page Allocate(bool bLock = true);

	void SetProgressHandler(int(*pfn)(void*), void* p = 0, int n = 1) {
		m_pfnProgress = pfn;
		m_ctxProgress = p;
		m_stepProgress = n;
	}

	void SetUserVersion(const Version& ver) {
		UserVersion = ver;
		m_bModified = true;
	}

	void lock_shared();
	void unlock_shared();

	uint32_t get_Salt() { return m_salt; }
	
	void put_Salt(uint32_t v) {
		if (m_state == OpenState::Opened)
			Throw(E_FAIL);
		m_salt = v;
	}

	DEFPROP(uint32_t, Salt);
protected:
	COrderedPgNos m_nextFreePages;
	uint32_t m_salt;

	int(*m_pfnProgress)(void*);
	int m_stepProgress;
	void *m_ctxProgress;

private:
	shared_mutex ShMtx;

	volatile OpenState m_state;
	future<void> m_futClose;

//!!!R	void ReleaseReader(DbTransaction *dbTx = 0);
	
	DateTime m_dtPrevCheckpoint;
	CBool m_bModified;
	int m_bitsViewPageRatio;

//!!!R	Page LastFreePoolPage;

	uint32_t GetUInt32(uint32_t pgno, int offset);
	void SetPageSize(size_t v);
	void DoClose(bool bLock);
	void WriteHeader();
	void MapMeta();
	void AddFullMapping(uint64_t fileLength);
	void Init();
	bool DoCheckpoint(bool bLock = true);
	uint32_t TryAllocateMappedFreePage();
	void UpdateFreePagesBitset();
	void FreePage(uint32_t pgno);
	uint32_t NextFreePgno(COrderedPgNos& newFreePages);
	void MarkAllocatedPage(uint32_t pgno);

	friend class DbTransaction;
	friend class HashTable;
	friend class Filet;
};

typedef KVStorage DbStorage;

KVStorage& Stg();

struct PagePos {
	Ext::DB::KV::Page Page;
	int Pos;
	//!!!R	byte KeyOffset;

	PagePos()
//!!!R		: KeyOffset(0)	//!!!?
	{}

	PagePos(const Ext::DB::KV::Page& page, int pos)
		:	Page(page)
		,	Pos(pos)
//!!!R		,	KeyOffset(0)
	{}

	bool operator==(const PagePos& v) const { return Page==v.Page && Pos==v.Pos; }
};

const uint32_t DB_EOF_PGNO = 0;

class CursorObj : public Object {
public:
	PagedMap *Map;
	CursorObj *Next, *Prev;		// for IntrusiveList<>

	CursorObj()
		: Map(0)
		, NPage(0xFFFFFFFF)
	{}

	~CursorObj() override;
	virtual PagePos& Top() =0;
	virtual void SetMap(PagedMap *pMap) { Map = pMap; }
	virtual void Touch() =0;
	virtual bool SeekToFirst() =0;
	virtual bool SeekToLast() =0;
	virtual bool SeekToSibling(bool bToRight) =0;
	virtual bool SeekToKey(const ConstBuf& k) =0;
	virtual bool SeekToNext();
	virtual bool SeekToPrev();
	virtual void Delete();
	virtual void Put(ConstBuf k, const ConstBuf& d, bool bInsert) =0;
	virtual void PushFront(ConstBuf k, const ConstBuf& d) { Throw(E_NOTIMPL); }
	virtual void Drop() =0;
	virtual void Balance() {}
protected:
	Blob m_bigKey, m_bigData;
	ConstBuf m_key, m_data;
	uint32_t NPage;										// used in get_Key()
	CBool Initialized, Deleted, Eof, IsDbDirty;

	bool ClearKeyData() {
		m_bigKey, m_bigData = Blob();
		m_key = m_data = ConstBuf();
		return true;					// just for method chaining
	}

	void DeepFreePage(const Page& page);
	bool ReturnFromSeekKey(int pos);
	void FreeBigdataPages(uint32_t pgno);
	virtual CursorObj *Clone() =0;
	void InsertImpHeadTail(pair<size_t, bool>& ppEntry, ConstBuf k, const ConstBuf& head, uint64_t fullSize, uint32_t pgnoTail);

	const ConstBuf& get_Key();
	const ConstBuf& get_Data();

	friend class DbCursor;
};

class DbCursor : public Pimpl<CursorObj> {
	typedef DbCursor class_type;
public:
	DbCursor(DbTransactionBase& tx, DbTable& table);
	DbCursor(DbCursor& c);
	DbCursor(DbCursor& c, bool bRight, Page& pageSibling);
	~DbCursor();

	const ConstBuf& get_Key() { return m_pimpl->get_Key(); }
	DEFPROP_GET(const ConstBuf&, Key);

	const ConstBuf& get_Data() { return m_pimpl->get_Data(); }
	DEFPROP_GET(const ConstBuf&, Data);

	bool SeekToFirst() { return m_pimpl->SeekToFirst(); }
	bool SeekToLast() { return m_pimpl->SeekToLast(); }
	bool Seek(CursorPos cPos, const ConstBuf& k = ConstBuf());
	bool SeekToKey(const ConstBuf& k) { return m_pimpl->SeekToKey(k); }
	bool SeekToNext() { return m_pimpl->SeekToNext(); }
	bool SeekToPrev() { return m_pimpl->SeekToPrev(); }
	bool Get(const ConstBuf& k) { return SeekToKey(k); }
	void Delete();
	void Put(ConstBuf k, const ConstBuf& d, bool bInsert = false);
	void PushFront(ConstBuf k, const ConstBuf& d) { m_pimpl->PushFront(k, d); }
	void Drop() { m_pimpl->Drop(); }
private:
	uint64_t m_cursorImp[20];

	void AssignImpl(TableType type);
};

struct TableData;

class PagedMap : public Object {
public:
	IntrusiveList<CursorObj> Cursors;

	String Name;
	DbTransactionBase& Tx;
	byte KeySize;
	CBool Dirty;

	PagedMap(DbTransactionBase& tx)
		:	Tx(tx)
		,	KeySize(0)
		,	m_pfnCompare(&Compare)
	{}

	void SetKeySize(byte keySize) {
		m_pfnCompare = (KeySize = keySize) ? &::memcmp : &Compare;
	}

	virtual TableType Type() =0;
	virtual void Init(const TableData& td);
	virtual TableData GetTableData();
	pair<size_t, bool> GetDataEntrySize(const ConstBuf& k, uint64_t dsize) const;	// <size, isBigData>
	pair<int, bool> EntrySearch(LiteEntry *entries, PageHeader& h, const ConstBuf& k);
	EntryDesc GetEntryDesc(const PagePos& pp);
protected:
	typedef int (__cdecl *PFN_Compare)(const void *p1, const void *p2, size_t size);
	PFN_Compare m_pfnCompare;

	static int __cdecl Compare(const void *p1, const void *p2, size_t cb2);
};

ENUM_CLASS(PageAlloc) {
	Zero,
	Nothing,
	Leaf,
	Branch,
	Copy,
	Move			// Copy and Free source
} END_ENUM_CLASS(PageAlloc);


class DBLITE_CLASS DbTransactionBase : noncopyable, public ITransactionable {
public:
	KVStorage& Storage;

	int64_t TransactionId;
	Page MainTableRoot;
	const bool ReadOnly;

	typedef map<String, ptr<PagedMap>> CTables;			// map<> has faster dtor than unordered_map<>
	CTables Tables;

	DbTransactionBase(KVStorage& storage);				// ReadOnly ctor
	virtual ~DbTransactionBase();
	virtual Page OpenPage(uint32_t pgno);
protected:
	shared_lock<KVStorage> m_shlk;
	CBool m_bComplete, m_bError;

	DbTransactionBase(KVStorage& storage, bool bReadOnly);
	void InitReadOnly();
	void Commit() override;
	void Rollback() override;

	void BeginTransaction() override {
		Throw(E_FAIL);
	}

	friend class BTreeCursor;
};

typedef DbTransactionBase DbReadTransaction;

class DBLITE_CLASS DbTransaction : public DbTransactionBase {
	typedef DbTransactionBase base;
public:
	CBool Bulk;

	DbTransaction(KVStorage& storage, bool bReadOnly = false);
	~DbTransaction();

	DbTransaction& Current();

	BTree& Table(RCString name);
	Page Allocate(PageAlloc pa, Page *pCopyFrom = 0);
	vector<uint32_t> AllocatePages(int n);
	Page OpenPage(uint32_t pgno) override;

	void Commit() override;
	void Rollback() override;
private:
	unique_lock<mutex> m_lockWrite;
	Blob TmpPageSpace;
	CPgNos AllocatedPages, ReleasedPages;

	void FreePage(uint32_t pgno);
	void FreePage(const Page& page);
	void Complete();

	friend class BTree;
	friend class HashTable;
	friend class BTreeCursor;
	friend class CursorObj;
	friend class Filet;
};


class DBLITE_CLASS DbTable {
public:
	static DbTable& AFXAPI Main();

	String Name;
	TableType Type;
	HashType HtType;
	byte KeySize;						// 0=variable

	DbTable(RCString name = nullptr, byte keySize = 0, TableType type = TableType::BTree, HashType htType = HashType::MurmurHash3)
		:	Name(name)
		,	KeySize(keySize)
		,	Type(type)
		,	HtType(htType)
	{}

	void Open(DbTransaction& tx, bool bCreate = false);
	void Close() {}
	void Drop(DbTransaction& tx);
	void Put(DbTransaction& tx, const ConstBuf& k, const ConstBuf& d, bool bInsert = false);
	bool Delete(DbTransaction& tx, const ConstBuf& k);
private:
	void CheckKeyArg(const ConstBuf& k);
};

class CKVStorageKeeper {
	KVStorage *m_prev;
public:
	CKVStorageKeeper(KVStorage *cur);
	~CKVStorageKeeper();
};

#ifdef _DEBUG//!!!D
void CheckPage(Page& page);
#endif

}}} // Ext::DB::KV::


