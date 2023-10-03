#ifndef _NYSY_HTTP_SERVER_
#define _NYSY_HTTP_SERVER_
#include "nysytcplib.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <memory>
#include <list>
#include "nysythreadpool.hpp"

/*
*Usage:
*1, Place index.html in the root directory, as well as other files and folders
*2, Specify the port, listen backlog and number of threads in the HTTPServer constructor
*3, call the member fucnction start()
*4, all set!
*/

namespace nysy {
	class Request {
	public:
		static std::vector<std::string> request_set;

		std::string m_request_type, m_url, m_http_ver, m_content_type;

		Request() :m_request_type("INVALID"), m_url(""), m_http_ver("0.0"), m_content_type("") {}

		void parse(const std::string& data) {
			std::string requestType, url, httpVer;
			std::stringstream ss{data};
			ss >> requestType >> url >> httpVer;
			if (std::find(request_set.begin(), request_set.end(), requestType) != request_set.end())
				m_request_type = requestType;

			m_url = ".";
			m_url.append(url);
			if (m_url.ends_with('/'))m_url.append("index.html");

			if (httpVer.starts_with("HTTP/")) {
				httpVer.erase(0, 5);
				m_http_ver = httpVer;
			}

			std::string ex_name = m_url;
			std::size_t dot_pos = ex_name.find(".");
			ex_name.erase(0, dot_pos + 1);

			std::size_t accept_line_b = data.find(std::string("Accept: "));
			std::size_t accept_line_e = data.find(std::string("\r\n"), accept_line_b);
			std::string acc_data = data;
			acc_data = acc_data.substr(accept_line_b + 8, accept_line_e);
			std::stringstream accept_ss{acc_data};
			std::string accept_buf;
			while (!accept_ss.eof()) {
				std::getline(accept_ss, accept_buf, ',');
				if (accept_buf.find(ex_name) != std::string::npos) {
					m_content_type = accept_buf;
					break;
				}
			}

		}

		std::string get_request_type()const { return m_request_type; }

		std::string get_url()const { return m_url; }

		std::string get_http_ver()const { return m_http_ver; }

		std::string get_content_type()const { return m_content_type; }
	};
	std::vector<std::string> Request::request_set{"GET", "POST", "PATCH", "PUT", "DELETE"};

	class HTTPServer {
		static bool is_wsa_startuped;
		nysy::TCPServer m_server;
		std::vector<nysy::Connection> m_conn_vec;
		std::list<std::thread> m_thread_list;
		nysy::ConnectionStatus m_status;
		nysy::ThreadPool m_thread_pool;

		void read_file(const std::string& file_name, std::string& buffer) {
			std::fstream fs{file_name, std::ios::binary | std::ios::in };
			if (!fs.is_open()) {
				m_status = nysy::ConnectionStatus::InvalidError;
				return;
			}

			fs.seekg(0, std::ios::end);
			auto size = fs.tellg();
			fs.seekg(0, std::ios::beg);

			std::shared_ptr<char[]> sbuf = std::make_shared<char[]>(static_cast<std::size_t>(size));

			fs.read(sbuf.get(), size);
			buffer.append(sbuf.get(), size);
			m_status = nysy::ConnectionStatus::Success;
		}

	public:
		HTTPServer(unsigned short port = 8080, int backlog = 128, size_t thread_count = std::thread::hardware_concurrency())
			:m_server(), m_conn_vec(), m_thread_list(), m_thread_pool(thread_count) {
#ifdef _WIN64
			if (!is_wsa_startuped) {
				WSAData data;
				if (::WSAStartup(MAKEWORD(2, 2), &data) == WSASYSNOTREADY) {
					m_status = nysy::ConnectionStatus::SystemError;
					return;
				}
				is_wsa_startuped = true;
			}
#endif//_WIN64

			m_status = m_server.init(port);
			if (m_status == nysy::ConnectionStatus::SystemError)return;
			listen(backlog);
		}

		nysy::ConnectionStatus get_status() { return m_status; }

		void listen(int backlog = 128) {
			if (m_status == nysy::ConnectionStatus::SystemError)return;
			m_status = m_server.listen(backlog);
		}

		void serve_client(nysy::Connection connection, sockaddr_in client_addr) {
			Request request;
			std::string req_buffer;

			m_status = connection.receive_once(req_buffer);
			if (m_status == nysy::ConnectionStatus::SystemError ||
				m_status == nysy::ConnectionStatus::Logout)return;

			std::cout << req_buffer;
			request.parse(req_buffer);
			if (request.get_request_type() == "GET") {

				std::string file_buffer{};
				read_file(request.get_url(), file_buffer);
				if (m_status == nysy::ConnectionStatus::InvalidError) {
					connection.send("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n\r\n\r\n");
				}

				std::string res{std::string("HTTP/") + request.get_http_ver() +
					std::string(" 200 OK\r\nContent-Type: ") +
					request.get_content_type() +
					std::string("\r\nContent-Length: ")};

				res.append(std::to_string(file_buffer.size()));
				res.append("\r\n\r\n");
				res.append(file_buffer);
				res.append("\r\n\r\n");
				connection.send(res);
			}
			else {
				connection.send("HTTP/1.1 500 UNIMPLEMENTED\r\nContent-Length: 0\r\n\r\n\r\n\r\n");
			}
		}

		void start() {
			while (1) {
				auto [conn_stat, connection, conn_addr] = m_server.accept();
				m_status = conn_stat;
				if (m_status == nysy::ConnectionStatus::SystemError)return;

				m_thread_pool.add_task(&HTTPServer::serve_client, this, connection, conn_addr);

			}
		}
		~HTTPServer() {
#ifdef _WIN64
			if (is_wsa_startuped)WSACleanup();
#endif
		}
	};
	bool HTTPServer::is_wsa_startuped{false};
}//namespace nysy
#endif//_NYSY_HTTP_SERVER_
