/* 
 * File:   main.cpp
 * Author: SantoshGG
 *
 * Created on May 31, 2016, 12:08 PM
 */

#include <stdio.h>
#include <iostream>
#include <cstdlib>
#include <stdio.h>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <set>
#include <map>
#include <unordered_map>
#include <fstream>
#include <iomanip>

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
#include <fstream>
#include <sstream>
#include <atomic>


#include "defines_internal.h"
#include "nsetbt_mcast_msg.h"
#include "struct_conrevtrade.h"

#include "log.h"
#include "shmQueue.h"
#include "ConfigReader.h"
#include "recoverymgr.h"
#include "feedreceiver.h"

using namespace std;

typedef std::set<int32_t>TOKEN_STORE;
typedef TOKEN_STORE::iterator TOKEN_ITR;

typedef std::vector<CompositeBcastMsg>TRADE_MSG_STORE;
typedef TRADE_MSG_STORE::iterator ITR;

typedef std::vector<TRADE_MSG_STORE>TRADE_VECTOR_STORE;

typedef std::unordered_map<int32_t, OMSScripInfo*>TOKENDATA_STORE;
typedef TOKENDATA_STORE::iterator TOKENDATA_ITR;

typedef std::unordered_map<std::string, tagTradeDetails*>CONREVTRADE_STORE;
typedef CONREVTRADE_STORE::iterator CONREV_TRADE_ITR;

typedef shmQueue1<tagConRevTradeDetails, 1000000> CONREVQ;  

bool WriteDataInFile1(const CompositeBcastMsg& stCompositeBcastMsg);
bool WriteDataInFile(const CompositeBcastMsg& stCompositeBcastMsg, TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKEN_STORE& cTokenStoreTemp,  TOKENDATA_STORE& cTokenDataTemp, CONREVQ* pcConRevQ);

int FileDigester (const char* pStrFileName, TOKENDATA_STORE& cTokenData);
bool ValidateTradeMsgRecords(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKEN_STORE& cTokenStoreTemp, TOKENDATA_STORE& cTokenDataTemp);
bool SplitTrade(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKEN_STORE& cTokenStoreTemp,  TOKENDATA_STORE& cTokenDataTemp, TRADE_VECTOR_STORE& vTradeVectorStore);
bool RemoveInvalidateTradeMsg(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKEN_STORE& cTokenStoreTemp,  TOKENDATA_STORE& cTokenDataTemp);
bool SecondLevelOrderValidation(int16_t nCustomInstrumentType, TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKENDATA_STORE& cTokenDataTemp);
bool IsValidateConRev(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKEN_STORE& cTokenStoreTemp,  TOKENDATA_STORE& cTokenDataTemp);
void DeletePacketTillExpiryDate(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKENDATA_STORE& cTokenDataTemp, int32_t nExpiryDate);
void DeletePacketTillStrikePrice(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKENDATA_STORE& cTokenDataTemp, int32_t nStrikePrice);
void GetExtraDetails(TRADE_MSG_STORE& vTradeMsgStoreTemp2, TOKEN_STORE& cTokenStoreTemp2, TOKENDATA_STORE& cTokenDataTemp, int32_t& nFutQty, int32_t& nCallQty, int32_t& nPutQty, int32_t& nStrikePrice, bool& bFutToken, bool& bPutToken, bool& bCallToken, int32_t& nLastExpiryDate, int16_t& nLastCustomInstrumentType);
void ResetExtraDetails(TOKEN_STORE& cTokenStoreTemp2, int32_t& nFutQty, int32_t& nCallQty, int32_t& nPutQty, int32_t& nStrikePrice, bool& bFutToken, bool& bPutToken, bool& bCallToken, int32_t& nLastExpiryDate, int16_t& nLastCustomInstrumentType);
bool GetMiddleQty(int32_t& nQty, TRADE_MSG_STORE& vTradeMsgStoreTemp2, TOKENDATA_STORE& cTokenDataTemp, int32_t& nStartIndex, int32_t& nEndIndex);


TOKENDATA_STORE*      g_pcTokenDataStore;
TRADE_MSG_STORE*      g_pcTradeMsgStore;
TOKEN_STORE*          g_pccTokenStore;
CONREVTRADE_STORE*    g_pcConRevTradeStore;
CONREVQ*              g_pcConRevQ;  

