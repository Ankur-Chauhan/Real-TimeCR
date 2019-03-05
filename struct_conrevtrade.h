#ifndef _STRUCT_CONREV_TRADE_H_
#define	_STRUCT_CONREV_TRADE_H_

#include "defines_conrevtrade.h"

#pragma pack(1)

struct OMSScripInfo
{
  int32_t                 nExpiryDate;    
  int32_t                 nStrikePrice;    
  int32_t                 nToken; 
  int32_t                 nBoardLotQty;
  int32_t                 nMinLotQty;	
  int32_t                 nTickSize;
  int16_t                 nCustomInstrumentType;
  char                    cSymbol[SCRIP_SYMBOL_LEN + 1];	            
  char                    cSeries[SERIES_ID_LEN + 1];	                
  char                    cSymbolName[TRADING_SYMBOL_NAME + 1]; //empty in case of Spread contract as not received from exchange
  int32_t                 nBoardLotQty2;
  int64_t                 nTotalTraded;
};

struct tagConRevTradeDetails
{
  int64_t                 nTimestamp;    
  int32_t                 nTotalTradeLot;  
  int32_t                 nExpirayDate;
  int32_t                 nSpread;
  int32_t                 nStrikePrice;  
  int32_t                 nExchangeSequenceNo;    
  int16_t                 nStreamId;    
  int16_t                 nStrategyIndentifier;  
  char                    cSymbol[SCRIP_SYMBOL_LEN + 1];  
};

struct tagTradeDetails
{
  int64_t                 nTradedValue1;  
  int64_t                 nTradedValue2;  
  int64_t                 nTradedValue3;    
  
  int32_t                 nTotalTradeLot;
  
  int32_t                 nTradedVolume1;  
  int32_t                 nTradedVolume2;  
  int32_t                 nTradedVolume3;  
  
  int32_t                 nAvgTradePrice1;
  int32_t                 nAvgTradePrice2;
  int32_t                 nAvgTradePrice3;
  
  int32_t                 nSpread;
  int32_t                 nStrikePrice;  
  int16_t                 nStrategyIndentifier;  
  char                    cSymbol[SCRIP_SYMBOL_LEN + 1];	              
};

struct tagPacketControl
{
  int32_t                 nExchangeSequenceNo;    
  int16_t                 nStreamId;    
};

#pragma pack()



#endif
