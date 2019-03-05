#ifndef SHMQUEUE_H_
#define SHMQUEUE_H_

#include<cassert>
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include <string.h>
#include <boost/lockfree/spsc_queue.hpp>
#include <atomic>
#include <mutex>
#include "spsc_atomic.h"
#include "mpsc_atomic.h"

//typedef char[1024] queue_element;

/*
template <class T, int Size = 1000000>
class shmQueue
{
  public:  
  int _shmid;
  key_t _shmKey;
  int _Size;
  char _shmKeyFilePath[1024];
  ProducerConsumerQueue<T, Size>* _ptrQ;
  public:

  shmQueue(const char* shmKeyFilePath,int exists )
  {
    assert(strlen(shmKeyFilePath) <= 1024); // File name to long. Stop being so verbose
    strncpy(_shmKeyFilePath,shmKeyFilePath,1024);

    if((_shmKey = ftok(_shmKeyFilePath,'R')) == -1)
    {
      perror("ftok() didn't work");
      printf("%s\n",_shmKeyFilePath);
      exit(1);
    }

    if((_shmid = shmget(_shmKey, sizeof(ProducerConsumerQueue<T, Size>), 0666| IPC_CREAT)) == -1)
    {
      perror("shmget() didn't work");
      printf("%s\n",_shmKeyFilePath);
      exit(1);
    }
    std::cout << "shmQueue " << shmKeyFilePath << " " << _shmid << std::endl;
    _ptrQ = (ProducerConsumerQueue<T, Size> *)shmat(_shmid, NULL, 0);
    if(_ptrQ == (ProducerConsumerQueue<T, Size> *)(-1))
    {
      perror("shmat() didn't work");
      printf("%s\n",_shmKeyFilePath);
      exit(1);
    }

    if(exists == 1)
    {
      return;
    }
    else
    {
      new (_ptrQ) ProducerConsumerQueue<T, Size>(Size);
    }
  };
  template<class ...Args>
  bool enqueue(Args&&... recordArgs)
  {
    return _ptrQ->enqueue(recordArgs...);
  };

  bool dequeue(T& record)
  {
    return _ptrQ->dequeue(record);
  };

  bool isEmpty()
  {
    return _ptrQ->isEmpty();
  };
  
  void popFront() 
  {
    _ptrQ->popFront();
  }
  
  T*  frontPtr() 
  {
    return _ptrQ->frontPtr();
  }  

};
*/

template <class T, int Size = 1000000>
class shmQueue
{
  public:  
  int _shmid;
  key_t _shmKey;
  int _Size;
  char _shmKeyFilePath[1024];
  ProducerConsumerQueue<T, Size>* _ptrQ;
  public:

  shmQueue(const char* shmKeyFilePath,int exists )
  {
    int lnExists = 0;    
    assert(strlen(shmKeyFilePath) <= 1024); // File name to long. Stop being so verbose
    strncpy(_shmKeyFilePath,shmKeyFilePath,1024);

    if((_shmKey = ftok(_shmKeyFilePath,'R')) == -1)
    {
      std::cout << "ftok() didn't work for file " << _shmKeyFilePath << std::endl;
      return;
    }

    errno = 0;
    if((_shmid = shmget(_shmKey, sizeof(ProducerConsumerQueue<T, Size>), 0666| IPC_CREAT| IPC_EXCL)) == -1)
    {
      if(errno == EEXIST)
      {
        std::cout << " shmget(IPC_CREAT| IPC_EXCL) already create for Keyfile " << shmKeyFilePath << std::endl;                            
        lnExists = 1;
        _shmid = shmget(_shmKey, sizeof(ProducerConsumerQueue<T, Size>), 0666| IPC_CREAT);
      }   
      
      if(_shmid == -1)
      {
        std::cout << "shmQueue shmget Failed : " << _shmKeyFilePath << " [" << std::hex << _shmKey << std::dec << "] : " << strerror(errno) << " ( " << errno << " ) "  << " Object Size " << sizeof(ProducerConsumerQueue<T, Size>) << std::endl;
        return;        
      }
      else
      {
        struct shmid_ds shmidDs={0};
        if(shmctl(_shmid, IPC_STAT, &shmidDs) > -1 )
        {
          if(shmidDs.shm_segsz != sizeof(ProducerConsumerQueue<T, Size>))
          {
            std::cout << "shmQueue " << shmKeyFilePath << " " << _shmid <<" Size Mismatch: shm Size= "<< shmidDs.shm_segsz << " Req Size="<<sizeof(ProducerConsumerQueue<T, Size>) << " Object Size " << sizeof(T) << std::endl;
           //exit(1);
          }
        }
      }
    }
    
    std::cout << "shmQueue " << shmKeyFilePath << " " << _shmid << std::endl;
    _ptrQ = (ProducerConsumerQueue<T, Size> *)shmat(_shmid, NULL, 0);
    if(_ptrQ == (ProducerConsumerQueue<T, Size> *)(-1))
    {
      perror("shmat() didn't work");
      printf("%s\n",_shmKeyFilePath);
      exit(1);
    }

    if(lnExists == 1)
    {
      return;
    }
    else
    {
      new (_ptrQ) ProducerConsumerQueue<T, Size>(Size);
    }
  };
  template<class ...Args>
  bool enqueue(Args&&... recordArgs)
  {
    return _ptrQ->enqueue(recordArgs...);
  };
  
