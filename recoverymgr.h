#ifndef _RECOVERY_MGR_H_
#define _RECOVERY_MGR_H_

#include <iostream>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/time.h>
 #include <unistd.h>
#include <sys/fcntl.h>
#include <inttypes.h>

#include "defines_internal.h"
#include "log.h"
#include "spsc_atomic.h"

#define SYSTEM_CALL_ERROR           -1
#define ERROR_LOCATION              __func__, __FILE__, __LINE__


extern bool WriteDataInFile1(const CompositeBcastMsg& stCompositeBcastMsg);

class CRecoveryMgr
{
  typedef bool (CRecoveryMgr::*fnWriteMsgPtr)(const CompositeBcastMsg& stCompositeBcastMsg);  
  
  public:
    CRecoveryMgr(const uint32_t& nPortNumber, char* pcRecoveryIP, 
                 const short nStremId, const int nMaxPacketCountInRecovery, const int64_t nConnectionDelay) : 
                 m_nRecoverySock(-1), m_nPortNumber(nPortNumber), 
                 m_nMaxPacketCount(nMaxPacketCountInRecovery)
    {
      memset(&m_cRecoveryIPAddr, 0, IP_ADDRESS_LEN + 1);
      
      memset(&m_stRecoverySeq, 0, sizeof(RECOVERY_REQ));
      memset(&m_stSockAddrIn,0,sizeof(struct sockaddr_in));      

      strncpy(m_cRecoveryIPAddr, pcRecoveryIP, IP_ADDRESS_LEN);      
      m_stRecoverySeq.cMsgType   = RECOVERY_REQUEST;
      m_stRecoverySeq.wStreamID  = nStremId;

      m_stSockAddrIn.sin_family = AF_INET;
      m_stSockAddrIn.sin_port = htons(m_nPortNumber);
      m_stSockAddrIn.sin_addr.s_addr = inet_addr(m_cRecoveryIPAddr);
      m_nConnectionDelay = nConnectionDelay; 
      m_nLastRecoveryTime = 2147483647;
    }
    
    ~CRecoveryMgr()
    {

    }

    //FromSeqNo 155549720 ToSeqNo 155549743
    //FromSeqNo 155549720 ToSeqNo 155549743 RecoveredLastSeqNo 155549728 Count 9
    //FromSeqNo 155549729 ToSeqNo 155549743 RecoveredLastSeqNo 155549742 Count 23 = 155549720 + 9  = 155549729
    //FromSeqNo 155549743 ToSeqNo 155549743 RecoveredLastSeqNo 155549743 Count 24 = 155549720 + 23 = 155549743   
    int ForceRecoveryPackets(const int& nFromSeqNo, const int& nToSeqNo, int& nRecoveryCount, int& nLastRecoverySeqNo)
    {
      Logger& lcLogger = Logger::getLogger();      
      lcLogger.log(REVY, "{ START ForceRecoveryPackets FromSeqNo %d and  ToSeqNo %d", nFromSeqNo, nToSeqNo);      
      
      if(nToSeqNo < nFromSeqNo)
      {
        lcLogger.log(REVY, "Error Invalid From and ToSeqNo. FromSeqNo %d ToSeqNo %d StremId %hd ", nFromSeqNo, nToSeqNo, m_stRecoverySeq.wStreamID);
        return -1; 
      }        
      
      int lnNewRecoveryCount = 0;
      while(nLastRecoverySeqNo != nToSeqNo)
      {
        lnNewRecoveryCount = 0;        
        const int lnNewFromSeqNo = (nFromSeqNo + nRecoveryCount);        
        if(0 == RecoveryPackets(lnNewFromSeqNo, nToSeqNo, lnNewRecoveryCount, std::ref(nLastRecoverySeqNo)))
        {
          nRecoveryCount += lnNewRecoveryCount;
          lcLogger.log(REVY, "Success from RecoveryPackets NewFromSeqNo %d ToSeqNo %d LastRecoverySeqNo %d RecoveryCount %d", lnNewFromSeqNo, nToSeqNo, nLastRecoverySeqNo, lnNewRecoveryCount);
          break;
        }
        nRecoveryCount += lnNewRecoveryCount;
      }
      
      lcLogger.log(REVY, "} END In ForceRecoveryPackets FromSeqNo %d ToSeqNo %d LastRecoverySeqNo %d TotalRecoveryCount %d }", nFromSeqNo, nToSeqNo, nLastRecoverySeqNo, nRecoveryCount);
      return 0;
    }
    
