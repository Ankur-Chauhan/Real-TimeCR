#ifndef _FEED_RECEIVER_
#define _FEED_RECEIVER_


#include <string.h>
#include <string>
#include <netinet/in.h>
#include <pthread.h>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>

#include <vector>
#include<fstream>
#include<sstream>

#include "defines_internal.h"
#include "nsetbt_mcast_msg.h"
#include "spsc_atomic.h"


class CFeedReceiver
{
  private:
    uint32_t                         m_nMulticastPortNumber;
    int                              m_nSockMulticast;    
    int                              m_nCoreId; 
    ProducerConsumerQueue<CompositeBcastMsg>* m_pcQ;
    
    void ReceiveData();
    void ReceiveDataWithNocheck();
    
  public:
    CFeedReceiver();
    int Init(const std::string& cPrimaryMultiCastIPAddr, const std::string& cInterfaceIPAddr, const uint32_t& nMulticastPortNumber, 
            ProducerConsumerQueue<CompositeBcastMsg>* m_pcQ,
            const int& nCoreId);
    void FeedReceiverProc(void* pThreadParam);
    void FeedReceiverProc1(void* pThreadParam);    
};

#endif
