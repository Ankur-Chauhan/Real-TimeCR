#ifndef CONFIGREADER_H
#define CONFIGREADER_H

#include<string>
#include<iostream>
#include<map>

class ConfigReader
{
  std::map<std::string,std::string> _propValMap;

  std::string _filename;
  void parseFile();
  public:
  
  ConfigReader(std::string);
  
  void addProperty(std::string property, std::string value);
  void setProperty(std::string property, std::string value);
  void delProperty(std::string property);
  
  std::string getProperty(std::string property);
  bool getKey(std::string property);  
  bool isValidKey(std::string& property);  
  void reset();
  void dump();
};

#endif
