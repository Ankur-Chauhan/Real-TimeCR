#include<iostream>
#include<time.h>
#include<fstream>
#include <sys/time.h>
#include "log.h"

std::unique_ptr<Logger> Logger::_instance;
std::once_flag Logger::_onceFlag;

Logger::Logger():
    _logLevel(INFO),
    _size(LOGGER_QUEUE_SIZE),
    _readIndex(0),
    _writeIndex(0),
    _exit(false)
{
    assert(LOGGER_QUEUE_SIZE >= 2);
    for(int i=0;i<LOGGER_QUEUE_SIZE;i++)
    {
      _flag[i].store(false);
    }
};

void Logger::setLevel(const int& level)
{
  if( level < 0 || level > PERF)
  {
    return;
  }
  _logLevel = level;
};


Logger::~Logger()
{
  //_exit=true; 
  _bgthread.join();
}

void Logger::log(const int& level, const std::string message)
{
  if(level > PERF)
  {
    return;
  }
  
//  auto const currentWrite = std::atomic_fetch_add(&_writeIndex,(unsigned long long)1);
//  auto queuePos = currentWrite%_size;
//  while(_readIndex + _size <= _writeIndex)
//  {
//    sched_yield();
//  }
//  new (&_records[queuePos]) LogMsg(level, message);
//  _flag[queuePos].store(true);
  
  auto const currentWrite = std::atomic_fetch_add_explicit(&_writeIndex,(unsigned long long)1,std::memory_order_relaxed);      
  auto queuePos = currentWrite%_size;
  while(__builtin_expect(_readIndex.load(std::memory_order_relaxed) + _size <= _writeIndex.load(std::memory_order_relaxed),0))      
  {
    //sched_yield();
  }
  new (&_records[queuePos]) LogMsg(level, message);
  _flag[queuePos].store(true,std::memory_order_relaxed);  
}


void Logger::exit(void)
{
  _exit = true;
}

void Logger::backgroundThread()
{
  std::string lszeTaskeSet("LoggerThrerad: Pinned to Core :" + std::to_string(_nCoreId));   
  TaskSet(_nCoreId, 0,  lszeTaskeSet);  
  std::cout << lszeTaskeSet << std::endl;
  
  time_t now = time(0);
  struct tm tstruct;
  char cfilename[80];
  tstruct = *localtime(&now);
  strftime(cfilename,sizeof(cfilename),"_%Y%m%d",&tstruct);
  std::string filename(cfilename);
  filename = _logFilePath + filename + std::string(".log");
  
  std::ofstream outputFile;
  outputFile.open(filename,std::fstream::app);
  
  if(!outputFile.is_open())
  {
      std::cout<<"ERROR:Failed to open log file :" << filename << std::endl ;
  }

  char lcDateString[32 + 1 ] = {0};  
  struct timeval tv;
  struct tm lcTm; 
  
  LogMsg record;
  while(!_exit)
  {
    
    auto currentRead = std::atomic_fetch_add_explicit(&_readIndex,(unsigned long long)1,std::memory_order_relaxed) % _size;        
    while(!_exit && _flag[currentRead].load(std::memory_order_relaxed) == false)    
    {
      //status = _flag[currentRead].load();
    }    
    record = std::move(_records[currentRead]);
    
    _records[currentRead].~LogMsg();
    _flag[currentRead].store(false,std::memory_order_relaxed);

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &lcTm);
  
//    int lnByteCount = snprintf(lcDateString, sizeof(lcDateString), "%02d-%02d-%02d %02d:%02d:%02d.%03ld",
//																lcTm.tm_mday, lcTm.tm_mon + 1, lcTm.tm_year + 1900,
//																lcTm.tm_hour, lcTm.tm_min, lcTm.tm_sec, tv.tv_usec / 1000);

    int lnByteCount = snprintf(lcDateString, sizeof(lcDateString), "%02d:%02d:%02d.%06ld",
																lcTm.tm_hour, lcTm.tm_min, lcTm.tm_sec, tv.tv_usec);
    
    //outputFile << levels[record._level] << ": " << lcDateString <<": " << record._message <<std::endl<<std::flush;
    outputFile << "[" << levels[record._level] << "] [" << lcDateString <<"] " << record._message <<std::endl<<std::flush;
  }
  auto currentReadIndex = std::atomic_fetch_add(&_readIndex,(unsigned long long)1);
  auto currentRead = currentReadIndex % _size;
  auto status = _flag[currentRead].load();
  while(status != false)
  {
    record = std::move(_records[currentRead]);
    _records[currentRead].~LogMsg();
    _flag[currentRead].store(false);
    
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &lcTm);
  
    int lnByteCount = snprintf(lcDateString, sizeof(lcDateString), "%02d-%02d-%02d %02d:%02d:%02d.%06ld",
																lcTm.tm_mday, lcTm.tm_mon + 1, lcTm.tm_year + 1900,
																lcTm.tm_hour, lcTm.tm_min, lcTm.tm_sec, tv.tv_usec);
    
    
    outputFile << levels[record._level] << ": " << lcDateString << ": " << record._message <<std::endl<<std::flush;
    currentReadIndex = std::atomic_fetch_add(&_readIndex,(unsigned long long)1);
    currentRead = currentReadIndex % _size;
    status = _flag[currentRead].load();
  }
};

void Logger::setDump(const std::string &filePathPrefix/*=""*/, int nCoreId)
{
  _logFilePath = filePathPrefix;
  _nCoreId = nCoreId;
  _bgthread = std::thread(&Logger::backgroundThread, _instance.get());
};

void Logger::setDump(int nCoreId)
{
  _nCoreId = nCoreId;  
  _bgthread = std::thread(&Logger::backgroundThread, _instance.get());
};

Logger& Logger::getLogger()
{
  std::call_once(_onceFlag,[]{_instance.reset(new Logger);});
  return *_instance.get();
};
