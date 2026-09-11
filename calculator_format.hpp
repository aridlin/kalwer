#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
namespace kalwer::calculator {
inline std::string number(double value) {
    if(value==0)value=0; // Normalize negative zero, never round small nonzero numbers to zero.
    std::ostringstream out;out<<std::setprecision(12)<<std::defaultfloat<<value;return out.str();
}
inline std::string grouped(std::string value) {
    // Group integer components only, including fractions and mixed numbers; leave decimal/exponent digits intact.
    std::string out;
    for(size_t i=0;i<value.size();) {
        if(value[i]<'0' || value[i]>'9'){out+=value[i++];continue;}
        size_t end=i;while(end<value.size() && value[end]>='0' && value[end]<='9')++end;
        bool group=i==0 || (value[i-1]!='.' && value[i-1]!='e' && value[i-1]!='E' && !(i>=2 && (value[i-1]=='+' || value[i-1]=='-') && (value[i-2]=='e' || value[i-2]=='E')));
        for(size_t j=i;j<end;++j){if(group && j>i && (end-j)%3==0)out+='\'';out+=value[j];}i=end;
    }return out;
}
inline std::string scientific(double value) {
    if(!std::isfinite(value) || value==0 || (std::abs(value)<1e12 && std::abs(value)>=1e-6))return {};
    std::ostringstream out;out<<std::scientific<<std::setprecision(11)<<value;
    auto s=out.str();auto e=s.find('e');auto mantissa=s.substr(0,e);while(mantissa.back()=='0')mantissa.pop_back();if(mantissa.back()=='.')mantissa.pop_back();
    return mantissa+"e"+std::to_string(std::stoi(s.substr(e+1)));
}
}
