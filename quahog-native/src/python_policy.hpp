#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace qpython {
struct Version {int major=0,minor=0;};
inline Version version(const std::wstring& text){
 try{size_t end=0;int major=std::stoi(text,&end);if(end>=text.size()||text[end]!=L'.')return {};
  size_t used=0;auto tail=text.substr(end+1);int minor=std::stoi(tail,&used);
  if(used==0||major<0||minor<0)return {};
  return {major,minor};
 }catch(const std::exception&){return {};}
}
inline bool supported(Version v){return v.major==3&&v.minor>=10;}
inline std::wstring lower(std::wstring value){for(auto& ch:value)ch=std::towlower(ch);return value;}
inline bool within(const std::filesystem::path& child,const std::filesystem::path& parent){
 std::error_code ec;auto a=std::filesystem::weakly_canonical(child,ec);if(ec)return true;
 auto b=std::filesystem::weakly_canonical(parent,ec);if(ec)return true;
 auto i=a.begin();for(auto j=b.begin();j!=b.end();++j,++i)if(i==a.end()||lower(i->wstring())!=lower(j->wstring()))return false;
 return true;
}
inline bool interpreter_file(const std::filesystem::path& path,const std::filesystem::path& mod){
 // Inspect bytes without running anything. Skip Store aliases, missing or
 // 32-bit executables, and any embedded runtime left in the mod folder.
 if(!path.is_absolute()||within(path,mod))return false;
 std::ifstream input(path,std::ios::binary);unsigned char dos[64]={};
 if(!input.read(reinterpret_cast<char*>(dos),sizeof(dos))||dos[0]!='M'||dos[1]!='Z')return false;
 uint64_t offset=uint64_t(dos[60])|(uint64_t(dos[61])<<8)|(uint64_t(dos[62])<<16)|(uint64_t(dos[63])<<24);
 if(offset<64||offset>16u*1024*1024)return false;
 input.seekg(std::streamoff(offset));unsigned char pe[6]={};
 return bool(input.read(reinterpret_cast<char*>(pe),sizeof(pe)))&&pe[0]=='P'&&pe[1]=='E'&&pe[2]==0&&pe[3]==0&&pe[4]==0x64&&pe[5]==0x86;
}
inline std::vector<std::filesystem::path> path_dirs(const std::wstring& raw){
 std::vector<std::filesystem::path> out;size_t start=0;
 while(start<=raw.size()){
  auto end=raw.find(L';',start);auto value=raw.substr(start,end==std::wstring::npos?end:end-start);
  if(value.size()>=2&&value.front()==L'"'&&value.back()==L'"')value=value.substr(1,value.size()-2);
  std::filesystem::path dir(value);if(dir.is_absolute())out.push_back(dir);
  if(end==std::wstring::npos)break;
  start=end+1;
 }
 return out;
}
inline std::string one_path_line(std::string value){
 if(value.size()>=3&&value.substr(0,3)=="\xef\xbb\xbf")value.erase(0,3);
 auto first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");
 if(first==std::string::npos)throw std::runtime_error("python-path.txt is empty");
 value=value.substr(first,last-first+1);
 if(value.size()>=2&&value.front()=='"'&&value.back()=='"')value=value.substr(1,value.size()-2);
 if(value.empty()||value.size()>32760||value.find_first_of("\r\n")!=std::string::npos||value.find('\0')!=std::string::npos)
  throw std::runtime_error("python-path.txt must contain just one absolute python.exe path, without arguments");
 return value;
}
}
