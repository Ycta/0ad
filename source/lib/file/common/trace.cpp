/* Copyright (c) 2010 Wildfire Games
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * IO event recording
 */

#include "precompiled.h"
#include "lib/file/common/trace.h"

#include <cstdio>
#include <sstream>

#include "lib/allocators/pool.h"
#include "lib/bits.h"	// round_up
#include "lib/timer.h"	// timer_Time
#include "lib/path_util.h"


/*virtual*/ ITrace::~ITrace()
{

}


//-----------------------------------------------------------------------------

TraceEntry::TraceEntry(EAction action, const fs::wpath& pathname, size_t size)
: m_timestamp((float)timer_Time())
, m_action(action)
, m_pathname(pathname)
, m_size(size)
{
}


TraceEntry::TraceEntry(const std::wstring& text)
{
	// swscanf is far too awkward to get working cross-platform,
	// so use iostreams here instead

	wchar_t dummy;
	wchar_t action;

	std::wstringstream stream(text);
	stream >> m_timestamp;

	stream >> dummy;
	debug_assert(dummy == ':');

	stream >> action;
	debug_assert(action == 'L' || action == 'S');
	m_action = (EAction)action;

	stream >> dummy;
	debug_assert(dummy == '"');

	std::wstring pathname;
	std::getline(stream, pathname, L'"');
	m_pathname = pathname;

	stream >> m_size;

	debug_assert(stream.get() == '\n');
	debug_assert(stream.good());
	debug_assert(stream.get() == WEOF);
}


std::wstring TraceEntry::EncodeAsText() const
{
	const wchar_t action = (wchar_t)m_action;
	wchar_t buf[1000];
	swprintf_s(buf, ARRAY_SIZE(buf), L"%#010f: %c \"%ls\" %lu\n", m_timestamp, action, m_pathname.string().c_str(), (unsigned long)m_size);
	return buf;
}


//-----------------------------------------------------------------------------

class Trace_Dummy : public ITrace
{
public:
	Trace_Dummy(size_t UNUSED(maxSize))
	{

	}

	virtual void NotifyLoad(const fs::wpath& UNUSED(pathname), size_t UNUSED(size))
	{
	}

	virtual void NotifyStore(const fs::wpath& UNUSED(pathname), size_t UNUSED(size))
	{
	}

	virtual LibError Load(const fs::wpath& UNUSED(pathname))
	{
		return INFO::OK;
	}

	virtual LibError Store(const fs::wpath& UNUSED(pathname)) const
	{
		return INFO::OK;
	}

	virtual const TraceEntry* Entries() const
	{
		return 0;
	}

	virtual size_t NumEntries() const
	{
		return 0;
	}
};


//-----------------------------------------------------------------------------

class Trace : public ITrace
{
public:
	Trace(size_t maxSize)
	{
		(void)pool_create(&m_pool, maxSize, sizeof(TraceEntry));
	}

	virtual ~Trace()
	{
		for(size_t i = 0; i < NumEntries(); i++)
		{
			TraceEntry* entry = (TraceEntry*)(uintptr_t(m_pool.da.base) + i*m_pool.el_size);
			entry->~TraceEntry();
		}

		(void)pool_destroy(&m_pool);
	}

	virtual void NotifyLoad(const fs::wpath& pathname, size_t size)
	{
		new(Allocate()) TraceEntry(TraceEntry::Load, pathname, size);
	}

	virtual void NotifyStore(const fs::wpath& pathname, size_t size)
	{
		new(Allocate()) TraceEntry(TraceEntry::Store, pathname, size);
	}

	virtual LibError Load(const fs::wpath& pathname)
	{
		pool_free_all(&m_pool);

		errno = 0;
		FILE* file;
		errno_t err = _wfopen_s(&file, pathname.string().c_str(), L"rt");
		if(err != 0)
			return LibError_from_errno();

		for(;;)
		{
			wchar_t text[500];
			if(!fgetws(text, ARRAY_SIZE(text)-1, file))
				break;
			new(Allocate()) TraceEntry(text);
		}
		fclose(file);

		return INFO::OK;
	}

	virtual LibError Store(const fs::wpath& pathname) const
	{
		errno = 0;
		FILE* file;
		errno_t err = _wfopen_s(&file, pathname.string().c_str(), L"at");
		if(err != 0)
			return LibError_from_errno();
		for(size_t i = 0; i < NumEntries(); i++)
		{
			std::wstring text = Entries()[i].EncodeAsText();
			fputws(text.c_str(), file);
		}
		(void)fclose(file);
		return INFO::OK;
	}

	virtual const TraceEntry* Entries() const
	{
		return (const TraceEntry*)m_pool.da.base;
	}

	virtual size_t NumEntries() const
	{
		return m_pool.da.pos / m_pool.el_size;
	}

private:
	void* Allocate()
	{
		void* p = pool_alloc(&m_pool, 0);
		debug_assert(p);
		return p;
	}

	Pool m_pool;
};


PITrace CreateDummyTrace(size_t maxSize)
{
	return PITrace(new Trace_Dummy(maxSize));
}

PITrace CreateTrace(size_t maxSize)
{
	return PITrace(new Trace(maxSize));
}
