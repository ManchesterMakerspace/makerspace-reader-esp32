#pragma once
#include "Arduino.h"
#include "BoardConfig.h"
#include "ReservationConfig.h"
extern int i2cMutex;
struct Lock { int &mutex; explicit Lock(int &m):mutex(m){assert(++mutex==1);} ~Lock(){assert(--mutex==0);} };
