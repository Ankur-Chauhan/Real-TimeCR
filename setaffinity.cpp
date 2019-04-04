#include "setaffinity.h"

int TaskSet(unsigned int nCoreID_, int nPid_, std::string &strStatus)
{
  int nRet = 0;
  char strErrMsg[768] = {0};
  unsigned int nThreads = std::thread::hardware_concurrency();
  cpu_set_t set;
  CPU_ZERO(&set);

  if(nCoreID_ >= nThreads)
  {
    sprintf(strErrMsg, "Error : Supplied core id %d is invalid. Valid range is 0 - %d", nCoreID_, nThreads - 1);
    strStatus = strErrMsg;
    nRet = -1;
  }
  else
  {
    CPU_SET(nCoreID_,&set);

    if(sched_setaffinity(nPid_,sizeof(cpu_set_t), &set) < 0)
    {
      sprintf(strErrMsg, "Error : %d returned while setting process affinity for process ID . Valid range is 0 - %d", errno,nPid_);
      strStatus = strErrMsg;
      nRet = -1;
    }
  }
  return nRet;
};
