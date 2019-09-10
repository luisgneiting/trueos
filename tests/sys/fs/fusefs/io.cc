/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 The FreeBSD Foundation
 *
 * This software was developed by BFF Storage Systems, LLC under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

extern "C" {
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/sysctl.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
}

#include "mockfs.hh"
#include "utils.hh"

/* 
 * For testing I/O like fsx does, but deterministically and without a real
 * underlying file system
 *
 * TODO: after fusefs gains the options to select cache mode for each mount
 * point, run each of these tests for all cache modes.
 */

using namespace testing;

enum cache_mode {
	Uncached,
	Writethrough,
	Writeback,
	WritebackAsync
};

const char *cache_mode_to_s(enum cache_mode cm) {
	switch (cm) {
	case Uncached:
		return "Uncached";
	case Writethrough:
		return "Writethrough";
	case Writeback:
		return "Writeback";
	case WritebackAsync:
		return "WritebackAsync";
	default:
		return "Unknown";
	}
}

const char FULLPATH[] = "mountpoint/some_file.txt";
const char RELPATH[] = "some_file.txt";
const uint64_t ino = 42;

static void compare(const void *tbuf, const void *controlbuf, off_t baseofs,
	ssize_t size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (((const char*)tbuf)[i] != ((const char*)controlbuf)[i]) {
			off_t ofs = baseofs + i;
			FAIL() << "miscompare at offset "
			       << std::hex
			       << std::showbase
			       << ofs
			       << ".  expected = "
			       << std::setw(2)
			       << (unsigned)((const uint8_t*)controlbuf)[i]
			       << " got = "
			       << (unsigned)((const uint8_t*)tbuf)[i];
		}
	}
}

typedef tuple<bool, uint32_t, cache_mode> IoParam;