const std::string MONTH[] = {"JAN","FEB","MAR","APR","MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};     

int main(int argc, char** argv)
{
  std::string lszFileName(CONFIG_FILE_NAME);
  ConfigReader lcConfigReader(lszFileName);
  
  int32_t lnReceiverCoreID = atoi(lcConfigReader.getProperty("RECEIVER_CORE_ID").c_str());            
  short lnStremID = atoi(lcConfigReader.getProperty("STREAM_ID").data());
  const int lnMaxPacketCountInRecovery = atoi(lcConfigReader.getProperty("SINGLE_RECOVERY_COUNT").data());  
  const int lnLoggerCoreId = atoi(lcConfigReader.getProperty("LOGGER_CORE_ID").data());    
  Logger::getLogger().setDump(lcConfigReader.getProperty("LOG_FILE_NAME"), lnLoggerCoreId);    
  Logger& lcLogger = Logger::getLogger();
  
  
  uint32_t lnRecoveryPort = 0;
  char lcRecoveryIPAddr[IP_ADDRESS_LEN + 1] = {0};
    
  strncpy(lcRecoveryIPAddr, lcConfigReader.getProperty("RECOVERY_IP_ADDR").data(), IP_ADDRESS_LEN);    
  lnRecoveryPort = atoi(lcConfigReader.getProperty("RECOVERY_PORT").data());          
  int32_t lnMulticastPortNumber = atoi(lcConfigReader.getProperty("SOURCE_MULTI_CAST_PORT_NUMBER").c_str());          
  std::string lszSourceMultiCastIpAddr = lcConfigReader.getProperty("SOURCE_MULTI_CAST_IP_ADDR");
  std::string lszInterfaceIpAddr = lcConfigReader.getProperty("INTERFACE_IP_ADDR");
  std::string lszContractFilePath = lcConfigReader.getProperty("CONTRACT_FILE_PATH").c_str();
  std::string lszShmQFilePath = lcConfigReader.getProperty("SHM_FILE_PATH").c_str();  
  int64_t lnConnectionDelay = atoi(lcConfigReader.getProperty("CONNECTION_DELAY").c_str());    
  
  int lnDoesNotExist = 0;
  g_pcConRevQ = new CONREVQ(lszShmQFilePath.c_str(), lnDoesNotExist);
  
  cpu_set_t set;
  CPU_ZERO(&set);
  int lnPid = 0;  

  CPU_SET(lnReceiverCoreID, &set);
  if(sched_setaffinity(lnPid, sizeof(cpu_set_t), &set) < 0)
  {
    std::cout << "sched_setaffinity failed for CoreID " << lnReceiverCoreID << std::endl;
    return EXIT_FAILURE;
  }
  
  //2 tbt file contract    
  TOKENDATA_STORE       lcTokenDataStore;
  TRADE_MSG_STORE       lvTradeMsgStore;
  TOKEN_STORE           lcTokenStore;
  CONREVTRADE_STORE     lcConRevTradeStore;
  
  g_pcTokenDataStore    = &lcTokenDataStore;
  g_pcTradeMsgStore     = &lvTradeMsgStore;
  g_pccTokenStore       = &lcTokenStore;
  g_pcConRevTradeStore  = &lcConRevTradeStore;  
  
  FileDigester(lszContractFilePath.c_str(), lcTokenDataStore);  
  
  CRecoveryMgr lcRecoveryMgr(lnRecoveryPort, lcRecoveryIPAddr, lnStremID, lnMaxPacketCountInRecovery, lnConnectionDelay);
  
  struct sockaddr_in server_addr;
  struct ip_mreq multicastAddr;

	char strErrorMsg[256] = {""};    

	memset(&server_addr,'0',sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(lnMulticastPortNumber);   
	multicastAddr.imr_multiaddr.s_addr = inet_addr(lszSourceMultiCastIpAddr.c_str());
	multicastAddr.imr_interface.s_addr = inet_addr(lszInterfaceIpAddr.c_str());

	int m_nSockMulticast = socket(AF_INET,SOCK_DGRAM,0);
	if(m_nSockMulticast < 0 )
	{
    sprintf(strErrorMsg,"Error in creation of Sockets with error : %s.", strerror(errno));    
    std::cout << strErrorMsg << std::endl;
    lcLogger.log(ERROR, strErrorMsg);
    return -1;    
	}
  
  int lnRecevBufSize = 134217728;
  if(0 != setsockopt(m_nSockMulticast, SOL_SOCKET, SO_RCVBUF, &lnRecevBufSize, sizeof(lnRecevBufSize)))
  {
    sprintf(strErrorMsg,"Error while setting SO_RCVBUF. Error number returned : %s",strerror(errno));
    std::cout << strErrorMsg << std::endl;
    lcLogger.log(ERROR, strErrorMsg);    
    close(m_nSockMulticast);
    return -1;
  }
  
  int nReuseAddr = 1;
  if(setsockopt(m_nSockMulticast,SOL_SOCKET,SO_REUSEADDR,(char*)&nReuseAddr, sizeof(nReuseAddr)) < 0)
  {
    sprintf(strErrorMsg,"Error while setting REUSEADDR. Error number returned : %s",strerror(errno));
    std::cout << strErrorMsg << std::endl;        
    close(m_nSockMulticast);      
    lcLogger.log(ERROR, strErrorMsg);        
    return -1;    
  }
  
  if(bind(m_nSockMulticast, (struct sockaddr*)&server_addr, sizeof(server_addr))< 0)
  {
    sprintf(strErrorMsg,"Error while binding socket : %s", strerror(errno));
    std::cout << strErrorMsg << std::endl;            
    lcLogger.log(ERROR, strErrorMsg);            
    close(m_nSockMulticast);      
    return -1;    
  }
  
  if(setsockopt(m_nSockMulticast,IPPROTO_IP, IP_ADD_MEMBERSHIP, &multicastAddr, sizeof(multicastAddr)) < 0)
  {
    sprintf(strErrorMsg,"Error while setsockopt : %s",strerror(errno));
    std::cout << strErrorMsg << std::endl;                
    close(m_nSockMulticast);
    lcLogger.log(ERROR, strErrorMsg);                
    return -1;    
  }

  std::cout << "RealTime ConRev Trade Generator started successfully..." << std::endl;                  
  
  int lnRecvCount = 0;    
  CompositeBcastMsg lstCompositeBcastMsg;
  const int lnMsgSize = sizeof(BcastMsg);
  
  int lnLastSeqNo = 0;  
  while(1)		
  {	
    lnRecvCount = recvfrom(m_nSockMulticast,(void*)&lstCompositeBcastMsg, lnMsgSize, MSG_DONTWAIT, NULL, NULL);
    if(lnRecvCount > 0)
    {
      if(lstCompositeBcastMsg.stBcastMsg.stHeader.nSeqNo != 0)
      {
        clock_gettime(CLOCK_REALTIME,&lstCompositeBcastMsg.tv);              
        
        if(lstCompositeBcastMsg.stBcastMsg.stHeader.nSeqNo  <= lnLastSeqNo)
          continue;

        if((lstCompositeBcastMsg.stBcastMsg.stHeader.nSeqNo - lnLastSeqNo) == 1)    
        {
          lnLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stHeader.nSeqNo;                    
          WriteDataInFile(lstCompositeBcastMsg, lvTradeMsgStore, lcTokenStore, lcTokenDataStore, g_pcConRevQ);      
          //std::cout << "case 2 lnLastSeqNo " << lnLastSeqNo << std::endl;                      
        }
        else 
        {
          if((lstCompositeBcastMsg.stBcastMsg.stHeader.nSeqNo - lnLastSeqNo) > 1)            
          {
            int lnFromSeqNo = lnLastSeqNo + 1;
            int lnToSeqNo = lstCompositeBcastMsg.stBcastMsg.stHeader.nSeqNo - 1;

            lcRecoveryMgr.SplitRecoveryDataRequest(lnFromSeqNo, lnToSeqNo);              
            WriteDataInFile(lstCompositeBcastMsg, lvTradeMsgStore, lcTokenStore, lcTokenDataStore, g_pcConRevQ);      
            lnLastSeqNo = lstCompositeBcastMsg.stBcastMsg.stHeader.nSeqNo;        
            
            //std::cout << "case lnLastSeqNo " << lnLastSeqNo << std::endl;            
          }
        }    
      }
    }
  }
  return 0;
}

bool WriteDataInFile1(const CompositeBcastMsg& stCompositeBcastMsg)
{
  WriteDataInFile(stCompositeBcastMsg, *g_pcTradeMsgStore, *g_pccTokenStore, *g_pcTokenDataStore, g_pcConRevQ);
  return true;
}

bool WriteDataInFile(const CompositeBcastMsg& stCompositeBcastMsg, TRADE_MSG_STORE& vTradeMsgStore, TOKEN_STORE& cTokenStore, TOKENDATA_STORE& cTokenDataStore, CONREVQ* pcConRevQ)
{
  int lnFirstSeqLastSeqNo = 0;    
  int lnLastSeqLastSeqNo = 0;      
  int64_t lnTimestamp = 0;        
  int64_t lnExpiryDate1  = 0;
  
  tagConRevTradeDetails lstConRevTradeDetails;
  memset(&lstConRevTradeDetails, 0, sizeof(tagConRevTradeDetails));  
  
  tagTradeDetails lstTradeDetails;
  memset(&lstTradeDetails, 0, sizeof(tagTradeDetails));
  
	char  lcScripOrContractName[64 + 1] = {0};    
  time_t lnTimeVal = 0;
  
  switch(stCompositeBcastMsg.stBcastMsg.stMsgHeader.cMsgType)
  {
    case NEW_ORDER:
    case ORDER_MODIFICATION:
    case ORDER_CANCELLATION:
    case SPREAD_NEW_ORDER:
    case SPREAD_ORDER_MODIFICATION:
    case SPREAD_ORDER_CANCELLATION:
    case SPREAD_TRADE_MESSAGE:        
    {
      if(!ValidateTradeMsgRecords(vTradeMsgStore, cTokenStore, cTokenDataStore))
      {
        //std::cout << " ValidateTradeMsgRecords " << std::endl;
        vTradeMsgStore.clear();
        cTokenStore.clear();          
        return true;
      }

      TRADE_VECTOR_STORE lvTradeVectorStore;        
      TRADE_VECTOR_STORE::iterator lcItrTVS;

      SplitTrade(vTradeMsgStore, cTokenStore, cTokenDataStore, lvTradeVectorStore);
      for(lcItrTVS = lvTradeVectorStore.begin(); lcItrTVS != lvTradeVectorStore.end(); ++lcItrTVS)
      { 
        memset(&lstTradeDetails, 0, sizeof(tagTradeDetails));          
        lnFirstSeqLastSeqNo = 0;          

        TRADE_MSG_STORE lvTradeMsgStoreTemp(*lcItrTVS);
        if(!IsValidateConRev(lvTradeMsgStoreTemp, cTokenStore, cTokenDataStore))
        {
          //std::cout << "Drop Packets start SequenceNo " << (*lvTradeMsgStoreTemp.begin()).stBcastMsg.stHeader.nSeqNo  << std::endl;
          continue;
        }          

        OMSScripInfo* lpstOMSScripInfo = NULL;                          
        int32_t lnTradeCount = lvTradeMsgStoreTemp.size();
        for(ITR lcItr = lvTradeMsgStoreTemp.begin(); lcItr != lvTradeMsgStoreTemp.end(); ++lcItr)
        {
          int64_t lnBuyOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblBuyOrdID;
          int64_t lnSellOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblSellOrdID;                      

          if(lnFirstSeqLastSeqNo == 0) lnFirstSeqLastSeqNo = lcItr->stBcastMsg.stTrdMsg.header.nSeqNo;     
          lnLastSeqLastSeqNo = lcItr->stBcastMsg.stTrdMsg.header.nSeqNo;   
          lnTimestamp = lcItr->stBcastMsg.stTrdMsg.lTimestamp;

          TOKENDATA_ITR lcTokenItr = cTokenDataStore.find(lcItr->stBcastMsg.stTrdMsg.nToken);
          if(lcTokenItr != cTokenDataStore.end())
          {
            lpstOMSScripInfo = (lcTokenItr->second);
            switch(lpstOMSScripInfo->nCustomInstrumentType)
            {
              case 1:
              {
                if(lnBuyOrderId == 0)
                {
                  lstTradeDetails.nStrategyIndentifier = 1;
                }

                if(lnSellOrderId == 0)
                {
                  lstTradeDetails.nStrategyIndentifier = 2;                    
                }

                lnExpiryDate1 = lpstOMSScripInfo->nExpiryDate;                
                lstTradeDetails.nTradedVolume1 += lcItr->stBcastMsg.stTrdMsg.nTradeQty;                   
                lstTradeDetails.nTradedValue1  += ((int64_t)lcItr->stBcastMsg.stTrdMsg.nTradeQty * lcItr->stBcastMsg.stTrdMsg.nTradePrice);
              }
              break;

              case 2:
              {
                lstTradeDetails.nStrikePrice = lpstOMSScripInfo->nStrikePrice;
                lstTradeDetails.nTradedVolume3 += lcItr->stBcastMsg.stTrdMsg.nTradeQty;                   
                lstTradeDetails.nTradedValue3  += ((int64_t)lcItr->stBcastMsg.stTrdMsg.nTradeQty * (lcItr->stBcastMsg.stTrdMsg.nTradePrice + lpstOMSScripInfo->nStrikePrice));
              }
              break;

              case 3:
              {
                lstTradeDetails.nStrikePrice = lpstOMSScripInfo->nStrikePrice;                    
                lstTradeDetails.nTradedVolume2 += lcItr->stBcastMsg.stTrdMsg.nTradeQty;                   
                lstTradeDetails.nTradedValue2  += ((int64_t)lcItr->stBcastMsg.stTrdMsg.nTradeQty * (lcItr->stBcastMsg.stTrdMsg.nTradePrice + lpstOMSScripInfo->nStrikePrice));
              }
              break;
            }
          }
        }//for(ITR lcItr = lvTradeMsgStore.begin(); lcItr != lvTradeMsgStore.end(); ++lcItr)
        lvTradeMsgStoreTemp.clear();

        lstTradeDetails.nAvgTradePrice1   = lstTradeDetails.nTradedValue1/lstTradeDetails.nTradedVolume1;
        lstTradeDetails.nAvgTradePrice2   = lstTradeDetails.nTradedValue2/lstTradeDetails.nTradedVolume2;
        lstTradeDetails.nAvgTradePrice3   = lstTradeDetails.nTradedValue3/lstTradeDetails.nTradedVolume3;            

        tm lstTimeStruct;              
        memset(&lstTimeStruct, 0, sizeof(tm));            
        lnTimeVal = lnExpiryDate1 + 315532800;            
        localtime_r(&lnTimeVal, &lstTimeStruct);    


        tm lstTimeStruct1;              
        memset(&lstTimeStruct1, 0, sizeof(tm));            
        time_t lnTimeVal1 = (lnTimestamp / 1000) + 315513000;
        localtime_r(&lnTimeVal1, &lstTimeStruct1);              
        //std::cout<< "S: "<< lstTimeStruct1.tm_year<< "-"<< std::setfill('0')<< std::setw(2)<< (lstTimeStruct1.tm_mon+ 1)<< "-"<< lstTimeStruct1.tm_mday<< ","<< lstTimeStruct1.tm_hour<< ","<< lstTimeStruct1.tm_min<< ","<< lstTimeStruct1.tm_sec<< std::endl;
        
        if(1 == lstTradeDetails.nStrategyIndentifier)
        {
          //std::cout<< lstTradeDetails.nAvgTradePrice1<< "|"<< lstTradeDetails.nAvgTradePrice2<< "|"<< lstTradeDetails.nAvgTradePrice3 << std::endl;
          lstTradeDetails.nSpread = (lstTradeDetails.nAvgTradePrice3 - lstTradeDetails.nAvgTradePrice2 + lstTradeDetails.nStrikePrice) - lstTradeDetails.nAvgTradePrice1;      
          snprintf(lcScripOrContractName, sizeof(lcScripOrContractName), "%s%hd%s-%d(C)",lpstOMSScripInfo->cSymbol, (lstTimeStruct.tm_year + 1900)%100, MONTH[lstTimeStruct.tm_mon].c_str(), lstTradeDetails.nStrikePrice/100);

          strncpy(lstConRevTradeDetails.cSymbol, lpstOMSScripInfo->cSymbol, SCRIP_SYMBOL_LEN);
          lstConRevTradeDetails.nSpread = lstTradeDetails.nSpread;
          lstConRevTradeDetails.nTotalTradeLot = lstTradeDetails.nTradedVolume1/lpstOMSScripInfo->nBoardLotQty;
          lstConRevTradeDetails.nTimestamp = lnTimestamp;
          lstConRevTradeDetails.nStrikePrice = lstTradeDetails.nStrikePrice;
          lstConRevTradeDetails.nStrategyIndentifier = 1;
          lstConRevTradeDetails.nExpirayDate = lnExpiryDate1;
          lstConRevTradeDetails.nExchangeSequenceNo = lnLastSeqLastSeqNo;   
          lstConRevTradeDetails.nStreamId = stCompositeBcastMsg.stBcastMsg.stHeader.wStremID;          
          
          pcConRevQ->enqueue(lstConRevTradeDetails);
          
          std::cout  <<lpstOMSScripInfo->cSymbol <<","<<lcScripOrContractName<<"," <<lstTradeDetails.nSpread/100.00 <<","<<lstTradeDetails.nTradedVolume1/lpstOMSScripInfo->nBoardLotQty<< "," <<lstTradeDetails.nStrikePrice/100<<"," 
                     <<(lstTradeDetails.nAvgTradePrice1 + lstTradeDetails.nAvgTradePrice2 + lstTradeDetails.nAvgTradePrice3)/3<< ","<< lnFirstSeqLastSeqNo <<","<<lnLastSeqLastSeqNo<<","<< std::setfill('0')
                     << std::setw(2)<< lstTimeStruct1.tm_hour<< ":"<< std::setfill('0')<< std::setw(2)<<lstTimeStruct1.tm_min << ":"<< std::setfill('0')<< std::setw(2)<<lstTimeStruct1.tm_sec<< ","<< lnTimestamp<< "1000000"<< ","<<lnTradeCount << std::endl;                
          //BANKNIFTY17SEP-22000(C)              
        }
        //REVERSAL_STRATEGY   Synthetically buy stock buy call sell put------long position
        else if(2 == lstTradeDetails.nStrategyIndentifier)
        {
          //std::cout<< lstTradeDetails.nAvgTradePrice1<< "|"<< lstTradeDetails.nAvgTradePrice2<< "|"<< lstTradeDetails.nAvgTradePrice3 << std::endl;
          lstTradeDetails.nSpread = lstTradeDetails.nAvgTradePrice1 - (lstTradeDetails.nAvgTradePrice3 - lstTradeDetails.nAvgTradePrice2 + lstTradeDetails.nStrikePrice);            
          snprintf(lcScripOrContractName, sizeof(lcScripOrContractName), "%s%hd%s-%d(R)", lpstOMSScripInfo->cSymbol, (lstTimeStruct.tm_year + 1900)%100, MONTH[lstTimeStruct.tm_mon].c_str(), lstTradeDetails.nStrikePrice/100);              
          
          strncpy(lstConRevTradeDetails.cSymbol, lpstOMSScripInfo->cSymbol, SCRIP_SYMBOL_LEN);
          lstConRevTradeDetails.nSpread = lstTradeDetails.nSpread;
          lstConRevTradeDetails.nTotalTradeLot = lstTradeDetails.nTradedVolume1/lpstOMSScripInfo->nBoardLotQty;
          lstConRevTradeDetails.nTimestamp = lnTimestamp;
          lstConRevTradeDetails.nStrikePrice = lstTradeDetails.nStrikePrice;
          lstConRevTradeDetails.nStrategyIndentifier = 2;  
          lstConRevTradeDetails.nExpirayDate = lnExpiryDate1;    
          lstConRevTradeDetails.nStreamId = stCompositeBcastMsg.stBcastMsg.stHeader.wStremID;
          lstConRevTradeDetails.nExchangeSequenceNo = lnLastSeqLastSeqNo;
          
          pcConRevQ->enqueue(lstConRevTradeDetails);
          
          std::cout <<lpstOMSScripInfo->cSymbol <<","<<lcScripOrContractName<<"," <<lstTradeDetails.nSpread/100.00 <<","<<lstTradeDetails.nTradedVolume1/lpstOMSScripInfo->nBoardLotQty<< "," <<lstTradeDetails.nStrikePrice/100<< "," 
                    << (lstTradeDetails.nAvgTradePrice1 + lstTradeDetails.nAvgTradePrice2 + lstTradeDetails.nAvgTradePrice3)/3<< ","<<lnFirstSeqLastSeqNo <<","<<lnLastSeqLastSeqNo<<","<< std::setfill('0')<< std::setw(2)
                    << lstTimeStruct1.tm_hour<< ":"<< std::setfill('0')<< std::setw(2)<< lstTimeStruct1.tm_min << ":"<< std::setfill('0')<< std::setw(2)<<lstTimeStruct1.tm_sec<< lnTimestamp<< "1000000"<< ","<<lnTradeCount<< std::endl;
          //BANKNIFTY17SEP-22000(R)                            
        }

/*        
       CONREV_TRADE_ITR lcConRevTradeItr = g_pcConRevTradeStore->find(lcScripOrContractName);
       if(lcConRevTradeItr != g_pcConRevTradeStore->end())
       {
         (lcConRevTradeItr->second)->nTotalTradeLot += lstTradeDetails.nTradedVolume1/lpstOMSScripInfo->nBoardLotQty;
       }
       else
       {
          tagTradeDetails* lpstTradeDetails = new tagTradeDetails();
          memset(lpstTradeDetails, 0, sizeof(tagTradeDetails));
         
          lpstTradeDetails->nTotalTradeLot += lstTradeDetails.nTradedVolume1/lpstOMSScripInfo->nBoardLotQty;
          std::pair<CONREV_TRADE_ITR, bool> lcRetValue = g_pcConRevTradeStore->insert(std::make_pair(lcScripOrContractName, lpstTradeDetails));
          if(lcRetValue.second == false)
          {
            std::cout << "insert failed for Token " << lcScripOrContractName << std::endl;
            delete lpstTradeDetails;
          }         
       }
*/
      }

      vTradeMsgStore.clear();
      cTokenStore.clear();
    }
    break;

    case TRADE_MESSAGE:
    {
      TOKENDATA_ITR lcTokenItr = cTokenDataStore.find(stCompositeBcastMsg.stBcastMsg.stTrdMsg.nToken);
      if(lcTokenItr != cTokenDataStore.end())
      {
        int64_t lclot = stCompositeBcastMsg.stBcastMsg.stTrdMsg.nTradeQty / lcTokenItr->second->nBoardLotQty;
        lcTokenItr->second->nTotalTraded += lclot;
      }
      int64_t lnBuyOrderId = (int64_t )stCompositeBcastMsg.stBcastMsg.stTrdMsg.dblBuyOrdID;
      int64_t lnSellOrderId = (int64_t )stCompositeBcastMsg.stBcastMsg.stTrdMsg.dblSellOrdID;        
      if((lnBuyOrderId == 0 && lnSellOrderId == 0) ||
        (lnBuyOrderId != 0 && lnSellOrderId != 0))
      {
        return true;
      }

      cTokenStore.insert(stCompositeBcastMsg.stBcastMsg.stTrdMsg.nToken);
      vTradeMsgStore.push_back(stCompositeBcastMsg);
    }
    break;

    default:
    {

    }
  }  
  return true;
}

bool ValidateTradeMsgRecords(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKEN_STORE& cTokenStoreTemp,  TOKENDATA_STORE& cTokenDataTemp)
{
  bool lbFutToken = false;
  bool lbPutToken = false;
  bool lbCallToken = false;
  
  int lnStrikePrice1 = 0;
  int lnStrikePrice2 = 0;  
  
  int lnExpiryDate1 = 0;
  int lnExpiryDate2 = 0;  
  int lnExpiryDate3 = 0;
  
  char lcFutBuySell = '\0';
  char lcPutBuySell = '\0';
  char lcCallBuySell = '\0';  
        
  bool lbStaus = false;
  OMSScripInfo* lpstOMSScripInfoTemp = NULL;        
  if(!cTokenStoreTemp.empty() && cTokenStoreTemp.size() >= 3)
  {
    for(TOKEN_ITR lcTokenItr = cTokenStoreTemp.begin(); lcTokenItr != cTokenStoreTemp.end(); ++lcTokenItr)
    {
      TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find((*lcTokenItr));
      
      if(lcTokenDataItr != cTokenDataTemp.end())
      {
        lpstOMSScripInfoTemp = (lcTokenDataItr->second);
        switch(lpstOMSScripInfoTemp->nCustomInstrumentType)
        {
          case 1://FUT
          {
          if(lbFutToken && lnExpiryDate1 != lpstOMSScripInfoTemp->nExpiryDate) 
          {
            lbFutToken = true;
          }
          
          lbFutToken = true;
          lnExpiryDate1 = lpstOMSScripInfoTemp->nExpiryDate;            
          }
          break;
          
          case 2:
          {
          lbCallToken = true;
          lnStrikePrice1 = lpstOMSScripInfoTemp->nStrikePrice;
          lnExpiryDate2 = lpstOMSScripInfoTemp->nExpiryDate;                
            
          }
          break;
          
          case 3:
          {
            lbPutToken = true;
            lnStrikePrice2 = lpstOMSScripInfoTemp->nStrikePrice;                  
            lnExpiryDate3 = lpstOMSScripInfoTemp->nExpiryDate;                                            
          }
          break;          
        }
        
        if((lbFutToken == true && lbPutToken == true && lbCallToken == true) && 
          (lnStrikePrice1 == lnStrikePrice2) && (lnExpiryDate3 == lnExpiryDate2 &&  lnExpiryDate2 == lnExpiryDate1))        
        {
          lbStaus = true;
        }
      }
    }
    
    for(ITR lcItr = vTradeMsgStoreTemp.begin(); lcItr != vTradeMsgStoreTemp.end(); ++lcItr)
    {
      int64_t lnBuyOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblBuyOrdID;
      int64_t lnSellOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblSellOrdID;                      

      OMSScripInfo* lpstOMSScripInfoTemp1 = NULL;        
      TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find((lcItr->stBcastMsg.stTrdMsg.nToken));
      if(lcTokenDataItr != cTokenDataTemp.end())
      {
        lpstOMSScripInfoTemp1 = (lcTokenDataItr->second);
      }

      switch(lpstOMSScripInfoTemp1->nCustomInstrumentType)
      {
        case 1://FUT
        {
          if(lnBuyOrderId == 0) lcFutBuySell = BUY_INDICATOR_CHAR;
          if(lnSellOrderId == 0) lcFutBuySell = SELL_INDICATOR_CHAR;                
        }
        break;

        case 2://CALL
        {
          if(lnBuyOrderId == 0) lcCallBuySell = BUY_INDICATOR_CHAR;
          if(lnSellOrderId == 0) lcCallBuySell = SELL_INDICATOR_CHAR;                                
        }
        break;

        case 3://PUT
        {
          if(lnBuyOrderId == 0) lcPutBuySell = BUY_INDICATOR_CHAR;
          if(lnSellOrderId == 0) lcPutBuySell = SELL_INDICATOR_CHAR;                                
        }
        break;              
      }
      
      for(ITR lcItr = vTradeMsgStoreTemp.begin(); lcItr != vTradeMsgStoreTemp.end(); ++lcItr)
      {
        int64_t lnBuyOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblBuyOrdID;
        int64_t lnSellOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblSellOrdID;                      

        OMSScripInfo* lpstOMSScripInfoTemp1 = NULL;        
        TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find((lcItr->stBcastMsg.stTrdMsg.nToken));
        if(lcTokenDataItr != cTokenDataTemp.end())
        {
          lpstOMSScripInfoTemp1 = (lcTokenDataItr->second);
        }

        switch(lpstOMSScripInfoTemp1->nCustomInstrumentType)
        {
          case 1://FUT
          {
            if(lnBuyOrderId == 0) lcFutBuySell = BUY_INDICATOR_CHAR;
            if(lnSellOrderId == 0) lcFutBuySell = SELL_INDICATOR_CHAR;                
          }
          break;

          case 2://CALL
          {
            if(lnBuyOrderId == 0) lcCallBuySell = BUY_INDICATOR_CHAR;
            if(lnSellOrderId == 0) lcCallBuySell = SELL_INDICATOR_CHAR;                                
          }
          break;

          case 3://PUT
          {
            if(lnBuyOrderId == 0) lcPutBuySell = BUY_INDICATOR_CHAR;
            if(lnSellOrderId == 0) lcPutBuySell = SELL_INDICATOR_CHAR;                                
          }
          break;              
        }
      }
      
      if((
          (lcFutBuySell == BUY_INDICATOR_CHAR && lcPutBuySell == BUY_INDICATOR_CHAR && lcCallBuySell == SELL_INDICATOR_CHAR) ||  
          (lcFutBuySell == SELL_INDICATOR_CHAR && lcPutBuySell == SELL_INDICATOR_CHAR && lcCallBuySell == BUY_INDICATOR_CHAR)) && lbStaus)
      {
        return true;
      }
    }          
  }
  
  return false;
}

bool SplitTrade(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKEN_STORE& cTokenStoreTemp,  TOKENDATA_STORE& cTokenDataTemp, TRADE_VECTOR_STORE& vTradeVectorStore)
{
  if(vTradeMsgStoreTemp.size() == 3 &&  cTokenStoreTemp.size() == 3)
  {
    vTradeVectorStore.push_back(vTradeMsgStoreTemp);
    return false;
  }

  TRADE_MSG_STORE lvTradeMsgStoreTemp;  
  
  bool lbFutToken = false;
  bool lbPutToken = false;
  bool lbCallToken = false;
  
  
  TOKEN_STORE  lcTokenStore;
  int32_t lnFutQty = 0;
  int32_t lnCallQty = 0;
  int32_t lnPutQty = 0;  
  
  
  int lnStrikePrice = 0;
  
  int lnLastExpiryDate = 0;  
  int16_t lnLastCustomInstrumentType = 0;
  
  int64_t lnBuyOrderId = 0;
  int64_t lnSellOrderId = 0;                      
  
  char lcFutBuySell = '\0';
  char lcPutBuySell = '\0';
  char lcCallBuySell = '\0';  

  OMSScripInfo* lpstOMSScripInfoTemp1 = NULL;          
  for(ITR lcItr = vTradeMsgStoreTemp.begin(); lcItr != vTradeMsgStoreTemp.end(); ++lcItr)
  {
    lnBuyOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblBuyOrdID;
    lnSellOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblSellOrdID;                              
    
    if((lnBuyOrderId == 0 && lnSellOrderId == 0) ||
      (lnBuyOrderId != 0 && lnSellOrderId != 0))
    {
      lvTradeMsgStoreTemp.clear();
      lcTokenStore.clear();
      lnFutQty = 0;
      lnCallQty = 0;
      lnPutQty = 0;                              
      lnStrikePrice = 0;
      lbFutToken = false;      
      lbPutToken = false;
      lbCallToken = false;
      lnLastExpiryDate = 0;
      lnLastCustomInstrumentType = 0;            
      continue;
    }
    
    TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find((lcItr->stBcastMsg.stTrdMsg.nToken));
    if(lcTokenDataItr != cTokenDataTemp.end())
    {
      lpstOMSScripInfoTemp1 = (lcTokenDataItr->second);
    }

    //std::cout << "8888 lnStrikePrice  " << lnStrikePrice << " "  << lpstOMSScripInfoTemp1->nStrikePrice << " lnLastExpiryDate " <<lnLastExpiryDate << " ExpiryDate " << lpstOMSScripInfoTemp1->nExpiryDate << " " << lcItr->stBcastMsg.stTrdMsg.header.nSeqNo << std::endl;          
    if((lnLastExpiryDate != 0 && lnLastExpiryDate != lpstOMSScripInfoTemp1->nExpiryDate) ||
       (lnStrikePrice != 0 && lnStrikePrice != lpstOMSScripInfoTemp1->nStrikePrice && lpstOMSScripInfoTemp1->nCustomInstrumentType != 1))
    {
      if(lnLastExpiryDate != 0 && lnLastExpiryDate != lpstOMSScripInfoTemp1->nExpiryDate)
      {
        DeletePacketTillExpiryDate(lvTradeMsgStoreTemp, cTokenDataTemp, lpstOMSScripInfoTemp1->nExpiryDate);
      }
      else if(lnStrikePrice != 0 && lnStrikePrice != lpstOMSScripInfoTemp1->nStrikePrice && lpstOMSScripInfoTemp1->nCustomInstrumentType != 1)
      {
        DeletePacketTillStrikePrice(lvTradeMsgStoreTemp, cTokenDataTemp, lpstOMSScripInfoTemp1->nStrikePrice);
      }
      
      lcTokenStore.clear();
      lnFutQty = 0;
      lnCallQty = 0;
      lnPutQty = 0;                              
      lnStrikePrice = 0;
      lbFutToken = false;      
      lbPutToken = false;
      lbCallToken = false;
      lnLastExpiryDate = 0;
      lnLastCustomInstrumentType = 0; 
      
      GetExtraDetails(lvTradeMsgStoreTemp, lcTokenStore, cTokenDataTemp, lnFutQty, lnCallQty, lnPutQty, lnStrikePrice, lbFutToken, lbPutToken, lbCallToken, lnLastExpiryDate, lnLastCustomInstrumentType );

      //std::cout << "7777 " << lnFutQty  << " "  << lnCallQty << " " << lnPutQty << " " <<  lbFutToken  << " " << lbPutToken << " " << lbCallToken << " " << lnLastCustomInstrumentType << std::endl; 
      //std::cout << "9999 lnStrikePrice  " << lnStrikePrice << " "  << lpstOMSScripInfoTemp1->nStrikePrice << " lnLastExpiryDate " <<lnLastExpiryDate << " ExpiryDate " << lpstOMSScripInfoTemp1->nExpiryDate << " " << lcItr->stBcastMsg.stTrdMsg.header.nSeqNo << std::endl;
    }

    lvTradeMsgStoreTemp.push_back(*lcItr);
    lcTokenStore.insert(lcItr->stBcastMsg.stTrdMsg.nToken);              
    switch(lpstOMSScripInfoTemp1->nCustomInstrumentType)
    {
      case 1://FUT
      {
        if((lnLastCustomInstrumentType != 0 && lnLastCustomInstrumentType != lpstOMSScripInfoTemp1->nCustomInstrumentType) &&
            (lbFutToken && (lbPutToken == true || lbCallToken == true)))
        {
          if((lnFutQty == lnCallQty ) && (lnCallQty == lnPutQty) && lcTokenStore.size() == 3)
          {
            vTradeVectorStore.push_back(lvTradeMsgStoreTemp);            
          }
          else
          {
            SecondLevelOrderValidation(lpstOMSScripInfoTemp1->nCustomInstrumentType, lvTradeMsgStoreTemp, cTokenDataTemp);
            ResetExtraDetails(lcTokenStore, lnFutQty, lnCallQty, lnPutQty, lnStrikePrice, lbFutToken, lbPutToken, lbCallToken, lnLastExpiryDate, lnLastCustomInstrumentType);
            GetExtraDetails(lvTradeMsgStoreTemp, lcTokenStore, cTokenDataTemp, lnFutQty, lnCallQty, lnPutQty, lnStrikePrice, lbFutToken, lbPutToken, lbCallToken, lnLastExpiryDate, lnLastCustomInstrumentType);      
            lnFutQty -= lcItr->stBcastMsg.stTrdMsg.nTradeQty;            
          }
        }
        
        lbFutToken = true;
        lnLastExpiryDate  = lpstOMSScripInfoTemp1->nExpiryDate;
        lnLastCustomInstrumentType = lpstOMSScripInfoTemp1->nCustomInstrumentType;
        
        lnFutQty += lcItr->stBcastMsg.stTrdMsg.nTradeQty;
        
        if(lnBuyOrderId == 0) lcFutBuySell = BUY_INDICATOR_CHAR;
        if(lnSellOrderId == 0) lcFutBuySell = SELL_INDICATOR_CHAR;                
      }
      break;

      case 2://CALL
      {
        if((lnLastCustomInstrumentType != 0 && lnLastCustomInstrumentType != lpstOMSScripInfoTemp1->nCustomInstrumentType) &&
          (lbCallToken  && (lbPutToken == true || lbFutToken == true)))
        {
          if((lnFutQty == lnCallQty ) && (lnCallQty == lnPutQty) && lcTokenStore.size() == 3)
          {
            vTradeVectorStore.push_back(lvTradeMsgStoreTemp);            
          }
          else
          {
            SecondLevelOrderValidation(lpstOMSScripInfoTemp1->nCustomInstrumentType, lvTradeMsgStoreTemp, cTokenDataTemp);
            ResetExtraDetails(lcTokenStore, lnFutQty, lnCallQty, lnPutQty, lnStrikePrice, lbFutToken, lbPutToken, lbCallToken, lnLastExpiryDate, lnLastCustomInstrumentType);
            GetExtraDetails(lvTradeMsgStoreTemp, lcTokenStore, cTokenDataTemp, lnFutQty, lnCallQty, lnPutQty, lnStrikePrice, lbFutToken, lbPutToken, lbCallToken, lnLastExpiryDate, lnLastCustomInstrumentType);      
            lnCallQty -= lcItr->stBcastMsg.stTrdMsg.nTradeQty;            
          }
        }        
        
        lbCallToken = true;        
        lnLastExpiryDate = lpstOMSScripInfoTemp1->nExpiryDate;
        lnStrikePrice  = lpstOMSScripInfoTemp1->nStrikePrice;
        lnLastCustomInstrumentType = lpstOMSScripInfoTemp1->nCustomInstrumentType;
        
        lnCallQty += lcItr->stBcastMsg.stTrdMsg.nTradeQty;
        
        if(lnBuyOrderId == 0) lcCallBuySell = BUY_INDICATOR_CHAR;
        if(lnSellOrderId == 0) lcCallBuySell = SELL_INDICATOR_CHAR;                                
      }
      break;

      case 3://PUT
      {
        if((lnLastCustomInstrumentType != 0 && lnLastCustomInstrumentType != lpstOMSScripInfoTemp1->nCustomInstrumentType) &&        
            (lbPutToken && (lbCallToken == true || lbFutToken == true)))
        {
          if((lnFutQty == lnCallQty ) && (lnCallQty == lnPutQty) && lcTokenStore.size() == 3)
          {
            vTradeVectorStore.push_back(lvTradeMsgStoreTemp);            
          }
          else
          {
            SecondLevelOrderValidation(lpstOMSScripInfoTemp1->nCustomInstrumentType, lvTradeMsgStoreTemp, cTokenDataTemp);
            ResetExtraDetails(lcTokenStore, lnFutQty, lnCallQty, lnPutQty, lnStrikePrice, lbFutToken, lbPutToken, lbCallToken, lnLastExpiryDate, lnLastCustomInstrumentType);
            GetExtraDetails(lvTradeMsgStoreTemp, lcTokenStore, cTokenDataTemp, lnFutQty, lnCallQty, lnPutQty, lnStrikePrice, lbFutToken, lbPutToken, lbCallToken, lnLastExpiryDate, lnLastCustomInstrumentType);      
            lnPutQty -= lcItr->stBcastMsg.stTrdMsg.nTradeQty;            
          }
        }                
        
        lbPutToken = true;        
        lnLastExpiryDate  = lpstOMSScripInfoTemp1->nExpiryDate;        
        lnStrikePrice     = lpstOMSScripInfoTemp1->nStrikePrice;   
        lnLastCustomInstrumentType = lpstOMSScripInfoTemp1->nCustomInstrumentType;        
        
        lnPutQty += lcItr->stBcastMsg.stTrdMsg.nTradeQty;
        
        if(lnBuyOrderId == 0) lcPutBuySell = BUY_INDICATOR_CHAR;
        if(lnSellOrderId == 0) lcPutBuySell = SELL_INDICATOR_CHAR;                                
      }
      break;              
    }
    
    if((lnFutQty == lnCallQty ) && (lnCallQty == lnPutQty) && lcTokenStore.size() == 3)
    {
      //std::cout << "Adding," << lnFutQty <<"," << lnCallQty << "," << lnPutQty <<","<<  lcTokenStore.size() <<"," <<lcItr->stBcastMsg.stTrdMsg.header.nSeqNo<<","<<lcItr->stBcastMsg.stTrdMsg.nToken << std::endl;                    
      vTradeVectorStore.push_back(lvTradeMsgStoreTemp);
      lvTradeMsgStoreTemp.clear();
      lcTokenStore.clear();
      lnFutQty = 0;
      lnCallQty = 0;
      lnPutQty = 0;  
      lnStrikePrice = 0;
      lnLastCustomInstrumentType = 0;      
    }
    else if(lnFutQty != 0  && lnCallQty != 0 && lnPutQty != 0 && lcTokenStore.size() == 3) 
    {
      int32_t lnQty1  = 0;
      int32_t lnQty3  = 0;
      
      int32_t lnQty = 0; 
      int32_t lnStartIndex = 0;  
      int32_t lnEndIndex   = 0;
      GetMiddleQty(lnQty, lvTradeMsgStoreTemp, cTokenDataTemp, lnStartIndex, lnEndIndex);  
      
      TRADE_MSG_STORE lvTradeMsgStoreTemp99;  
      
      //First Group
      int32_t lnQtyTemp = 0;               
      for(int32_t lnCount = lnStartIndex - 1; lnCount >= 0 ; --lnCount)
      {
        lvTradeMsgStoreTemp99.push_back(lvTradeMsgStoreTemp[lnCount]);
        
        lnQty1 += lvTradeMsgStoreTemp[lnCount].stBcastMsg.stTrdMsg.nTradeQty;
        if(lnQty1 == lnQty)
          break;
      }
      
      //Second Group
      for(int32_t lnCount = lnStartIndex; lnCount < lnEndIndex; ++lnCount)
      {
        lvTradeMsgStoreTemp99.push_back(lvTradeMsgStoreTemp[lnCount]);
      }      
      
      //Third Group
      lnQtyTemp = 0;     
      for(int32_t lnCount = lnEndIndex; lnCount < lvTradeMsgStoreTemp.size(); ++lnCount)
      {
        lvTradeMsgStoreTemp99.push_back(lvTradeMsgStoreTemp[lnCount]);
        lnQty3 += lvTradeMsgStoreTemp[lnCount].stBcastMsg.stTrdMsg.nTradeQty;
        if(lnQty3 == lnQty)
          break;        
      }    
      
      if(lnQty3 == lnQty1 && lnQty1 == lnQty)
      {
        vTradeVectorStore.push_back(lvTradeMsgStoreTemp99);

        lvTradeMsgStoreTemp.clear();      
        ResetExtraDetails(lcTokenStore, lnFutQty, lnCallQty, lnPutQty, lnStrikePrice, lbFutToken, lbPutToken, lbCallToken, lnLastExpiryDate, lnLastCustomInstrumentType);      
        //std::cout << "DEBUG "  << lnFutQty <<"," << lnCallQty << "," << lnPutQty <<","<<  lcTokenStore.size() <<"," <<lcItr->stBcastMsg.stTrdMsg.header.nSeqNo<<","<<lcItr->stBcastMsg.stTrdMsg.nToken << " " << lnStartIndex << " " << lnEndIndex << " " << lnQty << " " << lvTradeMsgStoreTemp99.size()  << std::endl;                    
      }
    }
  }
  
  return false;
}

bool SecondLevelOrderValidation(int16_t nCustomInstrumentType, TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKENDATA_STORE& cTokenDataTemp)
{
  if(vTradeMsgStoreTemp.empty() || (!vTradeMsgStoreTemp.empty() && vTradeMsgStoreTemp.size() == 1))
  {
    return true;
  }
  
  
  ITR lcBeginItr = vTradeMsgStoreTemp.begin();    
  OMSScripInfo* lpstOMSScripInfoTemp1 = NULL;          
  TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find((lcBeginItr->stBcastMsg.stTrdMsg.nToken));
  if(lcTokenDataItr != cTokenDataTemp.end())
  {
    lpstOMSScripInfoTemp1 = (lcTokenDataItr->second);
  }  

  while(lpstOMSScripInfoTemp1->nCustomInstrumentType == nCustomInstrumentType)
  {
    vTradeMsgStoreTemp.erase(lcBeginItr);
    lcBeginItr = vTradeMsgStoreTemp.begin();        
    lcTokenDataItr = cTokenDataTemp.find((lcBeginItr->stBcastMsg.stTrdMsg.nToken));
    if(lcTokenDataItr != cTokenDataTemp.end())
    {
      lpstOMSScripInfoTemp1 = (lcTokenDataItr->second);
    }      
  }
  
  return false;
}

void DeletePacketTillExpiryDate(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKENDATA_STORE& cTokenDataTemp, int32_t nExpiryDate)
{
  if(vTradeMsgStoreTemp.empty())
    return;
  
  int32_t lnCount = vTradeMsgStoreTemp.size();  
  for(int32_t lnLoopCount = 0; lnLoopCount < lnCount; ++lnLoopCount)
  {
    OMSScripInfo* lpstOMSScripInfoTemp = NULL;          
    for(ITR lcItr = vTradeMsgStoreTemp.begin(); lcItr != vTradeMsgStoreTemp.end(); ++lcItr)
    {
      TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find((lcItr->stBcastMsg.stTrdMsg.nToken));
      if(lcTokenDataItr != cTokenDataTemp.end())
      {
        lpstOMSScripInfoTemp = (lcTokenDataItr->second);
      }  

      if(nExpiryDate != lpstOMSScripInfoTemp->nExpiryDate)
      {
        //std::cout << "Erase1.1,"<< lcItr->stBcastMsg.stTrdMsg.nToken << ","<< lcItr->stBcastMsg.stTrdMsg.header.nSeqNo << std::endl;    
        vTradeMsgStoreTemp.erase(lcItr);
        break;
      }
    }
  }
  
  return;
}

void DeletePacketTillStrikePrice(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKENDATA_STORE& cTokenDataTemp, int32_t nStrikePrice)
{
  if(vTradeMsgStoreTemp.empty())
    return;
  
  int32_t lnCount = vTradeMsgStoreTemp.size();
  for(int32_t lnLoopCount = 0; lnLoopCount < lnCount; ++lnLoopCount)
  {
    OMSScripInfo* lpstOMSScripInfoTemp = NULL;          
    for(ITR lcItr = vTradeMsgStoreTemp.begin(); lcItr != vTradeMsgStoreTemp.end(); ++lcItr)
    {
      TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find((lcItr->stBcastMsg.stTrdMsg.nToken));
      if(lcTokenDataItr != cTokenDataTemp.end())
      {
        lpstOMSScripInfoTemp = (lcTokenDataItr->second);
      }  
      
      if(lpstOMSScripInfoTemp->nCustomInstrumentType == 1)
        return;

      if(nStrikePrice != lpstOMSScripInfoTemp->nStrikePrice)
      {
        //std::cout << "Erase1.2,"<< lcItr->stBcastMsg.stTrdMsg.nToken << ","<< lcItr->stBcastMsg.stTrdMsg.header.nSeqNo << std::endl;            
        vTradeMsgStoreTemp.erase(lcItr);
        break;
      }
    }
  }
  return;
}

bool IsValidateConRev(TRADE_MSG_STORE& vTradeMsgStoreTemp, TOKEN_STORE& cTokenStoreTemp,  TOKENDATA_STORE& cTokenDataTemp)
{
  int32_t lnFutQty = 0;
  int32_t lnCallQty = 0;
  int32_t lnPutQty = 0;  
   
  bool lbFutToken = false;
  bool lbPutToken = false;
  bool lbCallToken = false;
  
  int lnStrikePrice1 = 0;
  int lnStrikePrice2 = 0;  
  
  int lnExpiryDate = 0;
  
  char lcFutBuySell = '\0';
  char lcPutBuySell = '\0';
  char lcCallBuySell = '\0';  

  for(ITR lcItr = vTradeMsgStoreTemp.begin(); lcItr != vTradeMsgStoreTemp.end(); ++lcItr)
  {
    int64_t lnBuyOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblBuyOrdID;
    int64_t lnSellOrderId = (int64_t)lcItr->stBcastMsg.stTrdMsg.dblSellOrdID;                      

    if((lnBuyOrderId == 0 && lnSellOrderId == 0) ||
      (lnBuyOrderId != 0 && lnSellOrderId != 0))
    {
      //std::cout << "case 8" << std::endl;                                           
      return false;
    }

    OMSScripInfo* lpstOMSScripInfoTemp = NULL;        
    TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find((lcItr->stBcastMsg.stTrdMsg.nToken));
    if(lcTokenDataItr != cTokenDataTemp.end())
    {
      lpstOMSScripInfoTemp = (lcTokenDataItr->second);
    }

    switch(lpstOMSScripInfoTemp->nCustomInstrumentType)
    {
      case 1://FUT
      {
        if(lbFutToken && lnExpiryDate != lpstOMSScripInfoTemp->nExpiryDate) 
        {
          //std::cout << "case 7" << std::endl;                                     
          return false;
        }

        lbFutToken = true;
        lnExpiryDate = lpstOMSScripInfoTemp->nExpiryDate;                      

        if(lnBuyOrderId == 0) lcFutBuySell = BUY_INDICATOR_CHAR;
        if(lnSellOrderId == 0) lcFutBuySell = SELL_INDICATOR_CHAR;                
        lnFutQty += lcItr->stBcastMsg.stTrdMsg.nTradeQty;
      }
      break;

      case 2://CALL
      {
        if(lbCallToken && (lnExpiryDate != lpstOMSScripInfoTemp->nExpiryDate || lnStrikePrice1 != lpstOMSScripInfoTemp->nStrikePrice)) 
        {
          //std::cout << "case 6" << std::endl;                         
          return false;
        }          

        lbCallToken = true;
        lnStrikePrice1 = lpstOMSScripInfoTemp->nStrikePrice;
        lnExpiryDate = lpstOMSScripInfoTemp->nExpiryDate;                

        if(lnBuyOrderId == 0) lcCallBuySell = BUY_INDICATOR_CHAR;
        if(lnSellOrderId == 0) lcCallBuySell = SELL_INDICATOR_CHAR;                                

        lnCallQty += lcItr->stBcastMsg.stTrdMsg.nTradeQty;
      }
      break;

      case 3://PUT
      {
        if(lbPutToken && (lnExpiryDate != lpstOMSScripInfoTemp->nExpiryDate || lnStrikePrice2 != lpstOMSScripInfoTemp->nStrikePrice))  
        {
          //std::cout << "case 3" << std::endl;             
          return false;
        }

        lbPutToken = true;
        lnStrikePrice2 = lpstOMSScripInfoTemp->nStrikePrice;                  
        lnExpiryDate = lpstOMSScripInfoTemp->nExpiryDate;                                            

        if(lnBuyOrderId == 0) lcPutBuySell = BUY_INDICATOR_CHAR;
        if(lnSellOrderId == 0) lcPutBuySell = SELL_INDICATOR_CHAR;

        lnPutQty += lcItr->stBcastMsg.stTrdMsg.nTradeQty;          
      }
      break;              
    }
  }

  if((lnPutQty != lnCallQty) || (lnCallQty != lnFutQty))
  {
    //std::cout << "case 1" << std::endl; 
    return false;
  }

  if((!lbPutToken)  || (!lbCallToken) || (!lbFutToken))
  {
    //std::cout << "case 2" << std::endl;       
    return false;
  }    

  if((
      (lcFutBuySell == BUY_INDICATOR_CHAR && lcPutBuySell == BUY_INDICATOR_CHAR && lcCallBuySell == SELL_INDICATOR_CHAR) ||  
      (lcFutBuySell == SELL_INDICATOR_CHAR && lcPutBuySell == SELL_INDICATOR_CHAR && lcCallBuySell == BUY_INDICATOR_CHAR)) )
  {
    return true;
  }
  
  //std::cout << "case 99 " << lcFutBuySell <<  lcPutBuySell << lcCallBuySell << " " << vTradeMsgStoreTemp.size() << std::endl;                                       
  return false;
}


void ResetExtraDetails(TOKEN_STORE& cTokenStoreTemp2, int32_t& nFutQty, int32_t& nCallQty, int32_t& nPutQty, int32_t& nStrikePrice, bool& bFutToken, bool& bPutToken, bool& bCallToken, int32_t& nLastExpiryDate, int16_t& nLastCustomInstrumentType)
{
  cTokenStoreTemp2.clear();
  nFutQty = 0;
  nCallQty = 0;
  nPutQty = 0;                              
  nStrikePrice = 0;
  bFutToken = false;      
  bPutToken = false;
  bCallToken = false;
  nLastExpiryDate = 0;
  nLastCustomInstrumentType = 0;            
}

void GetExtraDetails(TRADE_MSG_STORE& vTradeMsgStoreTemp2, TOKEN_STORE& cTokenStoreTemp2, TOKENDATA_STORE& cTokenDataTemp, int32_t& nFutQty, int32_t& nCallQty, int32_t& nPutQty, int32_t& nStrikePrice, bool& bFutToken, bool& bPutToken, bool& bCallToken, int32_t& nLastExpiryDate, int16_t& nLastCustomInstrumentType)
{
  for(ITR lcItr1 = vTradeMsgStoreTemp2.begin(); lcItr1 != vTradeMsgStoreTemp2.end(); ++lcItr1)
  {      
    cTokenStoreTemp2.insert(lcItr1->stBcastMsg.stTrdMsg.nToken);                                
    TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find(lcItr1->stBcastMsg.stTrdMsg.nToken);
    OMSScripInfo* lpstOMSScripInfoTemp2  = (lcTokenDataItr->second);
    switch(lpstOMSScripInfoTemp2->nCustomInstrumentType)
    {
      case 1://FUT
      {
        bFutToken = true;
        nLastExpiryDate  = lpstOMSScripInfoTemp2->nExpiryDate;
        nLastCustomInstrumentType = lpstOMSScripInfoTemp2->nCustomInstrumentType;

        nFutQty += lcItr1->stBcastMsg.stTrdMsg.nTradeQty;
      }
      break;

      case 2:
      {
        bCallToken = true;        
        nLastExpiryDate = lpstOMSScripInfoTemp2->nExpiryDate;
        nStrikePrice  = lpstOMSScripInfoTemp2->nStrikePrice;
        nLastCustomInstrumentType = lpstOMSScripInfoTemp2->nCustomInstrumentType;

        nCallQty += lcItr1->stBcastMsg.stTrdMsg.nTradeQty;
      }
      break;

      case 3:
      {
        bPutToken = true;        
        nLastExpiryDate  = lpstOMSScripInfoTemp2->nExpiryDate;        
        nStrikePrice     = lpstOMSScripInfoTemp2->nStrikePrice;   
        nLastCustomInstrumentType = lpstOMSScripInfoTemp2->nCustomInstrumentType;        

        nPutQty += lcItr1->stBcastMsg.stTrdMsg.nTradeQty;
      }
      break;
    }
  }
}

bool GetMiddleQty(int32_t& nQty, TRADE_MSG_STORE& vTradeMsgStoreTemp2, TOKENDATA_STORE& cTokenDataTemp, int32_t& nStartIndex, int32_t& nEndIndex )
{
  int32_t lnFutQty  = 0;
  int32_t lnCallQty = 0;
  int32_t lnPutQty  = 0;
  
  int32_t lnLastUpdateQty  = 0;  
  
  int32_t lnCount = 0;
  int32_t lnIndex = 0;  
  
  bool lbFutStatus = false;
  bool lbCallStatus = false;
  bool lbPutStatus = false;  
  
  for(ITR lcItr1 = vTradeMsgStoreTemp2.begin(); lcItr1 != vTradeMsgStoreTemp2.end(); ++lcItr1)
  {      
    TOKENDATA_ITR lcTokenDataItr = cTokenDataTemp.find(lcItr1->stBcastMsg.stTrdMsg.nToken);
    OMSScripInfo* lpstOMSScripInfoTemp2  = (lcTokenDataItr->second);
    switch(lpstOMSScripInfoTemp2->nCustomInstrumentType)
    {
      case 1://FUT
      {
        if(lnCount == 1 && !lbFutStatus ) 
        {
          nStartIndex = lnIndex;
        }
        
        if(lnCount == 2 && !lbFutStatus )
        {
          nQty = lnLastUpdateQty; 
          nEndIndex = lnIndex;
          return true;
        }                    
        
        lnFutQty += lcItr1->stBcastMsg.stTrdMsg.nTradeQty;
        lnLastUpdateQty = lnFutQty;
        if(!lbFutStatus)
        {
          lbFutStatus = true;        
          lnCount++;                     
        }
      }
      break;

      case 2:
      {
        if(lnCount == 1 && !lbCallStatus) 
        {
          nStartIndex = lnIndex;
        }        
        
        if(lnCount == 2 && !lbCallStatus) 
        {
          nQty = lnLastUpdateQty; 
          nEndIndex = lnIndex;          
          return true;
        }
        
        lnCallQty += lcItr1->stBcastMsg.stTrdMsg.nTradeQty;
        lnLastUpdateQty = lnCallQty;
        if(!lbCallStatus)
        {
          lnCount++;   
          lbCallStatus = true;        
        }
      }
      break;

      case 3:
      {
        if(lnCount == 1 && !lbPutStatus) 
        {
          nStartIndex = lnIndex;
        }
        
        if(lnCount == 2 && !lbPutStatus)  
        {
          nQty = lnLastUpdateQty; 
          nEndIndex = lnIndex;                    
          return true;
        }
        
        lnPutQty += lcItr1->stBcastMsg.stTrdMsg.nTradeQty;
        lnLastUpdateQty = lnPutQty;        
        if(!lbPutStatus)        
        {
          lnCount++;   
          lbPutStatus = true;        
        }
      }
      break;
    }
    ++lnIndex;    
  }
  
  return true;
}

void printTrade(TRADE_MSG_STORE& lvTradeStore)
{
  for(ITR iter = lvTradeStore.begin(); iter != lvTradeStore.end(); iter++)
  {
    std::cout<< "TRADE INFO|" <<  "|"<< (*iter).stBcastMsg.stTrdMsg.nToken<< ","<< (*iter).stBcastMsg.stTrdMsg.header.nSeqNo;
  }
}

int FileDigester(const char* pStrFileName, TOKENDATA_STORE& cTokenData)
{
  int nRet = 0;
  FILE *fp = fopen(pStrFileName, "r+");
  if(fp == NULL)
  {
    cout << " Error while opening " << pStrFileName << endl;
    return nRet;
  }  

  int lnCount = 0;
  char* pTemp = NULL;
  size_t sizeTemp = 1024;  
  if(getline(&pTemp,&sizeTemp,fp) != -1) // to remoove the first line containing the file version 
  {
    free(pTemp);
  }
  
  while(!feof(fp))
  {
    char* pLine = NULL;
    size_t sizeLine = 1024;

    if(getline(&pLine, &sizeLine, fp) != -1)
    {
      string strLine(pLine);
      size_t startPoint = 0, endPoint = strLine.length();
      int  i = 1;
      OMSScripInfo* lpstOMSScripInfo = new OMSScripInfo();
      memset(lpstOMSScripInfo, 0, sizeof(OMSScripInfo));
      
      while(i <= 68)//startPoint != string::npos)
      {
        endPoint = strLine.find('|',startPoint);
        if(endPoint != string::npos)
        {
          switch(i)
          {
            case CONTRACT_TOKEN:
              lpstOMSScripInfo->nToken  = atol((strLine.substr(startPoint,endPoint - startPoint)).c_str());
              startPoint = endPoint + 1;
              break;
             
            case CONTRACT_SYMBOL:
              strncpy(lpstOMSScripInfo->cSymbol, (strLine.substr(startPoint, endPoint - startPoint)).c_str(), SCRIP_SYMBOL_LEN);
              startPoint = endPoint + 1;
              break;
             
            case CONTRACT_EXPIRY_DATE:
              lpstOMSScripInfo->nExpiryDate = atoi((strLine.substr(startPoint,endPoint - startPoint)).c_str());
              startPoint = endPoint + 1;
              break; 
              
            case CONTRACT_STRIKE_PRICE:
              lpstOMSScripInfo->nStrikePrice = atoi((strLine.substr(startPoint,endPoint - startPoint)).c_str());
              //std::cout << "SP " << lpstOMSScripInfo->nStrikePrice << std::endl;
              if(lpstOMSScripInfo->nStrikePrice == -1)
              {
                lpstOMSScripInfo->nStrikePrice = 0;
                lpstOMSScripInfo->nCustomInstrumentType = 1;//FUT
              }
              startPoint = endPoint + 1;
              break;
              
            case CONTRACT_CA_LEVEL :
            {
              strncpy(lpstOMSScripInfo->cSeries, (strLine.substr(startPoint,endPoint - startPoint)).c_str(), 2);    
              
              if(0 == strncmp(lpstOMSScripInfo->cSeries, "CE", 2))
              {
                //std::cout << "case 1 " <<  lpstOMSScripInfo->cSeries << std::endl;
                lpstOMSScripInfo->nCustomInstrumentType = 2;//CALL
              }
              
              if(0 == strncmp(lpstOMSScripInfo->cSeries, "PE", 2))
              {
                //std::cout << "case 2 " <<  lpstOMSScripInfo->cSeries << std::endl;                
                lpstOMSScripInfo->nCustomInstrumentType = 3;//PUT
              }
              
              startPoint = endPoint + 1;              
            }
            break;
              
            case CONTRACT_MIN_LOT_QTY:
              lpstOMSScripInfo->nMinLotQty = atoi((strLine.substr(startPoint,endPoint - startPoint)).c_str());
              startPoint = endPoint + 1;
              break;

            case CONTRACT_BROAD_LOT_QTY:
              lpstOMSScripInfo->nBoardLotQty = atoi((strLine.substr(startPoint,endPoint - startPoint)).c_str());
              startPoint = endPoint + 1;
              break;              
              
            case CONTRACT_TICK_SIZE:
              lpstOMSScripInfo->nTickSize = atoi((strLine.substr(startPoint,endPoint - startPoint)).c_str());
              startPoint = endPoint + 1;
              break; 
              
            case CONTRACT_NAME:
              strncpy(lpstOMSScripInfo->cSymbolName, (strLine.substr(startPoint,endPoint - startPoint)).c_str(), 25);              
              startPoint = endPoint + 1;
              nRet++;
              
            default:
              startPoint = endPoint + 1;  
              break; 
          } //switch(i) 
        } //if(endPoint != string::npos)
        i++;
      }//while(i <= 68)
      
      //std::cout << " " << lpstOMSScripInfo->nToken << " " << lpstOMSScripInfo->cSymbolName << " " << lpstOMSScripInfo->nStrikePrice  << " " << lpstOMSScripInfo->cSeries<< " " << lpstOMSScripInfo->nCustomInstrumentType  << std::endl;
      std::pair<TOKENDATA_STORE::iterator, bool> lcRetValue = cTokenData.insert(std::make_pair(lpstOMSScripInfo->nToken, lpstOMSScripInfo));
      if(lcRetValue.second == false)
      {
        std::cout << "insert failed for Token " << lpstOMSScripInfo->nToken << std::endl;
        delete lpstOMSScripInfo;
      }
      
      if(pLine != NULL)
      {free(pLine);}
    } //if(getline(&pLine,&sizeLine,fp)
    else
    {
      //cout << errno << " returned while reading line." << endl; 
    } 
  } //while(!feof(fp)) 
  
  return 1;
} 
