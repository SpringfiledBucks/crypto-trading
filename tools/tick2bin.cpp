// production-capable tick -> packed binary converter
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <ctime>
#include <cstdlib>

using json = nlohmann::json;
using namespace std;

struct TickRec { int64_t ts; uint64_t tid; int64_t price; int64_t qty; uint8_t side; char pad[7]; };

static void ensure_dir(const string& path){
    struct stat st{};
    if(stat(path.c_str(), &st)!=0){
        string cmd = string("mkdir -p ") + path;
        system(cmd.c_str());
    }
}

int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string symbol;
    string outdir = "data/tick";
    bool gzip_out = false;
    int shard_by = 1; // 0=day,1=month
    size_t index_every = 10000;

    for(int i=1;i<argc;i++){
        string a = argv[i];
        if(a=="--symbol" && i+1<argc) symbol = argv[++i];
        else if(a=="--outdir" && i+1<argc) outdir = argv[++i];
        else if(a=="--gzip") gzip_out = true;
        else if(a=="--shard-day") shard_by = 0;
        else if(a=="--shard-month") shard_by = 1;
        else if(a=="--index-every" && i+1<argc) index_every = stoull(argv[++i]);
    }

    ensure_dir(outdir);
    string line;
    uint64_t written = 0;
    // map shard path -> ofstream
    unordered_map<string, ofstream*> writers;
    unordered_map<string, vector<pair<int64_t,uint64_t>>> indexes;

    auto make_path = [&](int y,int m,int d){
        ostringstream ss; ss<<outdir<<"/"<<symbol<<"/"<<y<<"-"<<setw(2)<<setfill('0')<<m; if(shard_by==0) ss<<"-"<<setw(2)<<d; ss<<".bin"; return ss.str();
    };

    // process stdin JSON lines streaming, keep files open per shard
    while(getline(cin, line)){
        if(line.empty()) continue;
        try{
            json j = json::parse(line);
            int64_t ts = 0; uint64_t tid = 0; double p=0,q=0; bool buyer_maker=false;
            if(j.contains("T")) ts = j.value("T",0);
            else if(j.contains("E")) ts = j.value("E",0);
            if(j.contains("t")) tid = j.value("t",0);
            if(j.contains("p")) p = stod(j.value("p",string("0")));
            if(j.contains("q")) q = stod(j.value("q",string("0")));
            if(j.contains("m")) buyer_maker = j.value("m",false);
            if(ts==0) continue;

            time_t sec = ts/1000;
            struct tm tm{}; gmtime_r(&sec, &tm);
            string path = make_path(tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);

            if(writers.find(path) == writers.end()){
                ensure_dir(outdir + "/" + symbol);
                auto *ofs = new ofstream(path, ios::binary | ios::app);
                if(!ofs->good()){ cerr<<"failed to open "<<path<<"\n"; delete ofs; continue; }
                writers[path] = ofs;
            }

            TickRec r{};
            r.ts = ts;
            r.tid = tid;
            r.price = (int64_t)llround(p * 1e8);
            r.qty = (int64_t)llround(q * 1e8);
            r.side = buyer_maker?1:0;
            auto ofs = writers[path];
            uint64_t offset = (uint64_t)ofs->tellp();
            ofs->write(reinterpret_cast<const char*>(&r), sizeof(r));

            if((++written % index_every)==0){ indexes[path].emplace_back(r.ts, offset); }

        } catch(const std::exception &e){ cerr<<"parse error: "<<e.what()<<"\n"; continue; }
    }

    // close writers and write index files per shard
    for(auto &kv: writers){
        kv.second->close(); delete kv.second;
        auto &idxs = indexes[kv.first];
        if(!idxs.empty()){
            string idxpath = kv.first + ".idx";
            ofstream ifs(idxpath, ios::binary);
            for(auto &p: idxs){ ifs.write(reinterpret_cast<const char*>(&p.first), sizeof(p.first)); ifs.write(reinterpret_cast<const char*>(&p.second), sizeof(p.second)); }
            ifs.close();
            cerr<<"wrote index "<<idxpath<<" entries="<<idxs.size()<<"\n";
        }
    }

    // optionally gzip the produced .bin files per symbol directory
    if(gzip_out){
        string cmd = string("find ") + outdir + "/" + symbol + " -name '*.bin' -exec gzip -f {} \;";
        system(cmd.c_str());
    }

    cerr<<"tick2bin: processed wrote_records="<<written<<"\n";
    return 0;
}
