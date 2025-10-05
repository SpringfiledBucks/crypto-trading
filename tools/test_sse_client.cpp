// Simple blocking SSE test client for /events (moved to tools/)
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <fcntl.h>

static void usage(){
    std::cerr<<"test_sse_client [--host HOST] [--port PORT] [--log PATH] [--delay S] [--timeout S]"<<std::endl;
    std::cerr<<"  --host    SSE host (default 127.0.0.1)"<<std::endl;
    std::cerr<<"  --port    SSE port (default 8080)"<<std::endl;
    std::cerr<<"  --log     absolute path to runtime.log (default /home/crypto/crypto-trading/logs/runtime.log)"<<std::endl;
    std::cerr<<"  --delay   seconds to wait before injecting log (default 2)"<<std::endl;
    std::cerr<<"  --timeout seconds to wait for event (default 20)"<<std::endl;
}

int main(int argc, char** argv){
    std::string host = "127.0.0.1";
    std::string port = "8080";
    std::string logpath = "/home/crypto/crypto-trading/logs/runtime.log";
    int delay = 2;
    int timeout_s = 20;
    for(int i=1;i<argc;i++){
        std::string a = argv[i];
        if(a=="--host" && i+1<argc) host = argv[++i];
        else if(a=="--port" && i+1<argc) port = argv[++i];
        else if(a=="--log" && i+1<argc) logpath = argv[++i];
        else if(a=="--delay" && i+1<argc) delay = std::stoi(argv[++i]);
        else if(a=="--timeout" && i+1<argc) timeout_s = std::stoi(argv[++i]);
        else if(a=="-h" || a=="--help") { usage(); return 0; }
        else { std::cerr<<"Unknown arg: "<<a<<std::endl; usage(); return 2; }
    }
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host.c_str(), port.c_str(), &hints, &res)!=0){ std::cerr<<"getaddrinfo failed"<<std::endl; return 2; }
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(s<0){ perror("socket"); return 2; }
    if(connect(s, res->ai_addr, res->ai_addrlen)!=0){ perror("connect"); close(s); return 3; }
    freeaddrinfo(res);
    std::string req = "GET /events HTTP/1.1\r\nHost: " + host + ":" + port + "\r\nAccept: text/event-stream\r\nConnection: keep-alive\r\nUser-Agent: test_sse_client\r\n\r\n";
    ssize_t n = write(s, req.c_str(), req.size());
    if(n<=0){ perror("write"); close(s); return 4; }
    std::cout<<"sent request, reading headers..."<<std::endl;
    // read headers
    std::string headers;
    char c;
    while(read(s, &c, 1)==1){ headers.push_back(c); if(headers.size()>4 && headers.substr(headers.size()-4)=="\r\n\r\n") break; }
    std::cout<<"headers:\n"<<headers<<std::endl;

    // spawn a thread to append a STATE_JSON into logs/runtime.log after 2s
    std::thread t([logpath,delay](){ std::this_thread::sleep_for(std::chrono::seconds(delay));
        time_t ts = time(nullptr);
        char timestr[64];
        strftime(timestr, sizeof(timestr), "%F %T", localtime(&ts));
        std::string line = std::string("[") + timestr + "] [INFO] STATE_JSON {\"ts\": " + std::to_string(ts) + ", \"status\": \"manual-test\", \"note\": \"inject\"}\n";
        int fd = open(logpath.c_str(), O_WRONLY|O_APPEND|O_CREAT, 0644);
        if(fd >= 0){
            ssize_t w = write(fd, line.c_str(), line.size());
            if(w < 0) perror("write");
            else std::cout << "wrote bytes=" << w << " content='" << (line.size()>120? line.substr(0,120)+"...": line) << "'\n";
            fsync(fd);
            close(fd);
            std::cout<<"appended state to "<< logpath <<" (posix)"<<std::endl;
        } else {
            perror("open logs/runtime.log");
        }
    });

    // read loop up to 20s
    fd_set rfds;
    struct timeval tv;
    auto start = std::chrono::steady_clock::now();
    bool received=false;
    while(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count() < timeout_s){
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        tv.tv_sec = 1; tv.tv_usec = 0;
        int r = select(s+1, &rfds, nullptr, nullptr, &tv);
        if(r>0 && FD_ISSET(s, &rfds)){
            char buf[4096]; ssize_t m = read(s, buf, sizeof(buf));
            if(m<=0){ std::cout<<"socket closed by server"<<std::endl; break; }
            std::string chunk(buf, buf+m);
            std::cout<<"recv chunk:\n"<<chunk<<std::endl;
            if(chunk.find("manual-test")!=std::string::npos){ received=true; break; }
        }
    }
    close(s);
    if(t.joinable()) t.join();
    std::cout<<"done, received="<<received<<std::endl;
    return received?0:1;
}
