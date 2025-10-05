// aggregate packed ticks (or stdin CSV) into 1m OHLCV binary and write gzipped monthly files
#include <zlib.h>
#include <bits/stdc++.h>
using namespace std;

struct OHLCV { int64_t ts; int64_t open; int64_t high; int64_t low; int64_t close; int64_t vol; };
struct TickRec { int64_t ts; uint64_t tid; int64_t price; int64_t qty; uint8_t side; char pad[7]; };

static string month_path(const string &outdir, const string &symbol, int y, int m){
    ostringstream ss; ss<<outdir<<"/"<<symbol<<"/"<<y<<"-"<<setw(2)<<setfill('0')<<m<<".bin.gz"; return ss.str();
}

int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string outdir = "data/1m";
    string symbol = "SYM";
    if(argc>1) symbol = argv[1];
    if(argc>2) outdir = argv[2];

    map<int64_t, OHLCV> bucket;

    // auto-detect input: if stdin is binary of tick records, read in 40-byte chunks, else parse CSV lines
    istream &in = cin;
    // try to read first bytes non-destructively
    in >> std::noskipws;
    char c;
    vector<char> buffer;
    while(in.get(c)) buffer.push_back(c);
    // attempt to parse as binary ticks if size % sizeof(TickRec)==0
    if(!buffer.empty() && (buffer.size() % sizeof(TickRec) == 0)){
        size_t n = buffer.size()/sizeof(TickRec);
        for(size_t i=0;i<n;i++){
            TickRec t;
            memcpy(&t, buffer.data()+i*sizeof(TickRec), sizeof(TickRec));
            int64_t minute = (t.ts/60000)*60000;
            auto &o = bucket[minute];
            if(o.ts==0){ o.ts = minute; o.open = t.price; o.high = t.price; o.low = t.price; o.close = t.price; o.vol = t.qty; }
            else { o.high = max(o.high, t.price); o.low = min(o.low, t.price); o.close = t.price; o.vol += t.qty; }
        }
    } else {
        // treat buffer as lines
        string s(buffer.begin(), buffer.end());
        stringstream ss(s);
        string line;
        while(getline(ss,line)){
            if(line.empty()) continue;
            stringstream ls(line);
            int64_t ts; unsigned long tid; double p,q; string side;
            if(!(ls>>ts>>tid>>p>>q>>side)) continue;
            int64_t minute = (ts/60000)*60000;
            int64_t price = (int64_t)llround(p*1e8);
            int64_t qty = (int64_t)llround(q*1e8);
            auto &o = bucket[minute];
            if(o.ts==0){ o.ts = minute; o.open = price; o.high = price; o.low = price; o.close = price; o.vol = qty; }
            else { o.high = max(o.high, price); o.low = min(o.low, price); o.close = price; o.vol += qty; }
        }
    }

    // write per-month gz files
    map<pair<int,int>, vector<OHLCV>> groups;
    for(auto &kv: bucket){
        int64_t minute = kv.first; time_t sec = minute/1000;
        struct tm tm; gmtime_r(&sec, &tm);
        groups[{tm.tm_year+1900, tm.tm_mon+1}].push_back(kv.second);
    }

    for(auto &g: groups){
        int y = g.first.first; int m = g.first.second;
        string path = month_path(outdir, symbol, y, m);
        // ensure dir
        string dpath = outdir + "/" + symbol;
        string cmd = string("mkdir -p ") + dpath;
        system(cmd.c_str());
        gzFile gz = gzopen(path.c_str(), "wb");
        if(!gz) { cerr<<"failed to open "<<path<<"\n"; continue; }
        for(auto &o: g.second){ gzwrite(gz, reinterpret_cast<const char*>(&o), sizeof(o)); }
        gzclose(gz);
        cerr<<"wrote "<<path<<" entries="<<g.second.size()<<"\n";
    }

    return 0;
}
