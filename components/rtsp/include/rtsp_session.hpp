#pragma once

#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "esp_random.h"

#include "logger.hpp"
#include "task.hpp"
#include "tcp_socket.hpp"
#include "udp_socket.hpp"

#include "rtcp_packet.hpp"
#include "rtp_packet.hpp"

namespace espp {
/// Class that reepresents an RTSP session, which is uniquely identified by a
/// session id and sends frame data over RTP and RTCP to the client
class RtspSession {
public:
  /// Configuration for the RTSP session
  struct Config {
    std::string server_address;                            ///< The address of the server
    std::string rtsp_path;                                 ///< The RTSP path of the session
    Logger::Verbosity log_level = Logger::Verbosity::WARN; ///< The log level of the session
  };

  /// @brief Construct a new RtspSession object
  /// @param control_socket The control socket of the session
  /// @param config The configuration of the session
  explicit RtspSession(std::unique_ptr<TcpSocket> control_socket, const Config &config)
      : control_socket_(std::move(control_socket)),
        rtp_socket_({.log_level = Logger::Verbosity::WARN}),
        rtcp_socket_({.log_level = Logger::Verbosity::WARN}), session_id_(generate_session_id()),
        server_address_(config.server_address), rtsp_path_(config.rtsp_path),
        client_address_(control_socket_->get_remote_info().address),
        logger_({.tag = "RtspSession " + std::to_string(session_id_), .level = config.log_level}) {
    // start the session task to handle RTSP commands
    using namespace std::placeholders;
    control_task_ = std::make_unique<Task>(Task::Config{
        .name = "RtspSession " + std::to_string(session_id_),
        .callback = std::bind(&RtspSession::control_task_fn, this, _1, _2),
        .stack_size_bytes = 6 * 1024,
        .log_level = Logger::Verbosity::WARN,
    });
    control_task_->start();
  }

  ~RtspSession() {
    teardown();
    // stop the session task
    if (control_task_ && control_task_->is_started()) {
      logger_.info("Stopping control task");
      control_task_->stop();
    }
  }

  /// @brief Get the session id
  /// @return The session id
  uint32_t get_session_id() const { return session_id_; }

  /// @brief Check if the session is closed
  /// @return True if the session is closed, false otherwise
  bool is_closed() const { return closed_; }

  /// Get whether the session is connected
  /// @return True if the session is connected, false otherwise
  bool is_connected() const { return control_socket_ && control_socket_->is_connected(); }

  /// Get whether the session is active
  /// @return True if the session is active, false otherwise
  bool is_active() const { return session_active_; }

  /// Mark the session as active
  /// This will cause the server to start sending frames to the client
  void play() { session_active_ = true; }

  /// Pause the session
  /// This will cause the server to stop sending frames to the client
  /// @note This does not stop the session, it just pauses it
  /// @note This is useful for when the client is buffering
  void pause() { session_active_ = false; }

  /// Teardown the session
  /// This will cause the server to stop sending frames to the client
  /// and close the connection
  void teardown() {
    session_active_ = false;
    closed_ = true;
  }

  /// Send an RTP packet to the client
  /// @param packet The RTP packet to send
  /// @return True if the packet was sent successfully, false otherwise
  bool send_rtp_packet(const RtpPacket &packet) {
    logger_.debug("Sending RTP packet");
    return rtp_socket_.send(packet.get_data(), {
                                                   .ip_address = client_address_,
                                                   .port = (size_t)client_rtp_port_,
                                               });
  }

  /// Send an RTCP packet to the client
  /// @param packet The RTCP packet to send
  /// @return True if the packet was sent successfully, false otherwise
  bool send_rtcp_packet(const RtcpPacket &packet) {
    logger_.debug("Sending RTCP packet");
    return rtcp_socket_.send(packet.get_data(), {
                                                    .ip_address = client_address_,
                                                    .port = (size_t)client_rtcp_port_,
                                                });
  }

protected:
  /// Send a response to a RTSP request
  /// @param code The response code
  /// @param message The response message
  /// @param sequence_number The sequence number of the request
  /// @param headers The response headers (optional)
  /// @param body The response body (optional)
  /// @return True if the response was sent successfully, false otherwise
  bool send_response(int code, std::string_view message, int sequence_number = -1,
                     std::string_view headers = "", std::string_view body = "") {
    // create a response
    std::string response = "RTSP/1.0 " + std::to_string(code) + " " + std::string(message) + "\r\n";
    if (sequence_number != -1) {
      response += "CSeq: " + std::to_string(sequence_number) + "\r\n";
    }
    if (!headers.empty()) {
      response += headers;
    }
    if (!body.empty()) {
      response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
      response += "\r\n";
      response += body;
    } else {
      response += "\r\n";
    }
    logger_.info("Sending RTSP response");
    logger_.debug("{}", response);
    // send the response
    return control_socket_->transmit(response);
  }