  template<class ...Args>
  void benqueue(Args&&... recordArgs)
  {
    while(!_ptrQ->enqueue(recordArgs...))
    {
      sched_yield();
    }
  };

  bool dequeue(T& record)
  {
    return _ptrQ->dequeue(record);
  };

  bool isEmpty()
  {
    return _ptrQ->isEmpty();
  };
  
  void popFront() 
  {
    _ptrQ->popFront();
  }
  
  T*  frontPtr() 
  {
    return _ptrQ->frontPtr();
  }  
};

template <class T, int Size = 1000000>
class shmQueue1
{
  public:  
  int _shmid;
  key_t _shmKey;
  std::atomic_int _Size;
  char _shmKeyFilePath[1024];
  std::mutex mutex_lock;
  boost::lockfree::spsc_queue<T, boost::lockfree::capacity<Size> >* _ptrQ;
  public:

  shmQueue1(const char* shmKeyFilePath,int exists )
  {
    assert(strlen(shmKeyFilePath) <= 1024); // File name to long. Stop being so verbose
    strncpy(_shmKeyFilePath,shmKeyFilePath,1024);
    _Size = 0;
    if((_shmKey = ftok(_shmKeyFilePath,'R')) == -1)
    {
      printf("ftok failed for key %s\n",_shmKeyFilePath);      
      perror("ftok() didn't work");
      exit(1);
    }

    if((_shmid = shmget(_shmKey, sizeof(boost::lockfree::spsc_queue<T, boost::lockfree::capacity<Size>>), 0666| IPC_CREAT)) == -1)
    {
      perror("shmget() didn't work");
      printf("%s\n",_shmKeyFilePath);
      exit(1);
    }
    std::cout << "shmQueue " << shmKeyFilePath << " " << _shmid << std::endl;
    _ptrQ = (boost::lockfree::spsc_queue<T, boost::lockfree::capacity<Size>>*)shmat(_shmid, NULL, 0);
    if(_ptrQ == (boost::lockfree::spsc_queue<T, boost::lockfree::capacity<Size>> *)(-1))
    {
      perror("shmat() didn't work");
      printf("%s\n",_shmKeyFilePath);
      exit(1);
    }
    if(exists == 1)
    {
      return;
    }
    else
    {
      new (_ptrQ) boost::lockfree::spsc_queue<T, boost::lockfree::capacity<Size>>();// ProducerConsumerQueue<T, Size>(Size);
    }
    /*
    if(shmdt(_ptrQ) == -1)
    {
      perror("shmdt() didn't work");
      exit(1);
    }
    */
  };

  bool enqueue(T const & recordArgs)
  {
    mutex_lock.lock();
    _Size++;
    bool bRet = _ptrQ->push(recordArgs);
    mutex_lock.unlock();
    return bRet;
  };

  bool dequeue(T& record)
  {
    mutex_lock.lock();
    _Size--;
    bool bRet = _ptrQ->pop(record);
    mutex_lock.unlock();
    return bRet;
  };

  bool isEmpty()
  {
    
    return _ptrQ->empty();
  };
  
//  void popFront() 
//  {
//    _ptrQ->
//  }
//  
//  T*  frontPtr() 
//  {
//    return _ptrQ->frontPtr();
//  }  

};

template <class T, int _Size = 1000000>
class shmMPSCQueue
{
  int _shmid;
  key_t _shmKey;
  char _shmKeyFilePath[1024];
  MultipleProducerConsumerQueue<T>* _ptrQ;
  public:

  shmMPSCQueue(const char* shmKeyFilePath,int exists )
  {
    assert(strlen(shmKeyFilePath) <= 1024); // File name to long. Stop being so verbose
    strncpy(_shmKeyFilePath,shmKeyFilePath,1024);

    if((_shmKey = ftok(_shmKeyFilePath,'R')) == -1)
    {
      perror("ftok() didn't work");
      printf("%s\n",_shmKeyFilePath);      
      exit(1);
    }

    if((_shmid = shmget(_shmKey, sizeof(MultipleProducerConsumerQueue<T>), 0666| IPC_CREAT)) == -1)
    {
      perror("shmget() didn't work");
      printf("%s\n",_shmKeyFilePath);
      exit(1);
    }
    
    std::cout << " shmMPSCQueue " << shmKeyFilePath << " " << _shmid << std::endl;    
    _ptrQ = (MultipleProducerConsumerQueue<T> *)shmat(_shmid, NULL, 0);
    if(_ptrQ == (MultipleProducerConsumerQueue<T> *)(-1))
    {
      perror("shmat() didn't work");
      printf("%s\n",_shmKeyFilePath);            
      exit(1);
    }

    std::cout << " _ptrQ " << _ptrQ << std::endl;
    if(exists == 1)
    {
      return;
    }
    else
    {
      new (_ptrQ) MultipleProducerConsumerQueue<T>(_Size);
    }
  };
  
  template<class ...Args>
  bool enqueue(Args&&... recordArgs)
  {
    return _ptrQ->enqueue(recordArgs...);
  };

  bool dequeue(T& record)
  {
    return _ptrQ->dequeue(record);
  };

  bool isEmpty()
  {
    return _ptrQ->isEmpty();
  };
  
  void popFront() 
  {
    _ptrQ->popFront();
  }

};

#endif