    void SplitRecoveryDataRequest(int nFromSeqNo, int nToSeqNo)    
    {
      Logger& lcLogger = Logger::getLogger();            
      if(nToSeqNo < nFromSeqNo)
      {
        lcLogger.log(REVY, "Error Invalid From and ToSeqNo. FromSeqNo %d ToSeqNo %d StremId %hd ", nFromSeqNo, nToSeqNo, m_stRecoverySeq.wStreamID);
        return; 
      }       
      
      lcLogger.log(REVY, "{START SplitRecoveryDataRequest FromSeqNo %d ToSeqNo %d StremId %hd", nFromSeqNo, nToSeqNo, m_stRecoverySeq.wStreamID);      

      //int lnFullCount     =  (nToSeqNo - nFromSeqNo) / MAX_SINGLE_RECOVERY_COUNT;  
      int lnFullCount     =  (nToSeqNo - nFromSeqNo) / m_nMaxPacketCount;        
      
      int lnFromSeqNo = nFromSeqNo;
      int lnToSeqNo = nFromSeqNo;  
      int lnRecoveryCount = 0;
      int lnLastRecoverySeqNo = 0;      
      for(int lnLoop = 0; lnLoop < lnFullCount; ++lnLoop)
      {
        lnRecoveryCount = 0;
        lnLastRecoverySeqNo = 0;        
        lnToSeqNo += m_nMaxPacketCount;
        if(-1 == RecoveryPackets(lnFromSeqNo, lnToSeqNo, std::ref(lnRecoveryCount), std::ref(lnLastRecoverySeqNo)))
        {
          ForceRecoveryPackets(lnFromSeqNo, lnToSeqNo, lnRecoveryCount, std::ref(lnLastRecoverySeqNo));
        }
        
        if(nToSeqNo == lnLastRecoverySeqNo)
        {
          lcLogger.log(REVY, "} END SplitRecoveryDataRequest FromSeqNo %d ToSeqNo %d StremId %hd", nFromSeqNo, nToSeqNo, m_stRecoverySeq.wStreamID); 
          return;
        }        
        lnFromSeqNo = (lnToSeqNo + 1);    
      }
  
      lnRecoveryCount = 0;
      lnLastRecoverySeqNo = 0;      
      if(-1 == RecoveryPackets(lnFromSeqNo, nToSeqNo, lnRecoveryCount, std::ref(lnLastRecoverySeqNo)))
      {
        ForceRecoveryPackets(lnFromSeqNo, nToSeqNo, lnRecoveryCount, std::ref(lnLastRecoverySeqNo));
      }
      
      lcLogger.log(REVY, "} END SplitRecoveryDataRequest FromSeqNo %d ToSeqNo %d StremId %hd", nFromSeqNo, nToSeqNo, m_stRecoverySeq.wStreamID); 
      return;
    }
    