  /// Handle a RTSP options request
  /// @param request The RTSP request
  /// @return True if the request was handled successfully, false otherwise
  bool handle_rtsp_options(std::string_view request) {
    int sequence_number = 0;
    if (!parse_rtsp_command_sequence(request, sequence_number)) {
      return handle_rtsp_invalid_request(request);
    }
    logger_.info("RTSP OPTIONS request");
    // create a response
    int code = 200;
    std::string message = "OK";
    std::string headers = "Public: DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE\r\n";
    return send_response(code, message, sequence_number, headers);
  }

  /// Handle a RTSP describe request
  /// Create a SDP description and send it back to the client
  /// @param request The RTSP request
  /// @return True if the request was handled successfully, false otherwise
  bool handle_rtsp_describe(std::string_view request) {
    int sequence_number = 0;
    if (!parse_rtsp_command_sequence(request, sequence_number)) {
      return handle_rtsp_invalid_request(request);
    }
    logger_.info("RTSP DESCRIBE request");
    // create a response
    int code = 200;
    std::string message = "OK";
    // SDP description for an MJPEG stream
    std::string rtsp_path = "rtsp://" + server_address_ + "/" + rtsp_path_;
    std::string body = "v=0\r\n" // version (0)
                       "o=- " +
                       std::to_string(session_id_) + " 1 IN IP4 " + server_address_ +
                       "\r\n" // username (none), session id, version, network type (internet),
                              // address type, address
                       "s=MJPEG Stream\r\n" // session name (can be anything)
                       "i=MJPEG Stream\r\n" // session name (can be anything)
                       "t=0 0\r\n"          // start / stop
                       "a=control:" +
                       rtsp_path +
                       "\r\n"                                          // the RTSP path
                       "a=mimetype:string;\"video/x-motion-jpeg\"\r\n" // MIME type
                       "m=video 0 RTP/AVP 26\r\n"                      // MJPEG
                       "c=IN IP4 0.0.0.0\r\n" // client will use the RTSP address
                       "b=AS:256\r\n"         // 256kbps
                       "a=control:" +
                       rtsp_path +
                       "\r\n"
                       "a=udp-only\r\n";

    std::string headers = "Content-Type: application/sdp\r\n"
                          "Content-Base: " +
                          rtsp_path + "\r\n";
    return send_response(code, message, sequence_number, headers, body);
  }

  /// Handle a RTSP setup request
  /// Create a session and send the RTP port numbers back to the client
  /// @param request The RTSP request
  /// @return True if the request was handled successfully, false otherwise
  bool handle_rtsp_setup(std::string_view request) {
    // parse the rtsp path from the request
    std::string_view rtsp_path;
    int client_rtp_port;
    int client_rtcp_port;
    if (!parse_rtsp_setup_request(request, rtsp_path, client_rtp_port, client_rtcp_port)) {
      // the parse function will send the response, so we just need to return
      return false;
    }
    // parse the sequence number from the request
    int sequence_number = 0;
    if (!parse_rtsp_command_sequence(request, sequence_number)) {
      return handle_rtsp_invalid_request(request);
    }
    logger_.info("RTSP SETUP request");
    // save the client port numbers
    client_rtp_port_ = client_rtp_port;
    client_rtcp_port_ = client_rtcp_port;
    // create a response
    int code = 200;
    std::string message = "OK";
    // flesh out the transport header
    std::string headers =
        "Session: " + std::to_string(session_id_) + "\r\n" +
        "Transport: RTP/AVP;unicast;client_port=" + std::to_string(client_rtp_port) + "-" +
        std::to_string(client_rtcp_port) + "\r\n";
    return send_response(code, message, sequence_number, headers);
  }

  /// Handle a RTSP play request
  /// After responding to the request, the server should start sending RTP
  /// packets to the client
  /// @param request The request to handle
  /// @return True if the request was handled successfully, false otherwise
  bool handle_rtsp_play(std::string_view request) {
    int sequence_number = 0;
    if (!parse_rtsp_command_sequence(request, sequence_number)) {
      return handle_rtsp_invalid_request(request);
    }
    logger_.info("RTSP PLAY request");
    play();
    int code = 200;
    std::string message = "OK";
    std::string headers =
        "Session: " + std::to_string(session_id_) + "\r\n" + "Range: npt=0.000-\r\n";
    return send_response(code, message, sequence_number, headers);
  }

