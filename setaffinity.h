#ifndef _SETAFFINITY_H_
#define _SETAFFINITY_H_

#include <thread>

int TaskSet(unsigned int nCoreID_, int nPid_, std::string &strStatus);

#endif
