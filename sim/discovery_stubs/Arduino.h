#pragma once
#include <initializer_list>
struct SerialMock {template<class... T>void printf(const char*,T...){} void println(const char*){}};
extern SerialMock Serial;
