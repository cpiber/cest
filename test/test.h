#pragma once
#include <assert.h>

#define LINE__(l) #l
#define LINE_(l) LINE__(l)
#define TESTC __FILE__ " L" LINE_(__LINE__)
#define TEST SV(TESTC)