  /// Handle a RTSP pause request
  /// After responding to the request, the server should stop sending RTP
  /// packets to the client
  /// @param request The request to handle
  /// @return True if the request was handled successfully, false otherwise
  bool handle_rtsp_pause(std::string_view request) {
    int sequence_number = 0;
    if (!parse_rtsp_command_sequence(request, sequence_number)) {
      return handle_rtsp_invalid_request(request);
    }
    logger_.info("RTSP PAUSE request");
    pause();
    int code = 200;
    std::string message = "OK";
    std::string headers = "Session: " + std::to_string(session_id_) + "\r\n";
    return send_response(code, message, sequence_number, headers);
  }

  /// Handle an RTSP teardown request
  /// @param request The request to handle
  /// @return True if the request was handled successfully, false otherwise
  bool handle_rtsp_teardown(std::string_view request) {
    int sequence_number = 0;
    if (!parse_rtsp_command_sequence(request, sequence_number)) {
      return handle_rtsp_invalid_request(request);
    }
    logger_.info("RTSP TEARDOWN request");
    teardown();
    int code = 200;
    std::string message = "OK";
    std::string headers = "Session: " + std::to_string(session_id_) + "\r\n";
    return send_response(code, message, sequence_number, headers);
  }

  /// Handle an invalid RTSP request
  /// @param request The request to handle
  /// @return True if the request was handled successfully, false otherwise
  bool handle_rtsp_invalid_request(std::string_view request) {
    logger_.info("RTSP invalid request");
    // create a response
    int code = 400;
    std::string message = "Bad Request";
    int sequence_number = 0;
    if (!parse_rtsp_command_sequence(request, sequence_number)) {
      return send_response(code, message);
    }
    return send_response(code, message, sequence_number);
  }

  /// Handle a single RTSP request
  /// @note Requests are of the form "METHOD RTSP_PATH RTSP_VERSION"
  /// @param request The request to handle
  /// @return True if the request was handled successfully, false otherwise
  bool handle_rtsp_request(std::string_view request) {
    logger_.debug("RTSP request:\n{}", request);
    // store indices of the first and second spaces
    // to extract the method and the rtsp path
    auto first_space_index = request.find(' ');
    auto second_space_index = request.find(' ', first_space_index + 1);
    auto end_of_line_index = request.find('\r');
    if (first_space_index == std::string::npos || second_space_index == std::string::npos ||
        end_of_line_index == std::string::npos) {
      return handle_rtsp_invalid_request(request);
    }
    // extract the method and the rtsp path
    // where the request looks like "METHOD RTSP_PATH RTSP_VERSION"
    std::string_view method = request.substr(0, first_space_index);
    // TODO: we should probably check that the rtsp path is correct
    [[maybe_unused]] std::string_view rtsp_path =
        request.substr(first_space_index + 1, second_space_index - first_space_index - 1);
    // TODO: we should probably check that the rtsp version is correct
    [[maybe_unused]] std::string_view rtsp_version =
        request.substr(second_space_index + 1, end_of_line_index - second_space_index - 1);
    // extract the request body, which is separated by an empty line (\r\n)
    // from the request header
    std::string_view request_body = request.substr(end_of_line_index + 2);

    // handle the request
    if (method == "OPTIONS") {
      return handle_rtsp_options(request_body);
    } else if (method == "DESCRIBE") {
      return handle_rtsp_describe(request_body);
    } else if (method == "SETUP") {
      return handle_rtsp_setup(request_body);
    } else if (method == "PLAY") {
      return handle_rtsp_play(request_body);
    } else if (method == "PAUSE") {
      return handle_rtsp_pause(request_body);
    } else if (method == "TEARDOWN") {
      return handle_rtsp_teardown(request_body);
    }

    // if the method is not supported, return an error
    return handle_rtsp_invalid_request(request_body);
  }

  /// @brief The task function for the control thread
  /// @param m The mutex to lock when waiting on the condition variable
  /// @param cv The condition variable to wait on
  /// @return True if the task should stop, false otherwise
  bool control_task_fn(std::mutex &m, std::condition_variable &cv) {
    if (closed_) {
      logger_.info("Session is closed, stopping control task");
      // return true to stop the task
      return true;
    }
    if (!control_socket_) {
      logger_.warn("Control socket is no longer valid, stopping control task");
      teardown();
      // return true to stop the task
      return true;
    }
    if (!control_socket_->is_connected()) {
      logger_.warn("Control socket is not connected, stopping control task");
      teardown();
      // if the control socket is not connected, return true to stop the task
      return true;
    }
    static size_t max_request_size = 1024;
    std::vector<uint8_t> buffer;
    logger_.info("Waiting for RTSP request");
    if (control_socket_->receive(buffer, max_request_size)) {
      // parse the request
      std::string_view request(reinterpret_cast<char *>(buffer.data()), buffer.size());
      // handle the request
      if (!handle_rtsp_request(request)) {
        logger_.warn("Failed to handle RTSP request");
      }
    }
    // the receive handles most of the blocking, so we don't need to sleep
    // here, just return false to keep the task running
    return false;
  }