    int GetPacketFromMessage(char* pcBuffer, const int nTotalSize, int& nPendingData, int& nLastSeqNo, int& nCount)
    {
      Logger& lcLogger = Logger::getLogger();            
      
      struct timespec tv0;        
      clock_gettime(CLOCK_REALTIME,&tv0);                          
      
      CompositeBcastMsg lstCompositeBcastMsg;      
      
      const int lnMsgSize = sizeof(CompositeBcastMsg);
      int lnByteCount = 0;      
      
      bool lbPartialRead = false;
      int lnAvailable = 0;
      while(lnAvailable < nTotalSize)
      {
        if((nTotalSize - lnAvailable) <  sizeof(MSG_HEADER) ) 
        {
         nPendingData = nTotalSize - lnAvailable;
         memcpy(pcBuffer, pcBuffer + lnAvailable, nPendingData);
         return 0;              
        }

        MSG_HEADER* lpstMsgHeader = (MSG_HEADER*)(pcBuffer + lnAvailable);
        switch(lpstMsgHeader->cMsgType)
        {
          case RECOVERY_RESPONSE:
          {
            if(lnAvailable + sizeof(RECOVERY_RESP) <= nTotalSize)
            {
              RECOVERY_RESP* lpstRecoeryResp = (RECOVERY_RESP*)(pcBuffer + lnAvailable);
              if(lpstRecoeryResp->cReqStatus == RECOVERY_RESPONSE && lpstRecoeryResp->cReqStatus != REQUEST_SUCCESS)          
              {
                lcLogger.log(ERROR, "Error in first packet...");
                return -1;
              }
              else
              {
                lcLogger.log(INFO, "Success received from recovery server...");
                lnAvailable += sizeof(RECOVERY_RESP);          
              }
            }
          }
          break;

          case NEW_ORDER:
          case ORDER_MODIFICATION:
          case ORDER_CANCELLATION:
          case SPREAD_NEW_ORDER: 
          case SPREAD_ORDER_MODIFICATION:
          case SPREAD_ORDER_CANCELLATION:
          {
            if(lnAvailable + sizeof(GENERIC_ORD_MSG) <= nTotalSize)
            {
              clock_gettime(CLOCK_REALTIME,&tv0);                          
              
              GENERIC_ORD_MSG* lpstGenericOrdmsg = (GENERIC_ORD_MSG*)(pcBuffer + lnAvailable);
              lstCompositeBcastMsg.stBcastMsg.stGegenricOrdMsg = std::move(*lpstGenericOrdmsg);
              lstCompositeBcastMsg.tv = tv0;
              
              WriteDataInFile1(lstCompositeBcastMsg);
              
              nLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stGegenricOrdMsg.header.nSeqNo;
              lnAvailable += sizeof(GENERIC_ORD_MSG);
              ++nCount;
            }
            else
            {
              lbPartialRead = true;        
            }
          }
          break;

          case TRADE_MESSAGE:
          {
            if(lnAvailable + sizeof(TRD_MSG) <= nTotalSize)
            {
              clock_gettime(CLOCK_REALTIME,&tv0);                          
              
              TRD_MSG* lpstTrdMsg = (TRD_MSG*)(pcBuffer + lnAvailable);
              lstCompositeBcastMsg.stBcastMsg.stTrdMsg = std::move(*lpstTrdMsg);
              lstCompositeBcastMsg.tv = tv0;
              
              WriteDataInFile1(lstCompositeBcastMsg);                            
              
              nLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stTrdMsg.header.nSeqNo;          
              lnAvailable += sizeof(TRD_MSG);
              ++nCount;          
            }
            else
            {
              lbPartialRead = true;                
            }              
          }
          break;

          case SPREAD_TRADE_MESSAGE:                  
          {
            if(lnAvailable + sizeof(TRD_MSG) <= nTotalSize)
            {
              clock_gettime(CLOCK_REALTIME,&tv0);                          
              
              TRD_MSG* lpstTrdMsg = (TRD_MSG*)(pcBuffer + lnAvailable);
              lstCompositeBcastMsg.stBcastMsg.stTrdMsg = std::move(*lpstTrdMsg);
              lstCompositeBcastMsg.tv = tv0;
              
              WriteDataInFile1(lstCompositeBcastMsg);                                          
              
              nLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stTrdMsg.header.nSeqNo;          
              lnAvailable += sizeof(TRD_MSG);
              ++nCount;          
            }
            else
            {
              lbPartialRead = true;                
            }              
          }
          break;
          
          case BEGIN_OF_MASTER:
          {
            if(lnAvailable + sizeof(MST_DATA_HEADER) <= nTotalSize)
            {
              MST_DATA_HEADER* lpstDataHeader = (MST_DATA_HEADER*)(pcBuffer + lnAvailable);
              lstCompositeBcastMsg.stBcastMsg.stMSTDataHeader = std::move(*lpstDataHeader);
              lstCompositeBcastMsg.tv = tv0;
              
              WriteDataInFile1(lstCompositeBcastMsg);                                          
              
              nLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stMSTDataHeader.header.nSeqNo;                    
              lnAvailable += sizeof(MST_DATA_HEADER);
              ++nCount;                    
            }
            else
            {
              lbPartialRead = true;                
            }
          }
          break;

          case CONTRACT_INFORMATION:
          {
            if(lnAvailable + sizeof(CONTRACT_INFO) <= nTotalSize)
            {
              CONTRACT_INFO* lpstContractInfo = (CONTRACT_INFO*)(pcBuffer + lnAvailable);
              lstCompositeBcastMsg.stBcastMsg.stContractInfo = std::move(*lpstContractInfo);
              lstCompositeBcastMsg.tv = tv0;
              
              WriteDataInFile1(lstCompositeBcastMsg);                                                        
              
              nLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stContractInfo.header.nSeqNo;                              
              lnAvailable += sizeof(CONTRACT_INFO);
              ++nCount;                              
            }
            else
            {
              lbPartialRead = true;                
            }              
          }
          break;

          case SPREAD_INFORMATION:
          {
            if(lnAvailable + sizeof(SPRD_CONTRACT_INFO) <= nTotalSize)
            {            
              SPRD_CONTRACT_INFO* lpstSprdContractInfo = (SPRD_CONTRACT_INFO*)(pcBuffer + lnAvailable);
              lstCompositeBcastMsg.stBcastMsg.stSprdContractInfo = std::move(*lpstSprdContractInfo);
              lstCompositeBcastMsg.tv = tv0;
              
              WriteDataInFile1(lstCompositeBcastMsg);              
              nLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stSprdContractInfo.header.nSeqNo;                                        
              lnAvailable += sizeof(SPRD_CONTRACT_INFO);
              ++nCount;                                        
            }
            else
            {
              lbPartialRead = true;                
            }              
          }
          break;

          case END_OF_MASTER:
          {
            if(lnAvailable + sizeof(MST_DATA_TRAILER) <= nTotalSize)
            {
              MST_DATA_TRAILER* lpstDataTrailer = (MST_DATA_TRAILER*)(pcBuffer + lnAvailable);
              lstCompositeBcastMsg.stBcastMsg.stMSTDataTrailer = std::move(*lpstDataTrailer);
              lstCompositeBcastMsg.tv = tv0;
              nLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stMSTDataTrailer.header.nSeqNo;                                        

              lnAvailable += sizeof(MST_DATA_TRAILER);
              ++nCount;                                                  
            }
            else
            {
              lbPartialRead = true;                
            }              
          }
          break;

          default:
          {
            lcLogger.log(ERROR, "Invalid message type in recovery mode : %c", lpstMsgHeader->cMsgType);            
          }
          break;
        }

        if(lbPartialRead == true)  
        {
          nPendingData = nTotalSize - lnAvailable;
          memcpy(pcBuffer, pcBuffer + lnAvailable, nPendingData);
          break;    
        }
      }

      return 0;
    }
    
