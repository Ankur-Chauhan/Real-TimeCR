#ifndef _DEFINES_CONREV_TRADE_H_
#define	_DEFINES_CONREV_TRADE_H_


const int32_t DEALER_ID_LEN         = 15;
const int32_t SERIES_ID_LEN         = 2;
const int32_t TRADING_SYMBOL_NAME   = 25;
const int32_t SCRIP_SYMBOL_LEN      = 10;
const int32_t MAX_SEND_BUFFER_LEN   = 256;
const int32_t MAGIC_KEY_LEN         = 64;  

enum NSEFO_CONTRACT_FILE_INDEX
{
  CONTRACT_TOKEN                    = 1,				
  CONTRACT_SYMBOL                   = 4,			
  CONTRACT_SERIES                   = 5,					
  CONTRACT_EXPIRY_DATE              = 7,					
  CONTRACT_STRIKE_PRICE             = 8,					
  CONTRACT_CA_LEVEL                 = 9,                                
  CONTRACT_MIN_LOT_QTY              = 31,			
  CONTRACT_BROAD_LOT_QTY            = 32,					
  CONTRACT_TICK_SIZE                = 33,						
  CONTRACT_NAME                     = 54,								
};

enum CONNECTION_STATUS : int16_t
{
  CONNECTED       = 1,
  NOT_CONNECTED   = 2,  
};

enum ENUM_MESSAGE_TYPE
{
  ENUM_LOGIN_REQUEST                = 101,
  ENUM_LOGIN_RESPONSE               = 102,
  ENUM_RECOVERY_REQUEST             = 103,
  ENUM_START_OF_DOWNLOAD            = 104,
  ENUM_CONREV_TRADE_DOWNLOAD        = 105,
  ENUM_END_OF_DOWNLOAD              = 106,
  ENUM_LOGOUT_REQUEST               = 107,
  ENUM_LOGOUT_RESPONSE              = 108,
  ENUM_CONREV_TRADE_INFO            = 117,
};


#endif
