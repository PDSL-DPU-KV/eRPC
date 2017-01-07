#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <mutex>
#include <queue>
#include <string>

#include "common.h"
#include "transport_types.h"

namespace ERpc {

enum SessionEventType { Connected, Disconnected };

class SessionEstablishmentReq {
 public:
  TransportType transport_type;
  int client_sn;            /* Session number at client */
  size_t client_start_seq;  /* Starting sequence number chosen by client */
  RoutingInfo client_route; /* Transport-specific routing info of client */
};

class SessionEstablishmentResp {
 public:
  int server_sn;            /* Session number at server */
  size_t server_start_seq;  /* Starting sequence number chosen by server */
  RoutingInfo server_route; /* Transport-specific routing info of server */
};

/**
 * @brief An object created by the per-thread Rpc, and shared with the
 * per-process Nexus. All accesses must be done with @session_mgmt_mutex locked.
 */
class SessionManagementHook {
 public:
  int app_tid; /* App-level thread ID of the RPC obj that created this hook */
  std::mutex session_mgmt_mutex;
  volatile size_t session_mgmt_ev_counter; /* Number of session mgmt events */
  std::queue<SessionEstablishmentReq> session_req_queue;
  std::queue<SessionEstablishmentResp> session_resp_queue;

  SessionManagementHook() : session_mgmt_ev_counter(0) {}
};

/**
 * @brief A one-to-one session class for all transports
 */
class Session {
 public:
  Session(const char *_rem_hostname, int rem_fdev_port_index,
          uint16_t nexus_udp_port);
  ~Session();

  /**
   * @brief Enables congestion control for this session
   */
  void enable_congestion_control();

  /**
   * @brief Disables congestion control for this session
   */
  void disable_congestion_control();

  std::string rem_hostname;
  int rem_fdev_port_index; /* 0-based port index in the remote device list */
  uint16_t nexus_udp_port; /* The UDP port used by all Nexus-es */

  bool is_cc; /* Is congestion control enabled for this session? */

  /* InfiniBand UD. XXX: Can we reuse these fields? */
  struct ibv_ah *rem_ah;
  int rem_qpn;
};

typedef void (*session_mgmt_handler_t)(Session *, SessionEventType, void *);

}  // End ERpc

#endif  // ERPC_SESSION_H
