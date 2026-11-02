#pragma once
#include <string>
#include <cstdint>
#include <stdexcept>
#include <filesystem>
#include <vector>
namespace qboot {
constexpr uint32_t minimum_memory=512u*1024*1024;
inline bool needs_memory_setting(uint32_t bytes){return bytes<minimum_memory;}
inline std::wstring quote_arg(const std::wstring& s){
 std::wstring out=L"\"";size_t slash=0;
 for(wchar_t c:s){if(c==L'\\'){++slash;continue;}out.append(c==L'\"'?slash*2+1:slash,L'\\');slash=0;out+=c;}
 out.append(slash*2,L'\\');out+=L'\"';return out;
}
inline std::filesystem::path settings_path(const std::filesystem::path& cwd,const std::filesystem::path& explicit_path){
 // Same order as the game's load_settings(): SMS_SETTINGS, cwd, ../../.
 if(!explicit_path.empty())return explicit_path.is_absolute()?explicit_path:(cwd/explicit_path).lexically_normal();
 for(const auto& p:{cwd/"settings.txt",(cwd/"../../settings.txt").lexically_normal()})
  if(std::filesystem::is_regular_file(p))return p;
 throw std::runtime_error("No active settings.txt was found. Set SMS_MEM_MB = 1024 in the game's settings.txt, then close the game and press Play again.");
}
inline std::string trim(const std::string& s){auto first=s.find_first_not_of(" \t\r\n");return first==std::string::npos?"":s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);}
struct MemorySetting {std::string updated;bool already_sufficient=false;};
inline MemorySetting memory_settings(const std::string& original){
 if(original.size()>1024*1024||original.find('\0')!=std::string::npos)
  throw std::runtime_error("Unsupported settings.txt format; leave it unchanged and set SMS_MEM_MB = 1024 manually.");
 const bool bom=original.compare(0,3,"\xef\xbb\xbf")==0;
 const std::string newline=original.find("\r\n")!=std::string::npos?"\r\n":"\n";
 std::string result=bom?"\xef\xbb\xbf":"";bool found=false;size_t pos=bom?3:0;
 while(pos<original.size()){
  size_t end=original.find('\n',pos);size_t next=end==std::string::npos?original.size():end+1;
  std::string line=original.substr(pos,next-pos),clean=trim(line);auto eq=clean.find('=');
  bool memory=eq!=std::string::npos&&trim(clean.substr(0,eq))=="SMS_MEM_MB";
  if(memory){
   if(!found){
    const bool ignored_bom_line=bom&&pos==3;
    if(ignored_bom_line)result+=newline;
    auto value=trim(clean.substr(eq+1));auto comment=value.find(" #");if(comment!=std::string::npos)value.resize(comment);
    try{size_t n=0;long mb=std::stol(trim(value),&n);if(!ignored_bom_line&&n==trim(value).size()&&mb>=512&&mb<=2048)return {original,true};}catch(const std::exception&){}
    result+="SMS_MEM_MB = 1024";if(end!=std::string::npos)result+=newline;found=true;
   } // Remove duplicate assignments; the game uses the first one.
  }else result+=line;
  pos=next;
 }
 if(!found){if(!result.empty()&&result.back()!='\n')result+=newline;result+="SMS_MEM_MB = 1024"+newline;}
 return {result,false};
}
}
