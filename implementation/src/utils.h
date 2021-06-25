#pragma once

#include <chrono>
#include <xdrpp/marshal.h>

#include "lmdb_wrapper.h"
#include <openssl/sha.h>

#include "simple_debug.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cleanup.h"

#include <errno.h>
#include <string.h>

#include "xdr/database_commitments.h"

namespace edce {


using time_point = std::chrono::time_point<std::chrono::steady_clock>;

template<typename Clock>
inline static double time_diff(const Clock& start, const Clock& end) {
	return ((double)std::chrono::duration_cast<std::chrono::microseconds>(end-start).count()) / 1000000;
}

inline static time_point init_time_measurement() {
	return std::chrono::steady_clock::now();
}

inline static double measure_time(time_point& prev_measurement) {
	auto new_measurement = std::chrono::steady_clock::now();
	auto out = time_diff(prev_measurement, new_measurement);
	prev_measurement = new_measurement;
	return out;
}

inline static double measure_time_from_basept(const time_point& basept) {
	auto new_measurement = std::chrono::steady_clock::now();
	auto out = time_diff(basept, new_measurement);
	return out;
}

template<typename xdr_type>
int __attribute__((warn_unused_result)) load_xdr_from_file_fast(xdr_type& output, const char* filename, unsigned char* buffer, size_t BUF_SIZE) {
	void* buf_head = static_cast<void*>(buffer);//&buffer[0];
	size_t aligned_buf_size = BUF_SIZE;
	unsigned char* aligned_buf = reinterpret_cast<unsigned char*>(std::align(512, sizeof(unsigned char), buf_head, aligned_buf_size));

	unique_fd fd{open(filename, O_DIRECT | O_RDONLY)};

	if (!fd) {
		return -1;
	}

	std::printf("read buf: %p\n", aligned_buf);
	
	aligned_buf_size -= aligned_buf_size % 512;

	int bytes_read = read(fd.get(), aligned_buf, aligned_buf_size);

	if (bytes_read == -1) {
		std::printf("read error %d %s from fd %d\n", errno, strerror(errno), fd.get());
		throw std::runtime_error("read error!");
	}

	std::printf("aligned_buf_size %lu, bytes_read %d\n", aligned_buf_size, bytes_read);

	if (static_cast<size_t>(bytes_read) >= aligned_buf_size) {
		throw std::runtime_error("buffer wasn't big enough!");
	}
 	
 	xdr::xdr_get g(aligned_buf, aligned_buf + bytes_read);
 	xdr::xdr_argpack_archive(g, output);
 	g.done();
 	return 0;
}

template<typename xdr_type>
int __attribute__((warn_unused_result)) load_xdr_from_file(xdr_type& output, const char* filename)  {
	FILE* f = std::fopen(filename, "r");

	if (f == nullptr) {
		return -1;
	}

	std::vector<char> contents;
	const int BUF_SIZE = 65536;
	char buf[BUF_SIZE];

	int count = -1;
	while (count != 0) {
		count = std::fread(buf, sizeof(char), BUF_SIZE, f);
		if (count > 0) {
			contents.insert(contents.end(), buf, buf+count);
		}
	}

	xdr::xdr_from_opaque(contents, output);
	std::fclose(f);
	return 0;
}

static inline void 
flush_buffer(unique_fd& fd, unsigned char* buffer, size_t bytes_to_write) {
	std::size_t idx = 0;

	//std::printf("writing from %p\n", buffer);
	while (idx < bytes_to_write) {
		int written = write(fd.get(), buffer + idx, bytes_to_write - idx);
		if (written < 0) {

			std::printf("errno was %d %s from fd %d\n", errno, strerror(errno), fd.get());
			std::fflush(stdout);
			throw std::runtime_error("error returned from write TODO check and compensate?");
		}
		idx += written;
	}
}

constexpr static auto FILE_PERMISSIONS = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

unique_fd static inline
preallocate_file(const char* filename, size_t size = 0) {

	unique_fd fd{open(filename, O_CREAT | O_WRONLY | O_DIRECT, FILE_PERMISSIONS)};

	if (size == 0) {
		return fd;
	}
	auto res = fallocate(fd.get(), 0, 0, size);
	if (res) {
		std::printf("errno was %d %s\n", errno, strerror(errno));
		throw std::runtime_error("fallocate error");
	}
	return fd;
}

template<typename xdr_list_type, unsigned int BUF_SIZE = 65535>
void save_xdr_to_file_fast(const xdr_list_type& value, const char* filename, const size_t prealloc_size = 64000000)  {
	unsigned char buffer[BUF_SIZE];
	auto fd = preallocate_file(filename, prealloc_size);
	save_xdr_to_file_fast<xdr_list_type>(value, fd, buffer, BUF_SIZE);
}

template<typename xdr_list_type>
void save_xdr_to_file_fast(const xdr_list_type& value, unique_fd& fd, unsigned char* buffer, const unsigned int BUF_SIZE) {
	//unique_fd fd {open(filename, O_CREAT | O_WRONLY | O_DIRECT, FILE_PERMISSIONS)};

	std::printf("saving to fd %d using bufsize = %u\n", fd.get(), BUF_SIZE);
	std::size_t list_size = value.size();

	unsigned int buf_idx = 0;

	void* buf_head = static_cast<void*>(buffer);//&buffer[0];

	size_t aligned_buf_size = BUF_SIZE;

	unsigned char* aligned_buf = reinterpret_cast<unsigned char*>(std::align(512, sizeof(unsigned char), buf_head, aligned_buf_size));

	uint32_t* aligned_buf_cast = reinterpret_cast<uint32_t*>(aligned_buf);

	aligned_buf_size -= (aligned_buf_size % 4);
	aligned_buf_size -= 512; // ensure space for last few bits


	std::size_t list_idx = 0;

	xdr::xdr_put p (aligned_buf, aligned_buf + aligned_buf_size);


	//std::printf("before p_:%p\n", p.p_);

	p.put32(aligned_buf_cast, xdr::size32(list_size));
	p.p_ ++;
	buf_idx += 4;

	//std::printf("after p_:%p\n", p.p_);

	size_t total_written_bytes = 0;

	while (list_idx < list_size) {
		
		size_t next_sz = xdr::xdr_argpack_size(value[list_idx]);

		if (aligned_buf_size - buf_idx < next_sz) {

			size_t write_amount = buf_idx - (buf_idx % 512);

			flush_buffer(fd, aligned_buf, write_amount);
			total_written_bytes += write_amount;
			memcpy(aligned_buf, aligned_buf + write_amount, buf_idx % 512);
			buf_idx %= 512;

			if (buf_idx % 4 != 0) {
				throw std::runtime_error("buf_idx should always be 0 mod 4");
			}

			p.p_ = reinterpret_cast<uint32_t*>(aligned_buf + buf_idx);//static_cast<uint32_t*>(&(buffer[0])); // reset
			//p = std::move(xdr::xdr_put(buffer, buffer + BUF_SIZE));
		}
		p(value[list_idx]);

		buf_idx += next_sz;

		list_idx ++;
	}


	size_t write_amount = buf_idx - (buf_idx % 512) + 512;
	flush_buffer(fd, (aligned_buf), write_amount);

	total_written_bytes += buf_idx;
	auto res = ftruncate(fd.get(), total_written_bytes);

	if (res) {
		std::printf("errno was %d %s\n", errno, strerror(errno));
		throw std::runtime_error("invalid ftruncate result");
	}

	BLOCK_INFO("total written bytes = %lu", total_written_bytes);

	fsync(fd.get());
}

[[maybe_unused]]
static void save_account_block_fast(const AccountModificationBlock& value, unique_fd& fd, unsigned char* buffer, const unsigned int BUF_SIZE) {

	//std::printf("saving to fd %d using bufsize = %u num_txs = %lu\n", fd.get(), BUF_SIZE, list_size);
	//std::size_t list_size = value.size();

	unsigned int buf_idx = 0;

	void* buf_head = static_cast<void*>(buffer);//&buffer[0];

	size_t aligned_buf_size = BUF_SIZE;

	unsigned char* aligned_buf = reinterpret_cast<unsigned char*>(std::align(512, sizeof(unsigned char), buf_head, aligned_buf_size));

	uint32_t* aligned_buf_cast = reinterpret_cast<uint32_t*>(aligned_buf);

	aligned_buf_size -= (aligned_buf_size % 4);
	aligned_buf_size -= 512; // ensure space for last few bits



	size_t first_odirect_block_sz = 1024;
	uint8_t first_odirect_block[1024];
	bool first_set = false;

	void* first_head = static_cast<void*>(first_odirect_block);

	uint8_t* aligned_first = reinterpret_cast<uint8_t*>(std::align(512, sizeof(uint8_t), first_head, first_odirect_block_sz));
	uint32_t* aligned_first_reinterpreted = reinterpret_cast<uint32_t*>(aligned_first);


	std::size_t list_idx = 0;

	xdr::xdr_put p (aligned_buf, aligned_buf + aligned_buf_size);


	//std::printf("before p_:%p\n", p.p_);

	p.put32(aligned_buf_cast, xdr::size32(0)); //space for now
	p.p_ ++;
	buf_idx += 4;

	//std::printf("after p_:%p\n", p.p_);

	size_t total_written_bytes = 0;

	const xdr::xvector<SignedTransaction>* tx_buffer = nullptr;
	size_t buffer_idx = 0;
	size_t num_written = 0;

	while (list_idx < value.size()) {

		while (tx_buffer == nullptr) {
			if (value[list_idx].new_transactions_self.size() != 0) {
				tx_buffer = &(value[list_idx].new_transactions_self);
				buffer_idx = 0;
			}
			list_idx ++;
			if (list_idx >= value.size()) {
				break;
			}
		}

		if (tx_buffer == nullptr) {
			break;
		}

		auto& next_tx_to_write = (*tx_buffer)[buffer_idx];

		size_t next_sz = xdr::xdr_argpack_size(next_tx_to_write);

		if (aligned_buf_size - buf_idx < next_sz) {

			size_t write_amount = buf_idx - (buf_idx % 512);

			if (!first_set) {
				first_set = true;
				memcpy(aligned_first, aligned_buf, 512);
			}

			flush_buffer(fd, aligned_buf, write_amount);
			total_written_bytes += write_amount;
			memcpy(aligned_buf, aligned_buf + write_amount, buf_idx % 512);
			buf_idx %= 512;

			//if (buf_idx % 4 != 0) {
			//	throw std::runtime_error("buf_idx should always be 0 mod 4");
			//}

			p.p_ = reinterpret_cast<uint32_t*>(aligned_buf + buf_idx);//static_cast<uint32_t*>(&(buffer[0])); // reset
			//p = std::move(xdr::xdr_put(buffer, buffer + BUF_SIZE));
		}

		p(next_tx_to_write);
		num_written ++;
		buffer_idx++;
		if (buffer_idx >= tx_buffer -> size()) {
			tx_buffer = nullptr;
		}

		buf_idx += next_sz;

		//list_idx ++;
	}

	if (!first_set) {
		first_set = true;
		memcpy(aligned_first, aligned_buf, 512);
	}


	BLOCK_INFO("num written txs: %lu\n", num_written);
	//if (num_written != list_size) {
	//	std::printf("num_written %lu list_size %lu\n", num_written, list_size);
	//	std::fflush(stdout);
	//	throw std::runtime_error("mismatch between num_written and list_size!");
	//}


	size_t write_amount = buf_idx - (buf_idx % 512) + 512;
	flush_buffer(fd, (aligned_buf), write_amount);

	total_written_bytes += buf_idx;



	auto res2 = lseek(fd.get(), 0, SEEK_SET);
	if (res2 != 0) {
		throw std::runtime_error("got error from lseek " + std::to_string(res2));
	}

	p.put32(aligned_first_reinterpreted, xdr::size32(num_written));
	flush_buffer(fd, aligned_first, 512);

	auto res = ftruncate(fd.get(), total_written_bytes);

	if (res) {
		std::printf("errno was %d %s\n", errno, strerror(errno));
		throw std::runtime_error("invalid ftruncate result");
	}

	BLOCK_INFO("total written bytes = %lu", total_written_bytes);
	
	fsync(fd.get());
}

/*
SerializedBlock fast_serialize_block(const AccountModificationBlock& block) {
	size_t num_txs = 0;
	size_t used_bytes = 0;

	SerializedBlock output;
	output.resize(100'000'000);


}*/


template<typename xdr_type>
int __attribute__((warn_unused_result))  save_xdr_to_file(const xdr_type& value, const char* filename) {

	std::printf("starting save_xdr_to_file\n");
	FILE* f = std::fopen(filename, "w");

	if (f == nullptr) {
		return -1;
		//throw std::runtime_error("could not open output file");
	}

	auto timestamp = init_time_measurement();

	auto buf = xdr::xdr_to_opaque(value);

	auto res = measure_time(timestamp);
	std::printf("serializing took %lf\n", res);

	std::fwrite(buf.data(), sizeof(buf.data()[0]), buf.size(), f);
	std::fflush(f);

	res = measure_time(timestamp);
	std::printf("flush took %lf\n", res);

	fsync(fileno(f));

	res = measure_time(timestamp);
	std::printf("fsync took %lf\n", res);
	std::fclose(f);
	return 0;
}

/*
template<typename xdr_type>
dbval xdr_to_dbval(const xdr_type& value) {
	auto buf = xdr::xdr_to_msg(value);
	return dbval(buf->data(), buf->size());
}*/

template<typename xdr_type>
void dbval_to_xdr(const dbval& d, xdr_type& value) {
	auto bytes = d.bytes();
	xdr::xdr_from_opaque(bytes, value);
}

template<typename xdr_type>
void hash_xdr(const xdr_type& value, Hash& hash_out) {

	auto buf = xdr::xdr_to_msg(value);

	const unsigned char* msg = (const unsigned char*) buf->data();
	auto msg_len = buf->size();

	SHA256(msg, msg_len, hash_out.data());
}

//Doesn't fail if it already exists, but fails for any other reason.
constexpr static auto mkdir_perms = S_IRWXU | S_IRWXG | S_IRWXO;
[[maybe_unused]]
static bool 
mkdir_safe(const char* dirname) {
	auto res = mkdir(dirname, mkdir_perms);
	if (res == 0) {
		return false;
	}

	if (errno == EEXIST) {
		return true;
	}

	throw std::runtime_error("mkdir failed!");
}

}