  /// Generate a new RTSP session id for the client
  /// Session IDs are generated randomly when a client sends a SETUP request and are
  /// used to identify the client in subsequent requests when managing the RTP session.
  /// @return The new session id
  uint32_t generate_session_id() { return esp_random(); }

  /// Parse the RTSP command sequence number from a request
  /// @param request The request to parse
  /// @param cseq The command sequence number (output)
  /// @return True if the request was parsed successfully, false otherwise
  bool parse_rtsp_command_sequence(std::string_view request, int &cseq) {
    // parse the cseq from the request
    auto cseq_index = request.find("CSeq: ");
    if (cseq_index == std::string::npos) {
      return false;
    }
    auto cseq_end_index = request.find('\r', cseq_index);
    if (cseq_end_index == std::string::npos) {
      return false;
    }
    std::string_view cseq_str = request.substr(cseq_index + 6, cseq_end_index - cseq_index - 6);
    if (cseq_str.empty()) {
      return false;
    }
    // convert the cseq to an integer
    cseq = std::stoi(std::string{cseq_str});
    return true;
  }

  /// Parse a RTSP path from a request
  /// @param request The request to parse
  /// @return The RTSP path
  std::string_view parse_rtsp_path(std::string_view request) {
    // parse the rtsp path from the request
    // where the request looks like "METHOD RTSP_PATH RTSP_VERSION"
    std::string_view rtsp_path = request.substr(
        request.find(' ') + 1, request.find(' ', request.find(' ') + 1) - request.find(' ') - 1);
    return rtsp_path;
  }

  /// Parse a RTSP setup request
  /// Looks for the client RTP and RTCP port numbers in the request
  /// and returns them
  /// @param request The request to parse
  /// @param rtsp_path The RTSP path from the request (output)
  /// @param client_rtp_port The client RTP port number (output)
  /// @param client_rtcp_port The client RTCP port number (output)
  /// @return True if the request was parsed successfully, false otherwise
  bool parse_rtsp_setup_request(std::string_view request, std::string_view &rtsp_path,
                                int &client_rtp_port, int &client_rtcp_port) {
    // parse the rtsp path from the request
    rtsp_path = parse_rtsp_path(request);
    if (rtsp_path.empty()) {
      return false;
    }
    logger_.debug("Parsing setup request:\n{}", request);
    // parse the transport header from the request
    auto transport_index = request.find("Transport: ");
    if (transport_index == std::string::npos) {
      return false;
    }
    auto transport_end_index = request.find('\r', transport_index);
    if (transport_end_index == std::string::npos) {
      return false;
    }
    std::string_view transport =
        request.substr(transport_index + 11, transport_end_index - transport_index - 11);
    if (transport.empty()) {
      return false;
    }
    logger_.debug("Transport header: {}", transport);
    // we don't support TCP, so return an error if the transport is not RTP/AVP/UDP
    if (transport.find("RTP/AVP/TCP") != std::string::npos) {
      logger_.error("TCP transport is not supported");
      // TODO: this doesn't send the sequence number back to the client
      send_response(461, "Unsupported Transport");
      return false;
    }

    // parse the rtp port from the request
    auto client_port_index = request.find("client_port=");
    auto dash_index = request.find('-', client_port_index);
    std::string_view rtp_port =
        request.substr(client_port_index + 12, dash_index - client_port_index - 12);
    if (rtp_port.empty()) {
      return false;
    }
    // parse the rtcp port from the request
    std::string_view rtcp_port =
        request.substr(dash_index + 1, request.find('\r', client_port_index) - dash_index - 1);
    if (rtcp_port.empty()) {
      return false;
    }
    // convert the rtp and rtcp ports to integers
    client_rtp_port = std::stoi(std::string{rtp_port});
    client_rtcp_port = std::stoi(std::string{rtcp_port});
    return true;
  }

  std::unique_ptr<espp::TcpSocket> control_socket_;
  espp::UdpSocket rtp_socket_;
  espp::UdpSocket rtcp_socket_;

  uint32_t session_id_;
  bool closed_ = false;
  bool session_active_ = false;

  std::string server_address_;
  std::string rtsp_path_;

  std::string client_address_;
  int client_rtp_port_;
  int client_rtcp_port_;

  std::unique_ptr<Task> control_task_;

  Logger logger_;
};
} // namespace espp
