#pragma once

#include <boost/noncopyable.hpp>
#include <boost/circular_buffer.hpp>
#include <string>
#include <stdint.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

namespace rdma {

std::string getLastErrorMessage();

class ClientSocket;

class Buffer {
	friend class ClientSocket;
public:
	Buffer() :
			buffer(NULL), size(0) {
	}

	void* get() {
		return buffer;
	}
	void setMaxSize(size_t size){
		maxSize = size;
	}
	void setSize(size_t size){
		if(size > maxSize){
			throw std::runtime_error("Buffer size is to larger!");
		}
		this->size = size;
	}

	size_t size;
private:
	Buffer(void* buffer, size_t size) :
			buffer(buffer), size(size) {
		maxSize = size;
	}

	size_t maxSize;
	void* buffer;
};

class ClientSocket: boost::noncopyable {
public:
	ClientSocket(const char* host, const char* port, uint32_t packetSize,
			uint32_t packetWindowSize);

	ClientSocket(struct rdma_cm_id* clientId, uint32_t packetSize,
			uint32_t packetWindowSize);

	~ClientSocket();

	Buffer getWriteBuffer();

	void write(const Buffer& buffer);

	Buffer read();

	void returnReadBuffer(const Buffer& buffer);

private:
	void setupBuffers();

	struct rdma_cm_id* clientId;
	struct ibv_mr* memoryRegion;
	uint32_t packetSize;
	uint32_t packetWindowSize;
	char* buffer;
	boost::circular_buffer<Buffer> writeBuffers;
};

}
;

