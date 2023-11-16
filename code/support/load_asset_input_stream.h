//
// Copyright (C) YuqiaoZhang(HanetakaChou)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#ifndef _LOAD_ASSET_INPUT_STREAM_H_
#define _LOAD_ASSET_INPUT_STREAM_H_ 1

#include <stddef.h>
#include <stdint.h>

enum
{
	LOAD_ASSET_INPUT_STREAM_SEEK_SET = 0,
	LOAD_ASSET_INPUT_STREAM_SEEK_CUR = 1,
	LOAD_ASSET_INPUT_STREAM_SEEK_END = 2
};

class load_asset_input_stream
{
public:
	virtual int stat_size(int64_t *size) = 0;
	virtual intptr_t read(void *data, size_t size) = 0;
	virtual int64_t seek(int64_t offset, int whence) = 0;
};

class load_asset_memory_input_stream : public load_asset_input_stream
{
	void const *m_memory_range_base;
	int64_t m_memory_range_size;
	int64_t m_memory_range_offset;

public:
	load_asset_memory_input_stream();
	void init(void const *memory_range_base, size_t memory_range_size);
	void uninit();
	~load_asset_memory_input_stream();
	int stat_size(int64_t *size) override;
	intptr_t read(void *data, size_t size) override;
	int64_t seek(int64_t offset, int whence) override;
};

#endif