    int RecoveryPackets(const int& nFromSeqNo, const int& nToSeqNo, int& nRecoveryCount, int& nLastSeqNo)
    {
      Logger& lcLogger = Logger::getLogger();                          
      
      //{ A per exchange guidelines 24102017 START          
      clock_gettime(CLOCK_REALTIME, &m_timespece);        
      int64_t lnCurrentTimeInMS = (m_timespece.tv_sec*1000000000L + m_timespece.tv_nsec)/1000;
      int64_t lnDelay = lnCurrentTimeInMS - m_nLastRecoveryTime;
      if(lnDelay < m_nConnectionDelay)
      {
        __useconds_t lnNewDelay = m_nConnectionDelay - lnDelay;
        snprintf(m_cStrErrorMsg, sizeof(m_cStrErrorMsg), "Waiting for exchange defined delay. New Calculate Delay|%u|MaxDelay|%" PRId64 "|CurrentTime|%" PRId64 "|LastRecoveryTime|%" PRId64, lnNewDelay, m_nConnectionDelay, lnCurrentTimeInMS, m_nLastRecoveryTime);
        Logger::getLogger().log(REVY, m_cStrErrorMsg);   
        usleep(lnNewDelay);
      }
      //} A per exchange guidelines 24102017 END    
      
      m_stRecoverySeq.nBegSeqNo  = nFromSeqNo;
      m_stRecoverySeq.nEndSeqNo  = nToSeqNo;        
     
      int lnRet = CreateSocket();
      if(lnRet != 0)
      {
        return -1;
      }
      
      lnRet = ConnectServer();
      if(lnRet != 0)
      {
        return -1;
      }
      
//      lnRet = SetListenerConfiguration(m_nRecoverySock);      
//      if(lnRet != 0)
//      {
//        lcLogger.log(ERROR, "Error SetListenerConfiguration failed. %d returned.",errno);        
//        return -1;
//      }      
      
      lnRet = send(m_nRecoverySock, &m_stRecoverySeq, sizeof(RECOVERY_REQ),0);
      if(lnRet <= 0)
      {
        lcLogger.log(REVY, "Error while sending recovery request to server. %d returned.",errno);
        close(m_nRecoverySock);
        return -1;
      }
      
      //{ As per exchange guidelines 24102017 START                
      clock_gettime(CLOCK_REALTIME, &m_timespece);        
      m_nLastRecoveryTime = (m_timespece.tv_sec*1000000000L + m_timespece.tv_nsec)/1000;;
      //} As per exchange guidelines 24102017 END                
      
      char lcRecoveryData[8192]= {0};    
      int lnBuffSize = sizeof(lcRecoveryData);

      int lnTotalSize = lnBuffSize;
      int lnPendingData = 0;
      while(true)
      {
        int lnRet = recv(m_nRecoverySock,(void*)(lcRecoveryData + lnPendingData), (lnBuffSize - lnPendingData), 0);    
        if(0 == lnRet)
        {
          lcLogger.log(REVY, "Socket close. Data recovered from FromSeqNo %d ToSeqNo %d RecoveredLastSeqNo %d Count %d", nFromSeqNo, nToSeqNo, nLastSeqNo, nRecoveryCount);          
          close(m_nRecoverySock);
          break;    
        }
        else if(lnRet == SYSTEM_CALL_ERROR)
        {
          lcLogger.log(REVY, "Error while recv data from server errno is %d.Data recovered from FromSeqNo %d ToSeqNo %d RecoveredLastSeqNo %d Count %d", errno, nFromSeqNo, nToSeqNo, nLastSeqNo, nRecoveryCount);
          close(m_nRecoverySock);
          break;
        }
        
        lnTotalSize = lnRet + lnPendingData;
        lnPendingData = 0;
        if(-1 == GetPacketFromMessage((char*)(lcRecoveryData + lnPendingData), lnTotalSize, lnPendingData, nLastSeqNo, nRecoveryCount))
        {
          close(m_nRecoverySock);          
          lcLogger.log(REVY, "Error in GetPacketFromMessage.Data recovered from FromSeqNo %d ToSeqNo %d RecoveredLastSeqNo %d Count %d.", nFromSeqNo, nToSeqNo, nLastSeqNo, nRecoveryCount);
          return -1;
        }
      }
      
      lcLogger.log(REVY, "Data recovered from FromSeqNo %d ToSeqNo %d RecoveredLastSeqNo %d Count %d", nFromSeqNo, nToSeqNo, nLastSeqNo, nRecoveryCount);
      if(nToSeqNo == nLastSeqNo)
      {
        return 0;        
      }
      return -1;
    }

