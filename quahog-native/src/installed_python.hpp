#pragma once
#include "python_policy.hpp"
#ifdef _WIN32
#include <windows.h>
namespace qpython {
class Key {
 HKEY key=nullptr;
public:
 Key(HKEY parent,const std::wstring& name,REGSAM view){if(RegOpenKeyExW(parent,name.c_str(),0,KEY_READ|view,&key)!=ERROR_SUCCESS)key=nullptr;}
 ~Key(){if(key)RegCloseKey(key);}Key(const Key&)=delete;Key& operator=(const Key&)=delete;
 HKEY get()const{return key;}explicit operator bool()const{return key!=nullptr;}
};
inline std::wstring reg_string(HKEY key,const wchar_t* name){
 DWORD type=0,size=0;if(RegQueryValueExW(key,name,nullptr,&type,nullptr,&size)!=ERROR_SUCCESS||type!=REG_SZ||size>65536||size%sizeof(wchar_t))return {};
 std::vector<wchar_t> data(size/sizeof(wchar_t)+1,0);
 if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(data.data()),&size)!=ERROR_SUCCESS||type!=REG_SZ)return {};
 return std::wstring(data.data());
}
inline std::vector<std::wstring> subkeys(HKEY key){
 std::vector<std::wstring> result;
 for(DWORD i=0;i<512;++i){wchar_t name[512];DWORD size=512;auto status=RegEnumKeyExW(key,i,name,&size,nullptr,nullptr,nullptr,nullptr);
  if(status==ERROR_NO_MORE_ITEMS)break;
  if(status==ERROR_SUCCESS)result.emplace_back(name,size);
 }
 return result;
}
struct Candidate {std::filesystem::path path;Version ver;bool official=false;};
inline std::vector<Candidate> registry_candidates(){
 std::vector<Candidate> result;
 for(HKEY hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){
  Key python(hive,L"Software\\Python",view);if(!python)continue;
  for(const auto& company:subkeys(python.get())){
   if(lower(company)==L"pylauncher")continue;
   Key owner(python.get(),company,view);if(!owner)continue;
   for(const auto& tag:subkeys(owner.get())){
    Key entry(owner.get(),tag,view);if(!entry)continue;
    auto text=reg_string(entry.get(),L"SysVersion");auto ver=version(text.empty()?tag:text);if(!supported(ver))continue;
    Key install(entry.get(),L"InstallPath",view);if(!install)continue;
    auto exe=reg_string(install.get(),L"ExecutablePath");std::filesystem::path path;
    if(!exe.empty())path=exe;
    else{auto prefix=reg_string(install.get(),nullptr);if(prefix.empty())continue;path=std::filesystem::path(prefix)/L"python.exe";}
    result.push_back({path,ver,lower(company)==L"pythoncore"});
   }
  }
 }
 // Stable order preserves per-user preference for otherwise equal installs.
 std::stable_sort(result.begin(),result.end(),[](const Candidate& a,const Candidate& b){if(a.official!=b.official)return a.official;return a.ver.minor>b.ver.minor;});
 return result;
}
inline std::wstring environment(const wchar_t* name){
 DWORD size=GetEnvironmentVariableW(name,nullptr,0);if(!size)return {};
 std::wstring value(size,0);DWORD got=GetEnvironmentVariableW(name,value.data(),size);if(!got||got>=size)return {};value.resize(got);return value;
}
inline bool compatible_file(const std::filesystem::path& path,const std::filesystem::path& mod){
 if(!interpreter_file(path,mod))return false;
 DWORD ignored=0,size=GetFileVersionInfoSizeW(path.c_str(),&ignored);if(!size||size>1024*1024)return false;
 std::vector<unsigned char> bytes(size);if(!GetFileVersionInfoW(path.c_str(),0,size,bytes.data()))return false;
 VS_FIXEDFILEINFO* info=nullptr;UINT count=0;
 if(!VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&count)||count<sizeof(*info)||info->dwSignature!=0xfeef04bd)return false;
 return supported({int(HIWORD(info->dwFileVersionMS)),int(LOWORD(info->dwFileVersionMS))});
}
inline std::filesystem::path find(const std::filesystem::path& mod){
 auto configured=mod/L"python-path.txt";
 if(std::filesystem::exists(configured)){
  auto size=std::filesystem::file_size(configured);if(size>32768)throw std::runtime_error("python-path.txt is too large; use one absolute python.exe path");
  std::ifstream file(configured,std::ios::binary);if(!file)throw std::runtime_error("Cannot read python-path.txt");
  std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());auto path=std::filesystem::u8path(one_path_line(text));
  if(!interpreter_file(path,mod))throw std::runtime_error("python-path.txt must point to an existing 64-bit Python executable outside the mod folder. Use Python 3.10 or newer, with no command-line arguments.");
  return path;
 }
 for(const auto& candidate:registry_candidates())if(compatible_file(candidate.path,mod))return candidate.path;
 for(const auto& dir:path_dirs(environment(L"PATH")))for(const auto* name:{L"python.exe",L"python3.exe"}){
  auto path=dir/name;if(compatible_file(path,mod))return path;
 }
 return {}; // Missing runtime: the automatic installer handles this case.
}
}
#endif
