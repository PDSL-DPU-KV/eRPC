/**
 * @file rpc_datapath.cc
 * @brief Performance-critical Rpc datapath functions
 */

#include <iostream>
#include <stdexcept>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class Transport_>
int Rpc<Transport_>::send_request(Session *session, uint8_t req_type,
                                  MsgBuffer *req_msgbuf) {
  if (!kDatapathChecks) {
    assert(session != nullptr && session->role == Session::Role::kClient);
    assert(session->state == SessionState::kConnected);
    assert(req_msgbuf != nullptr && req_msgbuf->buf != nullptr &&
           req_msgbuf->check_pkthdr_0());
    assert(req_msgbuf->data_size > 0 && req_msgbuf->data_size <= kMaxMsgSize);
    assert(req_msgbuf->num_pkts > 0);
  } else {
    /* If datapath checks are enabled, return meaningful error codes */
    if (unlikely(session == nullptr ||
                 session->role != Session::Role::kClient ||
                 session->state == SessionState::kConnected)) {
      return static_cast<int>(RpcDatapathErrCode::kInvalidSessionArg);
    }

    if (unlikely(req_msgbuf == nullptr || req_msgbuf->buf == nullptr ||
                 !req_msgbuf->check_pkthdr_0())) {
      return static_cast<int>(RpcDatapathErrCode::kInvalidMsgBufferArg);
    }

    if (unlikely(req_msgbuf->data_size == 0 ||
                 req_msgbuf->data_size > kMaxMsgSize ||
                 req_msgbuf->num_pkts == 0)) {
      return static_cast<int>(RpcDatapathErrCode::kInvalidMsgBufferArg);
    }
  }

  if (session->sslot_free_vec.size() == 0) {
    /*
     * No free message slots left in session, so we can't queue this request.
     * This needs to be done even without kDatapathChecks.
     */
    return static_cast<int>(RpcDatapathErrCode::kNoSessionMsgSlots);
  }

  /* Find a free message slot in the session */
  size_t free_sslot = session->sslot_free_vec.pop_back();
  assert(free_sslot < Session::kSessionReqWindow);
  assert(!session->sslot_arr[free_sslot].in_use);

  /* Generate the next request number for this slot */
  size_t req_num =
      (req_num_arr[free_sslot]++ * Session::kSessionReqWindow) + /* Shift */
      free_sslot;

  // Fill in packet 0's header
  pkthdr_t *pkthdr_0 = req_msgbuf->get_pkthdr_0();
  pkthdr_0->req_type = req_type;
  pkthdr_0->msg_size = req_msgbuf->data_size;
  pkthdr_0->rem_session_num = session->server.session_num;
  pkthdr_0->is_req = 1;
  pkthdr_0->is_first = 1;
  pkthdr_0->is_expected = 0;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = req_num;
  /* pkthdr->magic is already filled in */

  if (small_msg_likely(req_msgbuf->num_pkts == 1)) {
    /* Nothing else needs to be done for small packets */
  } else {
    /*
     * Headers for non-zeroth packets are created by copying the 0th header, and
     * changing only the required fields. All request packets are Unexpected.
     */
    for (size_t i = 1; i < req_msgbuf->num_pkts; i++) {
      pkthdr_t *pkthdr_i = req_msgbuf->get_pkthdr_n(i);
      *pkthdr_i = *pkthdr_0;
      pkthdr_i->is_first = 0;
      pkthdr_i->pkt_num = i;
    }
  }

  /* Fill in the session message slot */
  session->sslot_arr[free_sslot].req_msgbuf = req_msgbuf;
  session->sslot_arr[free_sslot].in_use = true;

  /* Add \p session to the work queue if it's not already present */
  if (!session->in_datapath_tx_work_queue) {
    session->in_datapath_tx_work_queue = true;
    datapath_tx_work_queue.push_back(session);
  }

  return 0;
}

}  // End ERpc