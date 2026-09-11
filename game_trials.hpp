#pragma once
#include "appearance.hpp"
#include <cmath>
#include <ctime>
#include <functional>
#include <sstream>
namespace kalwer {
struct GameTrials {
    static constexpr double allowance=600;
    std::filesystem::path path;
    std::array<double,4> used{};
    int day=0;
    double pending=0;
    bool dirty=false,save_failed=false;
    std::function<int()> today=[] {
        auto now=std::time(nullptr);std::tm local{};
#ifdef _WIN32
        localtime_s(&local,&now);
#else
        localtime_r(&now,&local);
#endif
        return (local.tm_year+1900)*10000+(local.tm_mon+1)*100+local.tm_mday;
    };
    bool flush(){
        if(!dirty)return !save_failed;
        std::ostringstream out;out.precision(12);out<<"1 "<<day;
        for(double value:used)out<<' '<<value;
        out<<'\n';
        if(path.empty() || !atomic_text(path,out.str())){save_failed=true;return false;}
        dirty=save_failed=false;pending=0;return true;
    }
    void refresh(){
        auto desired=wallet.path.empty()?std::filesystem::path{}:wallet.path.parent_path()/"game-trials-v1";
        if(path!=desired){
            flush();path=desired;used.fill(0);day=0;pending=0;dirty=save_failed=false;
            std::ifstream in(path);int version=0,stored_day=0;std::array<double,4> values{};
            if(in>>version>>stored_day && version==1 && stored_day>0){
                bool valid=true;for(auto& value:values)if(!(in>>value) || !std::isfinite(value) || value<0 || value>allowance)valid=false;
                if(valid){day=stored_day;used=values;}
            }
        }
        int date=today();
        // Moving the clock backwards must not refill an already used day.
        if(date>day){day=date;used.fill(0);pending=0;dirty=true;}
    }
    double remaining(int id){refresh();return id>=0 && id<4?std::max(0.,allowance-used[id]):0;}
    bool begin(int id){return remaining(id)>0 && flush();}
    double consume(int id,double dt){
        refresh();if(id<0 || id>=4 || dt<=0 || !std::isfinite(dt))return 0;
        if(save_failed && !flush())return 0;
        double amount=std::min(remaining(id),dt);used[id]+=amount;pending+=amount;dirty|=amount>0;
        if(pending>=5 || used[id]>=allowance)flush();
        return amount;
    }
    std::string label(int id){int seconds=int(std::ceil(remaining(id)));return std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);}
};
inline GameTrials game_trials;
}
