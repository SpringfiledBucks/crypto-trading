// proxy_traffic_monitor.cpp
// Simple TCP proxy that forwards connections and records per-listener-port traffic stats.
// Usage: proxy_traffic_monitor --listen PORT --upstream HOST:PORT [--listen PORT2 --upstream HOST2:PORT2 ...] --log logs/proxy_traffic.log

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <csignal>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;
using namespace std::chrono_literals;

struct Stats { std::atomic<uint64_t> rx{0}; std::atomic<uint64_t> tx{0}; };

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket client, boost::asio::io_context& ioc, const tcp::endpoint& upstream_ep, Stats& stats)
        : client_(std::move(client)), upstream_sock_(ioc), resolver_(ioc), upstream_ep_(upstream_ep), stats_(stats) {}

    void start(){
        auto self = shared_from_this();
        upstream_sock_.async_connect(upstream_ep_, [this, self](const boost::system::error_code &ec){
            if(ec){ client_.close(); return; }
            do_read_client(); do_read_upstream();
        });
    }

private:
    void do_read_client(){
        auto self = shared_from_this();
        client_.async_read_some(boost::asio::buffer(cbuf_), [this,self](boost::system::error_code ec, std::size_t n){
            if(ec){ close(); return; }
            // forward to upstream
            stats_.rx += n;
            async_write(upstream_sock_, boost::asio::buffer(cbuf_, n), [this,self](boost::system::error_code ec2, std::size_t){ if(ec2) close(); });
            do_read_client();
        });
    }
    void do_read_upstream(){
        auto self = shared_from_this();
        upstream_sock_.async_read_some(boost::asio::buffer(sbuf_), [this,self](boost::system::error_code ec, std::size_t n){
            if(ec){ close(); return; }
            stats_.tx += n;
            async_write(client_, boost::asio::buffer(sbuf_, n), [this,self](boost::system::error_code ec2, std::size_t){ if(ec2) close(); });
            do_read_upstream();
        });
    }
    void close(){ boost::system::error_code ec; client_.close(ec); upstream_sock_.close(ec); }

    tcp::socket client_;
    tcp::socket upstream_sock_;
    tcp::resolver resolver_;
    tcp::endpoint upstream_ep_;
    Stats& stats_;
    enum { BUFSZ = 8192 };
    char cbuf_[BUFSZ];
    char sbuf_[BUFSZ];
};

int main(int argc, char** argv){
    if(argc<5){ std::cerr<<"usage: proxy_traffic_monitor --listen LP --upstream HOST:PORT [--listen LP2 --upstream H2:P2 ...] --log logfile\n"; return 2; }
    std::vector<std::pair<unsigned short,std::string>> mappings;
    std::string logpath = "logs/proxy_traffic.log";
    for(int i=1;i<argc;i++){
        std::string a = argv[i];
        if(a=="--listen" && i+1<argc){ unsigned short p = (unsigned short)std::stoi(argv[++i]); mappings.emplace_back(p, std::string()); }
        else if(a=="--upstream" && i+1<argc){ if(mappings.empty()){ std::cerr<<"--upstream without --listen\n"; return 2;} mappings.back().second = argv[++i]; }
        else if(a=="--log" && i+1<argc) logpath = argv[++i];
    }
    if(mappings.empty()){ std::cerr<<"no mappings\n"; return 2; }

    boost::asio::io_context ioc{1};
    std::map<unsigned short, std::shared_ptr<Stats>> stats_map;
    std::vector<std::shared_ptr<tcp::acceptor>> acceptors;

    for(auto &m: mappings){ unsigned short lp = m.first; std::string up = m.second; auto pos = up.find(':'); if(pos==std::string::npos){ std::cerr<<"bad upstream "<<up<<"\n"; return 2; }
        std::string host = up.substr(0,pos); unsigned short upport = (unsigned short)std::stoi(up.substr(pos+1));
        tcp::resolver r(ioc); auto eps = *r.resolve(host, std::to_string(upport)); tcp::endpoint upstream_ep = eps.endpoint();
        auto acceptor = std::make_shared<tcp::acceptor>(ioc, tcp::endpoint(tcp::v4(), lp));
        stats_map[lp] = std::make_shared<Stats>();
        acceptors.push_back(acceptor);
        // start accept loop
        std::function<void()> do_accept = [&,acceptor,upstream_ep,lp]() mutable {
            auto sock = std::make_shared<tcp::socket>(ioc);
            acceptor->async_accept(*sock, [&,sock,upstream_ep,lp](boost::system::error_code ec){
                if(!ec){ auto s = std::make_shared<Session>(std::move(*sock), ioc, upstream_ep, *stats_map[lp]); s->start(); }
                do_accept();
            });
        };
        do_accept();
    }

    // periodic logger
    boost::asio::steady_timer t(ioc, std::chrono::seconds(5));
    std::mutex logmt;
    std::function<void(const boost::system::error_code&)> tick;
    tick = [&](const boost::system::error_code&){
        std::ofstream ofs(logpath, std::ios::app);
        ofs<<"["<<std::time(nullptr)<<"]\n";
        for(auto &p: stats_map){ ofs<<"port="<<p.first<<" rx="<<p.second->rx.load()<<" tx="<<p.second->tx.load()<<"\n"; }
        ofs.close();
        t.expires_after(std::chrono::seconds(5)); t.async_wait(tick);
    };
    t.async_wait(tick);

    // handle signals to exit cleanly
    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ ioc.stop(); });

    std::thread thr([&]{ ioc.run(); });
    thr.join();
    return 0;
}