class Io: public FuseTest, public WithParamInterface<IoParam> {
public:
int m_backing_fd, m_control_fd, m_test_fd;
off_t m_filesize;
bool m_direct_io;

Io(): m_backing_fd(-1), m_control_fd(-1), m_test_fd(-1), m_direct_io(false) {};

void SetUp()
{
	m_filesize = 0;
	m_backing_fd = open("backing_file", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (m_backing_fd < 0)
		FAIL() << strerror(errno);
	m_control_fd = open("control", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (m_control_fd < 0)
		FAIL() << strerror(errno);
	srandom(22'9'1982);	// Seed with my birthday

	if (get<0>(GetParam()))
		m_init_flags |= FUSE_ASYNC_READ;
	m_maxwrite = get<1>(GetParam());
	switch (get<2>(GetParam())) {
		case Uncached:
			m_direct_io = true;
			break;
		case WritebackAsync:
			m_async = true;
			/* FALLTHROUGH */
		case Writeback:
			m_init_flags |= FUSE_WRITEBACK_CACHE;
			/* FALLTHROUGH */
		case Writethrough:
			break;
		default:
			FAIL() << "Unknown cache mode";
	}

	FuseTest::SetUp();
	if (IsSkipped())
		return;

	if (verbosity > 0) {
		printf("Test Parameters: init_flags=%#x maxwrite=%#x "
		    "%sasync cache=%s\n",
		    m_init_flags, m_maxwrite, m_async? "" : "no",
		    cache_mode_to_s(get<2>(GetParam())));
	}

	expect_lookup(RELPATH, ino, S_IFREG | 0644, 0, 1);
	expect_open(ino, m_direct_io ? FOPEN_DIRECT_IO : 0, 1);
	EXPECT_CALL(*m_mock, process(
		ResultOf([=](auto in) {
			return (in.header.opcode == FUSE_WRITE &&
				in.header.nodeid == ino);
		}, Eq(true)),
		_)
	).WillRepeatedly(Invoke(ReturnImmediate([=](auto in, auto& out) {
		const char *buf = (const char*)in.body.bytes +
			sizeof(struct fuse_write_in);
		ssize_t isize = in.body.write.size;
		off_t iofs = in.body.write.offset;

		ASSERT_EQ(isize, pwrite(m_backing_fd, buf, isize, iofs))
			<< strerror(errno);
		SET_OUT_HEADER_LEN(out, write);
		out.body.write.size = isize;
	})));
	EXPECT_CALL(*m_mock, process(
		ResultOf([=](auto in) {
			return (in.header.opcode == FUSE_READ &&
				in.header.nodeid == ino);
		}, Eq(true)),
		_)
	).WillRepeatedly(Invoke(ReturnImmediate([=](auto in, auto& out) {
		ssize_t isize = in.body.write.size;
		off_t iofs = in.body.write.offset;
		void *buf = out.body.bytes;
		ssize_t osize;

		osize = pread(m_backing_fd, buf, isize, iofs);
		ASSERT_LE(0, osize) << strerror(errno);
		out.header.len = sizeof(struct fuse_out_header) + osize;
	})));
	EXPECT_CALL(*m_mock, process(
		ResultOf([=](auto in) {
			return (in.header.opcode == FUSE_SETATTR &&
				in.header.nodeid == ino &&
				(in.body.setattr.valid & FATTR_SIZE));
				
		}, Eq(true)),
		_)
	).WillRepeatedly(Invoke(ReturnImmediate([=](auto in, auto& out) {
		ASSERT_EQ(0, ftruncate(m_backing_fd, in.body.setattr.size))
			<< strerror(errno);
		SET_OUT_HEADER_LEN(out, attr);
		out.body.attr.attr.ino = ino;
		out.body.attr.attr.mode = S_IFREG | 0755;
		out.body.attr.attr.size = in.body.setattr.size;
		out.body.attr.attr_valid = UINT64_MAX;
	})));
	/* Any test that close()s will send FUSE_FLUSH and FUSE_RELEASE */
	EXPECT_CALL(*m_mock, process(
		ResultOf([=](auto in) {
			return (in.header.opcode == FUSE_FLUSH &&
				in.header.nodeid == ino);
		}, Eq(true)),
		_)
	).WillRepeatedly(Invoke(ReturnErrno(0)));
	EXPECT_CALL(*m_mock, process(
		ResultOf([=](auto in) {
			return (in.header.opcode == FUSE_RELEASE &&
				in.header.nodeid == ino);
		}, Eq(true)),
		_)
	).WillRepeatedly(Invoke(ReturnErrno(0)));

	m_test_fd = open(FULLPATH, O_RDWR );
	EXPECT_LE(0, m_test_fd) << strerror(errno);
}

void TearDown()
{
	if (m_test_fd >= 0)
		close(m_test_fd);
	if (m_backing_fd >= 0)
		close(m_backing_fd);
	if (m_control_fd >= 0)
		close(m_control_fd);
	FuseTest::TearDown();
	leak(m_test_fd);
}

void do_closeopen()
{
	ASSERT_EQ(0, close(m_test_fd)) << strerror(errno);
	m_test_fd = open("backing_file", O_RDWR);
	ASSERT_LE(0, m_test_fd) << strerror(errno);

	ASSERT_EQ(0, close(m_control_fd)) << strerror(errno);
	m_control_fd = open("control", O_RDWR);
	ASSERT_LE(0, m_control_fd) << strerror(errno);
}

void do_ftruncate(off_t offs)
{
	ASSERT_EQ(0, ftruncate(m_test_fd, offs)) << strerror(errno);
	ASSERT_EQ(0, ftruncate(m_control_fd, offs)) << strerror(errno);
	m_filesize = offs;
}

void do_mapread(ssize_t size, off_t offs)
{
	void *control_buf, *p;
	off_t pg_offset, page_mask;
	size_t map_size;

	page_mask = getpagesize() - 1;
	pg_offset = offs & page_mask;
	map_size = pg_offset + size;

	p = mmap(NULL, map_size, PROT_READ, MAP_FILE | MAP_SHARED, m_test_fd,
	    offs - pg_offset);
	ASSERT_NE(p, MAP_FAILED) << strerror(errno);

	control_buf = malloc(size);
	ASSERT_NE(nullptr, control_buf) << strerror(errno);

	ASSERT_EQ(size, pread(m_control_fd, control_buf, size, offs))
		<< strerror(errno);

	compare((void*)((char*)p + pg_offset), control_buf, offs, size);

	ASSERT_EQ(0, munmap(p, map_size)) << strerror(errno);
	free(control_buf);
}

void do_read(ssize_t size, off_t offs)
{
	void *test_buf, *control_buf;
	ssize_t r;

	test_buf = malloc(size);
	ASSERT_NE(nullptr, test_buf) << strerror(errno);
	control_buf = malloc(size);
	ASSERT_NE(nullptr, control_buf) << strerror(errno);

	errno = 0;
	r = pread(m_test_fd, test_buf, size, offs);
	ASSERT_NE(-1, r) << strerror(errno);
	ASSERT_EQ(size, r) << "unexpected short read";
	r = pread(m_control_fd, control_buf, size, offs);
	ASSERT_NE(-1, r) << strerror(errno);
	ASSERT_EQ(size, r) << "unexpected short read";

	compare(test_buf, control_buf, offs, size);

	free(control_buf);
	free(test_buf);
}

void do_mapwrite(ssize_t size, off_t offs)
{
	char *buf;
	void *p;
	off_t pg_offset, page_mask;
	size_t map_size;
	long i;

	page_mask = getpagesize() - 1;
	pg_offset = offs & page_mask;
	map_size = pg_offset + size;

	buf = (char*)malloc(size);
	ASSERT_NE(nullptr, buf) << strerror(errno);
	for (i=0; i < size; i++)
		buf[i] = random();

	if (offs + size > m_filesize) {
		/* 
		 * Must manually extend.  vm_mmap_vnode will not implicitly
		 * extend a vnode
		 */
		do_ftruncate(offs + size);
	}

	p = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
	    MAP_FILE | MAP_SHARED, m_test_fd, offs - pg_offset);
	ASSERT_NE(p, MAP_FAILED) << strerror(errno);

	bcopy(buf, (char*)p + pg_offset, size);
	ASSERT_EQ(size, pwrite(m_control_fd, buf, size, offs))
		<< strerror(errno);

	free(buf);
	ASSERT_EQ(0, munmap(p, map_size)) << strerror(errno);
}

void do_write(ssize_t size, off_t offs)
{
	char *buf;
	long i;

	buf = (char*)malloc(size);
	ASSERT_NE(nullptr, buf) << strerror(errno);
	for (i=0; i < size; i++)
		buf[i] = random();

	ASSERT_EQ(size, pwrite(m_test_fd, buf, size, offs ))
		<< strerror(errno);
	ASSERT_EQ(size, pwrite(m_control_fd, buf, size, offs))
		<< strerror(errno);
	m_filesize = std::max(m_filesize, offs + size);

	free(buf);
}

};

class IoCacheable: public Io {
public:
virtual void SetUp() {
	Io::SetUp();
}
};

/*
 * Extend a file with dirty data in the last page of the last block.
 *
 * fsx -WR -P /tmp -S8 -N3 fsx.bin
 */
TEST_P(Io, extend_from_dirty_page)
{
	off_t wofs = 0x21a0;
	ssize_t wsize = 0xf0a8;
	off_t rofs = 0xb284;
	ssize_t rsize = 0x9b22;
	off_t truncsize = 0x28702;

	do_write(wsize, wofs);
	do_ftruncate(truncsize);
	do_read(rsize, rofs);
}

/*
 * mapwrite into a newly extended part of a file.
 *
 * fsx -c 100 -i 100 -l 524288 -o 131072 -N5 -P /tmp -S19 fsx.bin
 */
TEST_P(IoCacheable, extend_by_mapwrite)
{
	do_mapwrite(0x849e, 0x29a3a);	/* [0x29a3a, 0x31ed7] */
	do_mapwrite(0x3994, 0x3c7d8);	/* [0x3c7d8, 0x4016b] */
	do_read(0xf556, 0x30c16);	/* [0x30c16, 0x4016b] */
}

/*
 * When writing the last page of a file, it must be written synchronously.
 * Otherwise the cached page can become invalid by a subsequent extend
 * operation.
 *
 * fsx -WR -P /tmp -S642 -N3 fsx.bin
 */
TEST_P(Io, last_page)
{
	do_write(0xcc77, 0x1134f);	/* [0x1134f, 0x1dfc5] */
	do_write(0xdfa7, 0x2096a);	/* [0x2096a, 0x2e910] */
	do_read(0xb5b7, 0x1a3aa);	/* [0x1a3aa, 0x25960] */
}

/*
 * Read a hole using mmap
 *
 * fsx -c 100 -i 100 -l 524288 -o 131072 -N11 -P /tmp  -S14 fsx.bin
 */
TEST_P(IoCacheable, mapread_hole)
{
	do_write(0x123b7, 0xf205);	/* [0xf205, 0x215bb] */
	do_mapread(0xeeea, 0x2f4c);	/* [0x2f4c, 0x11e35] */
}

/* 
 * Read a hole from a block that contains some cached data.
 *
 * fsx -WR -P /tmp -S55  fsx.bin
 */
TEST_P(Io, read_hole_from_cached_block)
{
	off_t wofs = 0x160c5;
	ssize_t wsize = 0xa996;
	off_t rofs = 0x472e;
	ssize_t rsize = 0xd8d5;

	do_write(wsize, wofs);
	do_read(rsize, rofs);
}

/*
 * Truncating a file into a dirty buffer should not causing anything untoward
 * to happen when that buffer is eventually flushed.
 *
 * fsx -WR -P /tmp -S839 -d -N6 fsx.bin
 */
TEST_P(Io, truncate_into_dirty_buffer)
{
	off_t wofs0 = 0x3bad7;
	ssize_t wsize0 = 0x4529;
	off_t wofs1 = 0xc30d;
	ssize_t wsize1 = 0x5f77;
	off_t truncsize0 = 0x10916;
	off_t rofs = 0xdf17;
	ssize_t rsize = 0x29ff;
	off_t truncsize1 = 0x152b4;

	do_write(wsize0, wofs0);
	do_write(wsize1, wofs1);
	do_ftruncate(truncsize0);
	do_read(rsize, rofs);
	do_ftruncate(truncsize1);
	close(m_test_fd);
}

/*
 * Truncating a file into a dirty buffer should not causing anything untoward
 * to happen when that buffer is eventually flushed, even when the buffer's
 * dirty_off is > 0.
 *
 * Based on this command with a few steps removed:
 * fsx -WR -P /tmp -S677 -d -N8 fsx.bin
 */
TEST_P(Io, truncate_into_dirty_buffer2)
{
	off_t truncsize0 = 0x344f3;
	off_t wofs = 0x2790c;
	ssize_t wsize = 0xd86a;
	off_t truncsize1 = 0x2de38;
	off_t rofs2 = 0x1fd7a;
	ssize_t rsize2 = 0xc594;
	off_t truncsize2 = 0x31e71;

	/* Sets the file size to something larger than the next write */
	do_ftruncate(truncsize0);
	/* 
	 * Creates a dirty buffer.  The part in lbn 2 doesn't flush
	 * synchronously.
	 */
	do_write(wsize, wofs);
	/* Truncates part of the dirty buffer created in step 2 */
	do_ftruncate(truncsize1);
	/* XXX ?I don't know why this is necessary? */
	do_read(rsize2, rofs2);
	/* Truncates the dirty buffer */
	do_ftruncate(truncsize2);
	close(m_test_fd);
}

/*
 * Regression test for a bug introduced in r348931
 *
 * Sequence of operations:
 * 1) The first write reads lbn so it can modify it
 * 2) The first write flushes lbn 3 immediately because it's the end of file
 * 3) The first write then flushes lbn 4 because it's the end of the file
 * 4) The second write modifies the cached versions of lbn 3 and 4
 * 5) The third write's getblkx invalidates lbn 4's B_CACHE because it's
 *    extending the buffer.  Then it flushes lbn 4 because B_DELWRI was set but
 *    B_CACHE was clear.
 * 6) fuse_write_biobackend erroneously called vfs_bio_clrbuf, putting the
 *    buffer into a weird write-only state.  All read operations would return
 *    0.  Writes were apparently still processed, because the buffer's contents
 *    were correct when examined in a core dump.
 * 7) The third write reads lbn 4 because cache is clear
 * 9) uiomove dutifully copies new data into the buffer
 * 10) The buffer's dirty is flushed to lbn 4
 * 11) The read returns all zeros because of step 6.
 *
 * Based on:
 * fsx -WR -l 524388 -o 131072 -P /tmp -S6456 -q  fsx.bin
 */
TEST_P(Io, resize_a_valid_buffer_while_extending)
{
	do_write(0x14530, 0x36ee6);	/* [0x36ee6, 0x4b415] */
	do_write(0x1507c, 0x33256);	/* [0x33256, 0x482d1] */
	do_write(0x175c, 0x4c03d);	/* [0x4c03d, 0x4d798] */
	do_read(0xe277, 0x3599c);	/* [0x3599c, 0x43c12] */
	close(m_test_fd);
}

INSTANTIATE_TEST_CASE_P(Io, Io,
	Combine(Bool(),					/* async read */
		Values(0x1000, 0x10000, 0x20000),	/* m_maxwrite */
		Values(Uncached, Writethrough, Writeback, WritebackAsync)
	)
);

INSTANTIATE_TEST_CASE_P(Io, IoCacheable,
	Combine(Bool(),					/* async read */
		Values(0x1000, 0x10000, 0x20000),	/* m_maxwrite */
		Values(Writethrough, Writeback, WritebackAsync)
	)
);
