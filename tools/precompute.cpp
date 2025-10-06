// small C++ precompute tool: read data/latest/<SYM>.json, compute 30m/4h aggregates and indicators,
// write to data/precomputed/<SYM>.json
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "indicators.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

int main(int argc, char** argv){
    std::vector<std::string> syms;
    if(argc>1){
        for(int i=1;i<argc;++i) syms.emplace_back(argv[i]);
    } else {
        for(auto &p: fs::directory_iterator("data/latest")){
            if(p.path().extension()==".json") syms.push_back(p.path().stem());
        }
    }
    fs::create_directories("data/precomputed");
    for(auto &sym: syms){
        std::string path = std::string("data/latest/") + sym + ".json";
        if(!fs::exists(path)) { std::cerr<<"no latest for "<<sym<<"\n"; continue; }
        std::ifstream ifs(path);
        json j; try{ ifs>>j; } catch(...){ std::cerr<<"json parse fail "<<path<<"\n"; continue; }
        std::vector<json> raw;
        if(j.contains("1m")) raw = j["1m"].get<std::vector<json>>();
        else if(j.contains("raw_1m")) raw = j["raw_1m"].get<std::vector<json>>();
        if(raw.empty()){ std::cerr<<"no raw for "<<sym<<"\n"; continue; }
        auto agg30 = indicators::aggregate_to_interval_no_fmt(raw, "30m");
        auto agg4 = indicators::aggregate_to_interval_no_fmt(raw, "4h");
        json out; out["30m"] = agg30; out["4h"] = agg4;
        out["indicators"] = json::object();
        out["indicators"]["30m"] = indicators::compute_sma(agg30, 20);
        out["indicators"]["4h"] = indicators::compute_sma(agg4, 20);
        std::string tmp = std::string("data/precomputed/") + sym + ".json.tmp";
        std::string outp = std::string("data/precomputed/") + sym + ".json";
        std::ofstream ofs(tmp); ofs<<out.dump(); ofs.close(); fs::rename(tmp, outp);
        std::cout<<"wrote precomputed "<<sym<<" 30m="<<agg30.size()<<" 4h="<<agg4.size()<<"\n";
    }
    return 0;
}
