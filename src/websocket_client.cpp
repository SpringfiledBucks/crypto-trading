#include "websocket_client.h"
// websocket_client: 基于 Boost.Beast 的 TLS WebSocket 客户端实现，支持 HTTP CONNECT 代理
#include "websocket_client.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>

#include <thread>
#include <mutex>
#include "logger.h"
#include <regex>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct WebSocketClient::Impl {
    std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> ws;
    std::thread reader;
    on_msg_t on_msg;
    std::atomic<bool> connected{false};
    std::mutex mtx;
    net::io_context ioc;
    std::unique_ptr<std::thread> ioc_thread;
};

static bool parse_ws_url(const std::string &url, std::string &host, std::string &port, std::string &target) {
    // url like wss://host[:port]/path
    std::regex re(R"(^wss://([^/:]+)(?::(\d+))?(/.*)?$)");
    std::smatch m;
    if(!std::regex_match(url, m, re)) return false;
    host = m[1];
    port = m[2].matched ? m[2].str() : "443";
    target = m[3].matched ? m[3].str() : "/";
    return true;
}

WebSocketClient::WebSocketClient(): impl(new Impl()) {
    // start io_context thread
    impl->ioc_thread.reset(new std::thread([this]{ impl->ioc.run(); }));
}

WebSocketClient::~WebSocketClient() { close(); }

bool WebSocketClient::connect(const std::string &url) {
    std::string host, port, target;
        if(!parse_ws_url(url, host, port, target)) {
        Logger::error(std::string("Invalid wss URL: ") + url);
        return false;
    }

    try {
        // resolve proxy from env WS_PROXY or config not available here - read env
        const char *ws_proxy = std::getenv("WS_PROXY");
        std::string proxy_host; std::string proxy_port;
        bool use_proxy = false;
        if(ws_proxy && std::strlen(ws_proxy)>0) {
            // expect http://host:port or ws://host:port
            std::regex pre(R"(^(?:https?://)?([^/:]+)(?::(\d+))?.*$)");
            std::smatch pm;
            std::string proxy = ws_proxy;
            if(std::regex_match(proxy, pm, pre)) {
                proxy_host = pm[1];
                proxy_port = pm[2].matched ? pm[2].str() : "80";
                use_proxy = true;
            }
        }

        net::io_context &ioc = impl->ioc;
        net::ssl::context ctx(net::ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();

        // establish TCP connection: either to proxy or to host
        tcp::resolver resolver(ioc);
        tcp::socket sock(ioc);
        if(use_proxy) {
            auto const results = resolver.resolve(proxy_host, proxy_port);
            // Log resolved proxy endpoints for debugging (IPv4 vs IPv6, ordering)
            for(auto it = results.begin(); it != results.end(); ++it) {
                try {
                    auto ep = it->endpoint();
                    Logger::info(std::string("Resolved proxy endpoint: ") + ep.address().to_string() + ":" + std::to_string(ep.port()));
                } catch(...) {}
            }
            net::connect(sock, results.begin(), results.end());
            // send CONNECT
            std::string connect_req = "CONNECT " + host + ":" + port + " HTTP/1.1\r\nHost: " + host + ":" + port + "\r\nConnection: keep-alive\r\n\r\n";
            net::write(sock, net::buffer(connect_req));
            beast::flat_buffer buffer;
            http::response_parser<http::empty_body> rp;
            rp.skip(true);
            http::read_header(sock, buffer, rp);
            auto res = rp.get();
            if(res.result() != http::status::ok) {
                Logger::error(std::string("Proxy CONNECT failed: ") + std::to_string(res.result_int()));
                return false;
            }
        } else {
            auto const results = resolver.resolve(host, port);
            // Collect IPv4 then IPv6 endpoints to prefer IPv4 on systems where IPv6 isn't routable
            std::vector<tcp::endpoint> v4endpoints;
            std::vector<tcp::endpoint> v6endpoints;
            for(auto it = results.begin(); it != results.end(); ++it) {
                try {
                    auto ep = it->endpoint();
                    Logger::info(std::string("Resolved endpoint: ") + ep.address().to_string() + ":" + std::to_string(ep.port()));
                    if(ep.address().is_v4()) v4endpoints.push_back(ep);
                    else v6endpoints.push_back(ep);
                } catch(...) {}
            }
            bool connected = false;
            boost::system::error_code ec;
            // try IPv4 endpoints first
            for(auto &ep : v4endpoints) {
                try {
                    sock.connect(ep, ec);
                    if(!ec) { connected = true; break; }
                } catch(...) { }
            }
            // then try IPv6 endpoints if IPv4 failed
            if(!connected) {
                for(auto &ep : v6endpoints) {
                    try {
                        sock.connect(ep, ec);
                        if(!ec) { connected = true; break; }
                    } catch(...) { }
                }
            }
            if(!connected) {
                Logger::error(std::string("WS connect: failed to connect to any resolved endpoint for host ") + host);
                return false;
            }
        }

    // wrap socket into beast tcp_stream then ssl_stream
    beast::tcp_stream ts(std::move(sock));
    // construct ssl_stream by moving tcp_stream into it
    beast::ssl_stream<beast::tcp_stream> ssl_stream(std::move(ts), ctx);
    // perform TLS handshake
    ssl_stream.handshake(net::ssl::stream_base::client);

    // create websocket stream by moving ssl_stream
    impl->ws = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(std::move(ssl_stream));
        // Set timeout options
        impl->ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        // Set SNI Hostname
        if(!SSL_set_tlsext_host_name(impl->ws->next_layer().native_handle(), host.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            Logger::warn(std::string("SNI set failed: ") + ec.message());
        }

        // perform websocket handshake
        impl->ws->handshake(host, target);
        impl->connected = true;

        // start reader thread
        impl->reader = std::thread([this]{
            try {
                beast::flat_buffer b;
                while(impl->connected) {
                    b.clear();
                    impl->ws->read(b);
                    std::string msg = beast::buffers_to_string(b.data());
                    if(impl->on_msg) impl->on_msg(msg);
                }
            } catch(const std::exception &ex) {
                    // treat as disconnected
                    impl->connected = false;
                    Logger::error(std::string("WS reader exception: ") + ex.what());
            }
        });

        return true;
    } catch(const std::exception &ex) {
        Logger::error(std::string("WS connect exception: ") + ex.what());
        return false;
    }
}

void WebSocketClient::close() {
    try {
        impl->connected = false;
        if(impl->ws) {
            beast::error_code ec;
            impl->ws->close(websocket::close_code::normal, ec);
            (void)ec;
        }
        if(impl->reader.joinable()) impl->reader.join();
        if(impl->ioc_thread && impl->ioc_thread->joinable()) {
            impl->ioc.stop();
            impl->ioc_thread->join();
        }
    } catch(...) {}
}

bool WebSocketClient::send(const std::string &msg) {
    std::lock_guard<std::mutex> lk(impl->mtx);
    if(!impl->ws || !impl->connected) return false;
    beast::error_code ec;
    impl->ws->write(net::buffer(msg), ec);
    if(ec) {
        Logger::error(std::string("WS send error: ") + ec.message());
        return false;
    }
    return true;
}

void WebSocketClient::set_on_message(on_msg_t cb) { impl->on_msg = cb; }
