/*
 *
 * Usage: ./pingpong-server ip port
 *
 */

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

#include "pingping-common.h"

using std::cin;
using std::cout;
using std::cerr;
using std::endl;

#ifdef RDMASOCKET
#include "RDMAServerSocket.h"
int main(int argc, char* argv[]) {

	char* hostip = argv[1];
	char* port = argv[2];

	int id_num = 0;

	rdma::ServerSocket serverSocket(hostip, port, 1024, 1024);
	rdma::ClientSocket* clientSocket = serverSocket.accept();

	while (1) {
		try {
			rdma::Buffer readPacket = clientSocket->read();
			rdma::Buffer sendPacket = clientSocket->getWriteBuffer();
			memcpy(sendPacket.get(), readPacket.get(), readPacket.size);
			clientSocket->write(sendPacket);
			clientSocket->returnReadBuffer(readPacket);
		} catch (std::exception &e) {
			cerr << "Exception: " << e.what() << endl;
		}
	}

	delete clientSocket;

	return 0;
}
#else
int main(int argc, char* argv[]) {

	char* hostip = argv[1];
	char* port = argv[2];
	pdata rep_pdata;

	rdma_event_channel *cm_channel;
	rdma_cm_id *listen_id;
	rdma_cm_event *event;
	rdma_cm_id *cm_id;
	rdma_conn_param conn_param = { };

	ibv_pd *pd;
	ibv_comp_channel *comp_chan;
	ibv_cq *cq;
	ibv_cq *evt_cq;
	ibv_mr *mr;
	ibv_qp_init_attr qp_attr = { };
	ibv_sge sge;
	ibv_send_wr send_wr = { };
	ibv_send_wr *bad_send_wr;
	ibv_recv_wr recv_wr = { };
	ibv_recv_wr *bad_recv_wr;
	ibv_wc wc;
	void *cq_context;

	sockaddr_in sin;

	try {
		//MAXBUFFERSIZE Byte buffer
		uint32_t *buffer = (uint32_t *) malloc(sizeof(uint32_t) * 10);
		if (!buffer) {
			throw std::runtime_error("malloc buffer failed!");
		}

		query_device();

		cm_channel = rdma_create_event_channel();
		if (!cm_channel) {
			throw std::runtime_error("rdma_create_event_channel failed!");
		}

		if (rdma_create_id(cm_channel, &listen_id, NULL, RDMA_PS_TCP)) {
			throw std::runtime_error("rdma_create_id failed!");
		}

		sin.sin_family = AF_INET;
		sin.sin_port = htons(atoi(port));
		sin.sin_addr.s_addr = INADDR_ANY;

		if (rdma_bind_addr(listen_id, (struct sockaddr *) &sin)) {
			throw std::runtime_error("rdma_bind_addr failed!");
		}

		if (rdma_listen(listen_id, 1)) {
			throw std::runtime_error("rdma_listen failed!");
		}

		if (rdma_get_cm_event(cm_channel, &event)) {
			throw std::runtime_error("rdma_get_cm_event failed!");
		}

		if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
			throw std::runtime_error("RDMA_CM_EVENT_CONNECT_REQUEST failed!");
		}

		cm_id = event->id;

		rdma_ack_cm_event(event);

		pd = ibv_alloc_pd(cm_id->verbs);
		if (!pd) {
			throw std::runtime_error("ibv_alloc_pd failed!");
		}

		comp_chan = ibv_create_comp_channel(cm_id->verbs);
		if (!comp_chan) {
			throw std::runtime_error("ibv_create_comp_channel failed!");
		}

		cq = ibv_create_cq(cm_id->verbs, 2, NULL, comp_chan, 0);
		if (!cq) {
			throw std::runtime_error("ibv_create_cq failed!");
		}
		if (ibv_req_notify_cq(cq, 0)) {
			throw std::runtime_error("ibv_req_notify_cq failed!");
		}

		mr = ibv_reg_mr(pd, buffer, 10 * sizeof(uint32_t),
				IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ
						| IBV_ACCESS_REMOTE_WRITE);
		if (!mr) {
			throw std::runtime_error("ibv_reg_mr failed!");
		}

		qp_attr.cap.max_send_wr = 1;
		qp_attr.cap.max_send_sge = 1;
		qp_attr.cap.max_recv_wr = 1;
		qp_attr.cap.max_recv_sge = 1;

		qp_attr.send_cq = cq;
		qp_attr.recv_cq = cq;

		qp_attr.qp_type = IBV_QPT_RC;

		if (rdma_create_qp(cm_id, pd, &qp_attr)) {
			throw std::runtime_error("rdma_create_qp failed!");
		}

		//prepare to receive from client ---- ping function
		sge.addr = (uintptr_t) (buffer+sizeof(char));
		sge.length = sizeof(uint32_t);
		sge.lkey = mr->lkey;

		recv_wr.sg_list = &sge;
		recv_wr.num_sge = 1;

		if (ibv_post_recv(cm_id->qp, &recv_wr, &bad_recv_wr)) {
			throw std::runtime_error("ibv_post_recv failed!");
		}

		rep_pdata.buf_va = htonll((uintptr_t) buffer);
		rep_pdata.buf_rkey = htonl(mr->rkey);

		conn_param.responder_resources = 1;
		conn_param.private_data = &rep_pdata;
		conn_param.private_data_len = sizeof rep_pdata;

		if (rdma_accept(cm_id, &conn_param)) {
			throw std::runtime_error("rdma_accept failed!");
		}
		if (rdma_get_cm_event(cm_channel, &event)) {
			throw std::runtime_error("rdma_get_cm_event failed!");
		}
		if (event->event != RDMA_CM_EVENT_ESTABLISHED) {
			throw std::runtime_error("RDMA_CM_EVENT_ESTABLISHED failed!");
		}
		rdma_ack_cm_event(event);

		if (ibv_get_cq_event(comp_chan, &evt_cq, &cq_context)) {
			throw std::runtime_error("ibv_get_cq_event failed!");
		}

		if (ibv_req_notify_cq(cq, 0)) {
			throw std::runtime_error("ibv_req_notify_cq failed!");
		}

		if (ibv_poll_cq(cq, 1, &wc) < 1) {
			throw std::runtime_error("ibv_poll_cq failed!");
		}

		if (wc.status != IBV_WC_SUCCESS) {
			throw std::runtime_error("IBV_WC_SUCCESS failed!");
		}

		cerr << "Receive: " << (int)ntohl(buffer[1]) << endl;


		//sent to client ---- pong function
		buffer[0] = htonl(ntohl(buffer[1])+1);
		sge.addr = (uintptr_t) (buffer);
		sge.length = sizeof(uint32_t);
		sge.lkey = mr->lkey;

		send_wr.wr_id = 0;
		send_wr.opcode = IBV_WR_SEND;
		send_wr.send_flags = IBV_SEND_SIGNALED;
		send_wr.sg_list = &sge;
		send_wr.num_sge = 1;

		if (ibv_post_send(cm_id->qp, &send_wr, &bad_send_wr)){
			throw std::runtime_error("ibv_post_send failed!");
		}

		/* Wait for send completion */

		if (ibv_get_cq_event(comp_chan, &evt_cq, &cq_context)){
			throw std::runtime_error("ibv_get_cq_event failed!");
		}

		if (ibv_poll_cq(cq, 1, &wc) < 1){
			throw std::runtime_error("ibv_poll_cq failed!");
		}

		if (wc.status != IBV_WC_SUCCESS){
			throw std::runtime_error("IBV_WC_SUCCESS 2 failed!");
		}
		ibv_ack_cq_events(cq, 2);

		free(buffer);
	} catch (std::exception &e) {
		cerr << "Exception: " << e.what() << endl;
	}

#endif
	return 0;
}

