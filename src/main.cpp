#include "ieim_model.hpp"
#include <iostream>
#include <locale>
#include <tuple>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
std::string executable_directory(const char* argv0) {
    std::string path;
#ifdef _WIN32
    (void)argv0;
    std::vector<char> buffer(32768);
    const DWORD n=GetModuleFileNameA(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!n || n>=buffer.size()) throw std::runtime_error("Cannot locate executable");
    path.assign(buffer.data(),n);
#else
    std::vector<char> buffer(32768);
    const auto n=readlink("/proc/self/exe",buffer.data(),buffer.size());
    path=n>0 && static_cast<std::size_t>(n)<buffer.size() ? std::string(buffer.data(),static_cast<std::size_t>(n)) : std::string(argv0);
#endif
    const auto slash=path.find_last_of("/\\");
    return slash==std::string::npos ? "." : path.substr(0,slash);
}
void help() {
    std::cout<<"IEIM point VTEC (TECU), selected UT or all 25 hourly samples (0..24)\n"
      <<"Usage: reconstruct YEAR DOY ICI LONGITUDE LATITUDE [UT] [options]\n"
      <<"   or: reconstruct --year Y --doy D --icivalue I --lon LON --lat LAT [--ut HOUR] [options]\n"
      <<"   or: reconstruct --input FILE [options]\n"
      <<"Options: --output FILE (default stdout), --data-dir DIR, --modes 1..18 (default 18)\n"
      <<"Input rows: Year DOY ICI Longitude Latitude [UT]; UT is 0..24 decimal hours.\n"
      <<"Longitude: -180..180; latitude: -87.5..87.5; east/north positive. Output: TECU.\n"
      <<"UT24 uses next-day ICI at the same input point, if present; otherwise today's ICI.\n";
}
struct Input {ieim::Request point; std::optional<double> ut;};
Input request(const std::vector<std::string>& f) {
    if(f.size()!=5 && f.size()!=6) throw std::runtime_error("Expected Year DOY ICI Longitude Latitude [UT]");
    ieim::Request r{ieim::integer(f[0]),ieim::integer(f[1]),ieim::number(f[2]),ieim::number(f[3]),ieim::number(f[4])};
    ieim::validate(r);
    std::optional<double> ut; if(f.size()==6) {ut=ieim::number(f[5]);ieim::validate_ut(*ut);}
    return {r,ut};
}
std::vector<Input> read_requests(const std::string& path) {
    std::ifstream in(path);if(!in) throw std::runtime_error("Cannot open input: "+path);
    std::vector<Input> result;std::string line;int n=0;
    while(std::getline(in,line)) {
        ++n;if(n==1) ieim::strip_bom(line);
        line=ieim::trim(line.substr(0,line.find_first_of("#%")));
        if(line.empty())continue;
        std::istringstream row(line);std::vector<std::string> fields;std::string field;
        while(row>>field) fields.push_back(field);
        if(fields[0]=="Year" || fields[0]=="YEAR")continue;
        try {result.push_back(request(fields));}
        catch(const std::exception& e) {throw std::runtime_error(path+":"+std::to_string(n)+": "+e.what());}
    }
    if(in.bad() || result.empty()) throw std::runtime_error("No valid input records or input read failure: "+path);
    return result;
}
using Key=std::tuple<int,int,double,double>;
Key key(const ieim::Request& r) {return {r.year,r.doy,r.longitude,r.latitude};}
}
int main(int argc,char** argv) {
    try {
        std::locale::global(std::locale::classic());
        for(int i=1;i<argc;++i) if(std::string(argv[i])=="--help" || std::string(argv[i])=="-h") {help();return 0;}
        std::string data=executable_directory(argv[0])+"/data",input,output;
        std::vector<std::string> positional;std::map<std::string,std::string> named;int modes=18;
        for(int i=1;i<argc;++i) {
            const std::string flag=argv[i];
            if(flag.rfind("--",0)!=0) {positional.push_back(flag);continue;}
            if(flag!="--year" && flag!="--doy" && flag!="--icivalue" && flag!="--lon" && flag!="--lat" &&
               flag!="--ut" && flag!="--input" && flag!="--output" && flag!="--data-dir" && flag!="--modes")
                throw std::runtime_error("Unknown option: "+flag);
            if(i+1>=argc)throw std::runtime_error("Missing value for "+flag);
            if(!named.emplace(flag,argv[++i]).second) throw std::runtime_error("Duplicate option: "+flag);
        }
        if(named.count("--data-dir"))data=named.at("--data-dir");
        if(named.count("--input"))input=named.at("--input");
        if(named.count("--output"))output=named.at("--output");
        if(named.count("--modes"))modes=ieim::integer(named.at("--modes"));
        std::vector<Input> requests;
        const std::array<std::string,5> fields{"--year","--doy","--icivalue","--lon","--lat"};
        bool has_named=false;for(const auto& f:fields)has_named=has_named || named.count(f);
        if(!input.empty()) {
            if(!positional.empty() || has_named || named.count("--ut"))throw std::runtime_error("--input cannot be combined with point arguments");
            requests=read_requests(input);
        } else if(!positional.empty()) {
            if(has_named)throw std::runtime_error("Use positional or named point arguments, not both");
            if(named.count("--ut")) {
                if(positional.size()!=5)throw std::runtime_error("UT specified twice or invalid point fields");
                positional.push_back(named.at("--ut"));
            }
            requests.push_back(request(positional));
        } else {
            std::vector<std::string> f;
            for(const auto& name:fields) {
                if(!named.count(name))throw std::runtime_error("Missing "+name+"; use --help");
                f.push_back(named.at(name));
            }
            if(named.count("--ut")) f.push_back(named.at("--ut"));
            requests.push_back(request(f));
        }
        std::map<Key,double> ici_by_point;
        for(const auto& row:requests) {
            const auto& r=row.point; const auto found=ici_by_point.emplace(key(r),r.ici);
            if(!found.second && found.first->second!=r.ici)
                throw std::runtime_error("Conflicting ICI for the same date and coordinates");
        }
        const ieim::Model model(data,modes);
        // Calculate the complete result before opening any destination file.
        std::ostringstream result;result.imbue(std::locale::classic());result<<std::setprecision(17);
        result<<"Year\tDOY\tHour_UT\tICI\tLongitude\tLatitude\tVTEC_TECU\n";
        for(const auto& row:requests) {
            const auto& r=row.point;
            const auto next=ieim::next_date(r.year,r.doy);
            const auto it=ici_by_point.find({next.first,next.second,r.longitude,r.latitude});
            std::optional<double> next_ici;if(it!=ici_by_point.end())next_ici=it->second;
            auto write=[&](double ut,double tecu) {
                result<<r.year<<'\t'<<r.doy<<'\t'<<ut<<'\t'<<r.ici<<'\t'<<r.longitude<<'\t'<<r.latitude<<'\t'<<std::fixed<<std::setprecision(2)<<tecu
                      <<std::defaultfloat<<std::setprecision(17)<<'\n';
            };
            if(row.ut) write(*row.ut,model.predict_at(r,*row.ut,next_ici));
            else {
                const auto values=model.predict(r,next_ici);
                for(int h=0;h<ieim::hour_count;++h)write(h,values[h]);
            }
        }
        if(output.empty()) {std::cout<<result.str();if(!std::cout)throw std::runtime_error("Failed writing stdout");}
        else {
            // Never replace an existing input or model data file with a result.
            std::ifstream existing(output);
            if(existing.good()) throw std::runtime_error("Output already exists; choose a new file: "+output);
            std::ofstream out(output);if(!out)throw std::runtime_error("Cannot create output: "+output);
            out<<result.str();out.close();if(!out)throw std::runtime_error("Failed writing output: "+output);
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Error: "<<e.what()<<'\n';return 1;}
}
