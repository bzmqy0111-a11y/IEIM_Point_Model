#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ieim {
constexpr int latitude_count = 71, longitude_count = 73, hour_count = 25;
constexpr double pi = 3.14159265358979323846;
struct Request { int year, doy; double ici, longitude, latitude; };
inline bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
inline void validate(const Request& r) {
    if (r.year < 1000 || r.year > 9999 || r.doy < 1 || r.doy > (leap(r.year) ? 366 : 365))
        throw std::runtime_error("Invalid Year/DOY");
    if (!std::isfinite(r.ici) || r.ici < 0) throw std::runtime_error("ICI must be finite and nonnegative");
    if (!std::isfinite(r.longitude) || r.longitude < -180 || r.longitude > 180)
        throw std::runtime_error("Longitude must be -180..180 degrees");
    if (!std::isfinite(r.latitude) || r.latitude < -87.5 || r.latitude > 87.5)
        throw std::runtime_error("Latitude must be -87.5..87.5 degrees (model grid coverage)");
}
inline void validate_ut(double ut) {
    if (!std::isfinite(ut) || ut<0 || ut>24) throw std::runtime_error("UT must be 0..24 decimal hours");
}
inline std::pair<int,int> next_date(int year, int doy) {
    if (++doy > (leap(year) ? 366 : 365)) { ++year; doy = 1; }
    return {year,doy};
}
inline std::string trim(const std::string& s) {
    const auto a=s.find_first_not_of(" \t\r\n");
    return a==std::string::npos ? "" : s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
inline void strip_bom(std::string& s) { if (s.rfind("\xEF\xBB\xBF",0)==0) s.erase(0,3); }
inline double number(const std::string& s) {
    std::size_t used=0; const double x=std::stod(s,&used);
    if (used!=s.size() || !std::isfinite(x)) throw std::runtime_error("Expected finite number: "+s);
    return x;
}
inline int integer(const std::string& s) {
    std::size_t used=0; const int x=std::stoi(s,&used);
    if (used!=s.size()) throw std::runtime_error("Expected integer: "+s);
    return x;
}
inline std::string data_path(const std::string& directory, const std::string& stem, int mode) {
    std::ostringstream s; s<<directory<<'/'<<stem<<'_'<<std::setfill('0')<<std::setw(2)<<mode<<".txt";
    return s.str();
}
namespace detail {
class TimeModel {
    int mode_;
    double mean_, std_;
    std::vector<double> days_, hours_, beta_, scale_;
public:
    TimeModel(int mode, const std::string& path):mode_(mode) {
        if (mode==2 || mode==6 || mode==8 || mode==9) {
            days_={365.25,121.75}; hours_={24,12};
        } else if(mode==5) {
            days_={365.25,182.625}; hours_={24,12,8,6};
        } else if(mode==7) {
            days_={365.25,182.625,121.75}; hours_={24,12};
        } else if(mode==11 || mode==13) {
            days_={365.25,182.625,121.75}; hours_={24,12,8};
        } else if(mode==15) {
            days_={365.25,182.625,121.75}; hours_={24,12,6};
        } else if(mode==16) {
            days_={182.625}; hours_={24,12,8};
        } else {
            days_={365.25,182.625}; hours_={24,12,8};
        }
        std::ifstream in(path); if(!in) throw std::runtime_error("Cannot open "+path);
        std::map<std::string,std::string> parameters;
        std::string line; bool first=true;
        while(std::getline(in,line)) {
            if(first) {strip_bom(line);first=false;}
            line=trim(line);
            if(line.empty() || line[0]=='#' || line[0]=='%') continue;
            const auto eq=line.find('=');
            if(eq==std::string::npos || !parameters.emplace(trim(line.substr(0,eq)),trim(line.substr(eq+1))).second)
                throw std::runtime_error("Invalid or duplicate parameter in "+path);
        }
        if(in.bad()) throw std::runtime_error("Failed reading "+path);
        auto value=[&](const std::string& key) -> const std::string& {
            const auto it=parameters.find(key);
            if(it==parameters.end()) throw std::runtime_error("Missing "+key+" in "+path);
            return it->second;
        };
        if(integer(value("MODE"))!=mode) throw std::runtime_error("Coefficient MODE mismatch in "+path);
        mean_=number(value("ICI_MEAN")); std_=number(value("ICI_STD"));
        if(std_<=0) throw std::runtime_error("ICI_STD must be positive");
        const int count=3*(1+2*static_cast<int>(days_.size()))*(1+2*static_cast<int>(hours_.size()));
        int nb=0,ns=0;
        for(const auto& kv:parameters) {nb+=kv.first.rfind("BETA_",0)==0;ns+=kv.first.rfind("SCALE_",0)==0;}
        if(nb!=count || ns!=count) throw std::runtime_error("Coefficient count mismatch in "+path);
        for(int i=0;i<count;++i) {
            beta_.push_back(number(value("BETA_"+std::to_string(i))));
            scale_.push_back(number(value("SCALE_"+std::to_string(i))));
            if(scale_.back()<=0) throw std::runtime_error("SCALE must be positive");
        }
    }
    double predict(int doy,int hour,double ici) const {
        std::vector<double> season{1},lt{1};
        const double t=doy+hour/24.0;
        for(double period:days_) {const double a=2*pi*t/period;season.push_back(std::cos(a));season.push_back(std::sin(a));}
        for(double period:hours_) {const double a=2*pi*hour/period;lt.push_back(std::cos(a));lt.push_back(std::sin(a));}
        const double z=(ici-mean_)/std_;
        double value=0,zpow=1; int col=0;
        for(int q=0;q<=2;++q) {
            if(q>0) zpow*=z;
            for(double s:season) for(double l:lt) {
                const double feature=zpow*s*l;
                value+=beta_[col]*feature/scale_[col]; ++col;
            }
        }
        if(!std::isfinite(value)) throw std::runtime_error("Nonfinite predicted TC");
        return mode_==1 ? std::exp(std::max(-700.0,std::min(700.0,value))) : value;
    }
};
inline std::vector<double> read_grid(const std::string& path) {
    std::ifstream in(path); if(!in) throw std::runtime_error("Cannot open "+path);
    std::vector<double> grid; std::string line; bool first=true;
    while(std::getline(in,line)) {
        if(first) {strip_bom(line);first=false;}
        line=trim(line); if(line.empty() || line[0]=='#') continue;
        std::istringstream row(line);double lat,lon,value;std::string extra;
        if(!(row>>lat>>lon>>value) || (row>>extra)) throw std::runtime_error("Bad grid row in "+path);
        const auto index=grid.size();
        if(index>=latitude_count*longitude_count || !std::isfinite(value) || !std::isfinite(lat) || !std::isfinite(lon) ||
           std::abs(lat-(87.5-2.5*(index%latitude_count)))>1e-7 ||
           std::abs(lon-(-180.0+5.0*(index/latitude_count)))>1e-7)
            throw std::runtime_error("Grid coordinates or value invalid in "+path);
        grid.push_back(value);
    }
    if(in.bad() || grid.size()!=latitude_count*longitude_count) throw std::runtime_error("Grid must have 5183 points: "+path);
    return grid;
}
} // namespace detail

class Model {
    using Curve=std::vector<std::array<double,hour_count>>;
    std::vector<detail::TimeModel> models_;
    std::vector<std::vector<double>> spatial_;
    Curve curve(int doy,double ici) const {
        Curve result(models_.size());
        for(std::size_t m=0;m<models_.size();++m) for(int h=0;h<hour_count;++h) result[m][h]=models_[m].predict(doy,h,ici);
        return result;
    }
    double node(const Curve& tc,double ut,int lon_index,int lat_index) const {
        const double lon=-180.0+5.0*lon_index;
        double lt=std::fmod(ut+lon/15.0,24.0); if(lt<0) lt+=24;
        const int lower=static_cast<int>(std::floor(lt));const double w=lt-lower;
        double tecu=0;
        for(std::size_t m=0;m<models_.size();++m) {
            const double point=spatial_[m][lon_index*latitude_count+lat_index];
            tecu+=(1-w)*point*tc[m][lower]+w*point*tc[m][lower+1];
        }
        if(!std::isfinite(tecu)) throw std::runtime_error("Nonfinite reconstructed VTEC");
        return std::max(0.0,tecu);
    }
    double interpolate(const Request& r,const Curve& c,double ut) const {
        const double x=(r.longitude+180)/5,y=(87.5-r.latitude)/2.5;
        const int left=std::min(static_cast<int>(std::floor(x)),longitude_count-2);
        const int top=std::min(static_cast<int>(std::floor(y)),latitude_count-2);
        const double wx=x-left,wy=y-top;
        const double north=(1-wx)*node(c,ut,left,top)+wx*node(c,ut,left+1,top);
        const double south=(1-wx)*node(c,ut,left,top+1)+wx*node(c,ut,left+1,top+1);
        const double value=(1-wy)*north+wy*south;
        if(!std::isfinite(value)) throw std::runtime_error("Nonfinite interpolated VTEC");
        return value;
    }
public:
    explicit Model(const std::string& data_directory,int modes=18) {
        if(modes<1 || modes>18) throw std::runtime_error("Modes must be 1..18");
        for(int m=1;m<=modes;++m) {
            models_.emplace_back(m,data_path(data_directory,"coeff",m));
            spatial_.push_back(detail::read_grid(data_path(data_directory,"spatial",m)));
        }
    }
    // Evaluate one decimal UT hour. UT24 is next-day UT00.
    double predict_at(const Request& r,double ut,std::optional<double> next_ici=std::nullopt) const {
        validate(r); validate_ut(ut);
        if(next_ici && (!std::isfinite(*next_ici) || *next_ici<0)) throw std::runtime_error("Invalid next-day ICI");
        const Curve c=ut==24 ? curve(next_date(r.year,r.doy).second,next_ici.value_or(r.ici)) : curve(r.doy,r.ici);
        return interpolate(r,c,ut==24 ? 0 : ut);
    }
    // All 25 integer UT samples; share each day's precomputed LT coefficients.
    std::array<double,hour_count> predict(const Request& r,std::optional<double> next_ici=std::nullopt) const {
        validate(r);
        if(next_ici && (!std::isfinite(*next_ici) || *next_ici<0)) throw std::runtime_error("Invalid next-day ICI");
        const Curve today=curve(r.doy,r.ici);
        const Curve tomorrow=curve(next_date(r.year,r.doy).second,next_ici.value_or(r.ici));
        std::array<double,hour_count> out{};
        for(int h=0;h<hour_count;++h) out[h]=interpolate(r,h==24 ? tomorrow : today,h==24 ? 0 : h);
        return out;
    }
};
} // namespace ieim
