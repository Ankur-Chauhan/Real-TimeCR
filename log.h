#ifndef LOG_H_
#define LOG_H_

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <memory>
#include <mutex>
#include <thread>
#include <functional>
#include <sched.h>
#include <iostream>
#include <stdio.h>
#include <stdarg.h>
#include "setaffinity.h"

#define MSG_SIZE 768 //ADDED BY SG on 09092015
#define LOGGER_QUEUE_SIZE 10000

const int TRACE=0, DEBUG=1, INFO=2, ERROR=3, FATAL=4, PERF=5, REVY=6;
const std::string levels[] = {"TRACE","DEBUG","INFO ","ERROR","FATAL", "PERF", "REVY"};


class LogMsg
{
  public:
  char _message[MSG_SIZE];
  uint32_t _level;
  LogMsg(int level, std::string message)
  {
    memset(_message,'\0',MSG_SIZE);
    _level=level;
    strncpy(_message,message.c_str(),MSG_SIZE);
    _message[MSG_SIZE-1]='\0';
  };
  
  template<class... Args>  
  LogMsg(int level, const std::string& format, Args&&... args)
  {
    snprintf(_message, MSG_SIZE, format.c_str(), std::forward<Args>(args)...);    
    
    //WriteToLogFile(std::forward);
    _level=level;
    _message[MSG_SIZE-1]='\0';
  };
  
  LogMsg()
  {
    memset(_message,'\0',MSG_SIZE);
    _level=-1;
  }
  
  ~LogMsg()
  {
    memset(_message,'\0',MSG_SIZE);
    _level=-1;
  }  

  void set(int level, std::string message)
  {
    memset(_message,'\0',MSG_SIZE);
    _level=level;
    strcpy(_message,message.c_str());
    _message[MSG_SIZE-1]='\0';
  };

  void reset()
  {
    memset(_message,'\0',MSG_SIZE);
    _level=-1;
  };
};


class Logger
{
  Logger();
  Logger(const Logger&);
  Logger& operator=(const Logger&);
  
  void backgroundThread();
  
  std::string _logFilePath;
  std::thread _bgthread;

  int _logLevel;
  int _nCoreId;

  static std::unique_ptr<Logger> _instance;
  static std::once_flag _onceFlag;
  
  const uint32_t _size;
  std::atomic<unsigned long long> _readIndex;
  std::atomic<unsigned long long> _writeIndex;
  
  LogMsg _records[LOGGER_QUEUE_SIZE];
  std::atomic<bool> _flag[LOGGER_QUEUE_SIZE];  
    
  bool _exit;

  public:
  ~Logger();
  static Logger& getLogger();
  void setDump(const std::string &filePathPrefix, int nCoreId);
  void setDump(int nCoreId);    
  void setLevel(const int& level);

  template<class... Args>  
  void log(const int& level, const std::string& format, Args&&... args)
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
  new (&_records[queuePos]) LogMsg(level, format, args...);
  _flag[queuePos].store(true,std::memory_order_relaxed);    
  }
  
  void log(const int& level, const std::string message);
  void exit(void);
};

#endif
