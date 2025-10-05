// simple rate-limited downloader using libcurl
#include <curl/curl.h>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

using namespace std;

size_t write_file(void* ptr, size_t size, size_t nmemb, void* stream){
    FILE* f = (FILE*)stream;
    return fwrite(ptr, size, nmemb, f);
}

struct Job { string url; string fname; };

int main(int argc, char** argv){
    if(argc < 3){ cerr<<"usage: downloader <urls.txt> <outdir> [--workers N] [--delay-ms N] [--retries N]"<<"\n"; return 2; }
    string list = argv[1]; string outdir = argv[2]; int delay_ms = 200; int workers = 1; int retries = 2;
    for(int i=3;i<argc;i++){
        string a = argv[i]; if(a=="--delay-ms" && i+1<argc) delay_ms = stoi(argv[++i]);
        if(a=="--workers" && i+1<argc) workers = stoi(argv[++i]);
        if(a=="--retries" && i+1<argc) retries = stoi(argv[++i]);
    }

    // Respect global config flag to disable history downloads
    try {
        namespace fs = std::filesystem;
        fs::path cfg = "config/config.json";
        if(fs::exists(cfg)){
            std::ifstream cifs(cfg);
            if(cifs){
                nlohmann::json cj; cifs >> cj;
                if(cj.contains("disable_history_download") && cj["disable_history_download"].is_boolean() && cj["disable_history_download"].get<bool>()){ 
                    std::cerr<<"downloader: disabled via config/config.json disable_history_download=true"<<"\n";
                    return 0;
                }
            }
        }
    } catch(...) {}

    vector<string> urls; string u;
    ifstream ifs(list); while(getline(ifs,u)) if(!u.empty()) urls.push_back(u);

    queue<Job> q;
    for(size_t i=0;i<urls.size();++i){ q.push(Job{urls[i], outdir + "/" + to_string(i) + ".dat"}); }

    mutex qmt;
    unordered_map<string, chrono::steady_clock::time_point> host_next_allowed;
    mutex hostmt;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    auto worker_fn = [&](){
        CURL* c = curl_easy_init();
        while(true){
            qmt.lock(); if(q.empty()){ qmt.unlock(); break; } Job job = q.front(); q.pop(); qmt.unlock();

            // enforce per-host delay
            string host = job.url; // naive; for real use parse host
            hostmt.lock(); auto it = host_next_allowed.find(host); auto now = chrono::steady_clock::now(); if(it!=host_next_allowed.end() && it->second>now){ auto wait = it->second - now; hostmt.unlock(); this_thread::sleep_for(wait); hostmt.lock(); }
            // perform download with retries
            bool ok=false; for(int attempt=0; attempt<=retries; ++attempt){
                FILE* f = fopen(job.fname.c_str(), "wb");
                if(!f){ cerr<<"failed open "<<job.fname<<"\n"; break; }
                curl_easy_setopt(c, CURLOPT_URL, job.url.c_str());
                curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_file);
                curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
                curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
                CURLcode rc = curl_easy_perform(c);
                fclose(f);
                if(rc==CURLE_OK){ ok=true; break; }
                else { cerr<<"curl error "<<curl_easy_strerror(rc)<<" for "<<job.url<<" attempt "<<attempt<<"\n"; this_thread::sleep_for(chrono::milliseconds(200)); }
            }
            // schedule next allowed time for this host
            host_next_allowed[host] = chrono::steady_clock::now() + chrono::milliseconds(delay_ms);
            (void)ok;
        }
        curl_easy_cleanup(c);
    };

    vector<thread> th; for(int i=0;i<workers;i++) th.emplace_back(worker_fn);
    for(auto &t: th) t.join();
    curl_global_cleanup();
    return 0;
}
