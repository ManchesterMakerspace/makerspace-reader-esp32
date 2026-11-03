#pragma once
#include <string>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cassert>
#include <type_traits>
class String:public std::string {
public:
 using std::string::string;
 String(const std::string &s):std::string(s) {}
 template<class T,typename std::enable_if<std::is_integral<T>::value,int>::type=0>
 explicit String(T v):std::string(std::to_string(v)) {}
};
struct SerialPort {
 std::string text;
 void println(const String &s){text+=s; text+='\n';}
 void printf(const char *format,...){char buffer[256];va_list args;va_start(args,format);vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);text+=buffer;}
};
extern SerialPort Serial;
struct Chip { unsigned getFlashChipSize(); unsigned getPsramSize(); };
extern Chip ESP;
constexpr int OUTPUT=1, LOW=0, HIGH=1;
extern unsigned pinWrites,waitMs;
void pinMode(int,int);
void digitalWrite(int,int);
inline void delay(unsigned ms){waitMs+=ms;}
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) assert(++*(p)==1)
#define portEXIT_CRITICAL(p) assert(--*(p)==0)