  private:
    int ConnectServer()
    {
      Logger& lcLogger = Logger::getLogger();                                
      if(connect(m_nRecoverySock, (struct sockaddr*)&m_stSockAddrIn, sizeof(struct sockaddr_in)) == -1)
      {
        lcLogger.log(REVY, "Error while connecting client socket to primary server. %d code returned and error is %s.",errno, strerror(errno));
        return -1;
      }
      lcLogger.log(REVY, "Connection on primary successful.");
      return 0;        
    }
    
    int CreateSocket()
    {
      Logger& lcLogger = Logger::getLogger();                                      
      
      m_nRecoverySock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); 
      if(m_nRecoverySock <= -1)
      {
        lcLogger.log(REVY, "Error while creating client socket. %d code returned.",errno);
        return -1;
      }
      return 0;
      //return SetListenerConfiguration(m_nRecoverySock);
    }

    int SetListenerConfiguration(int nSockFd)
    {
      char lcErrorString[512 + 1] = {0};
      struct linger lstLingerOpt;
      lstLingerOpt.l_onoff = 1;
      lstLingerOpt.l_linger = 10;
      int lnRetVal = 0;
      int lnOptVal = 0;

      lnRetVal = setsockopt(nSockFd, SOL_SOCKET, SO_LINGER, (const void *)(&lstLingerOpt), sizeof(lstLingerOpt));
      if (SYSTEM_CALL_ERROR == lnRetVal)
      {
        snprintf(lcErrorString, sizeof(lcErrorString), "%s-%s(%d): Setting Linger Option failed FD(%d) reason(%s)", ERROR_LOCATION, nSockFd, strerror(errno));
        std::cout << lcErrorString << std::endl;
        return SYSTEM_CALL_ERROR;
      }

      lnOptVal = 1;
      lnRetVal = setsockopt(nSockFd, SOL_SOCKET, SO_REUSEADDR, (const void *)&lnOptVal, sizeof(lnOptVal));
      if (SYSTEM_CALL_ERROR == lnRetVal)
      {
        snprintf(lcErrorString, sizeof(lcErrorString), "%s-%s(%d): Setting Reuse add Option failed FD(%d) reason(%s)", ERROR_LOCATION, nSockFd, strerror(errno));
        std::cout << lcErrorString << std::endl;
        return SYSTEM_CALL_ERROR;
      }

      lnOptVal = 134217728;
      lnRetVal = setsockopt(nSockFd, SOL_SOCKET, SO_SNDBUF, (const void *)&lnOptVal, sizeof(lnOptVal));
      if (SYSTEM_CALL_ERROR == lnRetVal)
      {
        snprintf(lcErrorString, sizeof(lcErrorString), "%s-%s(%d): Setting SO_SNDBUF Option failed FD(%d) reason(%s)", ERROR_LOCATION, nSockFd, strerror(errno));
        std::cout << lcErrorString << std::endl;
        return SYSTEM_CALL_ERROR;
      }

      lnOptVal = 134217728;
      lnRetVal = setsockopt(nSockFd, SOL_SOCKET, SO_RCVBUF, (const void *)&lnOptVal, sizeof(lnOptVal));
      if (SYSTEM_CALL_ERROR == lnRetVal)
      {
        snprintf(lcErrorString, sizeof(lcErrorString), "%s-%s(%d): Setting SO_RCVBUF Option failed FD(%d) reason(%s)", ERROR_LOCATION, nSockFd, strerror(errno));
        std::cout << lcErrorString << std::endl;
        return SYSTEM_CALL_ERROR;
      }

      int lnNonBlocking = 1;
      lnRetVal = ioctl(nSockFd, FIONBIO, &lnNonBlocking);
      if (SYSTEM_CALL_ERROR == lnRetVal)	
      {
        snprintf(lcErrorString, sizeof(lcErrorString), "%s-%s(%d): Setting FIONBIO Option failed FD(%d) reason(%s)", ERROR_LOCATION, nSockFd, strerror(errno));
        std::cout << lcErrorString << std::endl;
        return SYSTEM_CALL_ERROR;
      }

      lnOptVal = 1;
      lnRetVal = setsockopt(nSockFd, SOL_TCP, TCP_NODELAY, (const void *)(&lnOptVal), sizeof(lnOptVal));
      if (SYSTEM_CALL_ERROR == lnRetVal)
      {
        snprintf(lcErrorString, sizeof(lcErrorString), "%s-%s(%d): Setting TCP_NODELAY Option failed FD(%d) reason(%s)", ERROR_LOCATION, nSockFd, strerror(errno));
        std::cout << lcErrorString << std::endl;
        return SYSTEM_CALL_ERROR;
      }
      return 0;
    }
    

  private:
    RECOVERY_REQ                              m_stRecoverySeq;
    sockaddr_in                               m_stSockAddrIn;        
    int                                       m_nRecoverySock;
    uint32_t                                  m_nPortNumber;
    int                                       m_nMaxPacketCount;    
    char                                      m_cRecoveryIPAddr[32 + 1];    
    char                                      m_cStrErrorMsg[512];    
    FILE*                                     m_pcFile;
    fnWriteMsgPtr                             m_fnPtr;
    ProducerConsumerQueue<CompositeBcastMsg>* m_pcPrimaryQ;    
    
    //{ A per exchange guidelines 24102017 START    
    int64_t                                   m_nConnectionDelay;    
    struct timespec                           m_timespece;          
    int64_t                                   m_nCmpFromLastRecovery;    
    int64_t                                   m_nLastRecoveryTime;
    //} A per exchange guidelines 24102017 END        
};

#endif //_RECOVERY_MGR_H_
