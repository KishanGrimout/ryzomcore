// DynWorld
// Copyright (C) 2016 Kishan Grimout <kishan.grimout@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef DISKFILE_DATABASE_HEADER
#define DISKFILE_DATABASE_HEADER

class CDiskFileDataBase
{
	static const char* ms_defaultPath;

	template <typename K>
	std::string getFileName(K key)
	{
		std::string filename = NLMISC::toString(key) + ".sector";
		std::string filepath = NLMISC::CPath::lookup(filename, false, false);
		if (filepath.empty())
		{
			filepath = ms_defaultPath;
			filepath += filename;
		}
		return filepath;
	}

public:
	explicit CDiskFileDataBase(const char* dbPath);

	void init(const char* dbPath);
	void close();
	bool isValid();
	void flush();
	void handleError();

	template <typename K>
	bool readData(K key, void* buffer, sint64& inOutBufLen, sint64 readOffset = 0)
	{
		std::string filename = getFileName(key);

		NLMISC::CIFile file;
		if (file.open(filename))
		{
			inOutBufLen = std::min(inOutBufLen, file.getFileSize() - readOffset);
			file.seek(readOffset, NLMISC::IStream::begin);
			file.serialBuffer((uint8*)buffer, inOutBufLen);
			file.close();

			return true;
		}

		return false;
	}

	template <typename K>
	bool insertData(K key, const void* data, sint64 dataLen, bool append)
	{
		std::string filename = getFileName(key);

		NLMISC::COFile file;
		if (file.open(filename, append))
		{
			file.serialBuffer((uint8*)data, dataLen);
			file.close();

			return true;
		}

		return false;
	}

	template <typename K>
	bool storeData(K key, const void* data, sint64 dataLen)
	{
		return insertData(key, data, dataLen, false);
	}

	template <typename K, typename V>
	bool storeData(K key, const V& data)
	{
		return storeData(key, &data, sizeof(V));
	}

	template <typename K>
	bool appendData(K key, const void* data, sint64 dataLen)
	{
		return insertData(key, data, dataLen, true);
	}

	template <typename K, typename V>
	bool appendData(K key, const V& data)
	{
		return appendData(key, &data, sizeof(V));
	}
};

#endif
