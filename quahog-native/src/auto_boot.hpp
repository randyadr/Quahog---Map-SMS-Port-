#pragma once
#include "boot_policy.hpp"
#include "world_overrides.hpp"
#ifdef _WIN32
#include <utility>
#include "auto_python.hpp"
namespace qboot {
class Handle {
 HANDLE h=nullptr;
public:
 Handle()=default;explicit Handle(HANDLE v):h(v){}
 Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
 Handle(Handle&& other) noexcept:h(other.h){other.h=nullptr;}
 Handle& operator=(Handle&& other) noexcept {if(this!=&other){reset();h=other.h;other.h=nullptr;}return *this;}
 ~Handle(){reset();}void reset(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
 HANDLE get()const{return h;}explicit operator bool()const{return h&&h!=INVALID_HANDLE_VALUE;}
};
inline std::wstring env(const wchar_t* key){DWORD n=GetEnvironmentVariableW(key,nullptr,0);if(!n)return {};std::wstring out(n,L'\0');DWORD got=GetEnvironmentVariableW(key,out.data(),n);if(!got||got>=n)throw std::runtime_error("Cannot read launcher environment");out.resize(got);return out;}
inline std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8 startup message");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
inline void write_new(const std::filesystem::path& path,const qworld::Bytes& bytes){
 Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
 if(!file)throw std::runtime_error("Cannot create settings backup or temporary file: "+path.u8string());
 DWORD n=0;if(!WriteFile(file.get(),bytes.data(),DWORD(bytes.size()),&n,nullptr)||n!=bytes.size()||!FlushFileBuffers(file.get()))throw std::runtime_error("Cannot finish writing "+path.u8string());
}
inline void configure_memory(uint32_t current,bool in_stage,const std::filesystem::path& data){
 if(!needs_memory_setting(current))return;
 if(in_stage)throw std::runtime_error("Quahog needs more map memory. Close the game and press Play with Quahog enabled before loading a stage.");
 const auto file=settings_path(std::filesystem::current_path(),std::filesystem::path(env(L"SMS_SETTINGS")));
 if(std::filesystem::is_symlink(file))throw std::runtime_error("The game settings path is a symbolic link. Set SMS_MEM_MB = 1024 manually, then restart the game.");
 auto before=qworld::read_file(file,1024u*1024);auto changed=memory_settings(std::string(before.begin(),before.end()));
 if(changed.already_sufficient)throw std::runtime_error("Quahog still has less than 512 MB although settings.txt already requests enough memory. Close and reopen SMS Launcher, and check any SMS_MEM_MB environment override. Settings left unchanged.\nSettings: "+file.u8string());
 std::filesystem::create_directories(data);
 auto backup=data/("settings-before-quahog-"+std::to_string(qworld::hash(before.data(),before.size()))+".txt");
 if(std::filesystem::exists(backup)){
  if(qworld::read_file(backup,1024u*1024)!=before)throw std::runtime_error("Existing memory-settings backup differs; settings left unchanged.");
 }else write_new(backup,before);
 auto temporary=file;temporary+=L".quahog-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
 qworld::Bytes after(changed.updated.begin(),changed.updated.end());write_new(temporary,after);
 try{
  if(qworld::read_file(file,1024u*1024)!=before)throw std::runtime_error("Game settings changed during preparation; leaving them unchanged. Close the game and retry.");
  if(!ReplaceFileW(file.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr))throw std::runtime_error("Cannot update game memory setting (Windows error "+std::to_string(GetLastError())+"). Set SMS_MEM_MB = 1024 manually.\nSettings: "+file.u8string());
 }catch(...){std::error_code ignore;std::filesystem::remove(temporary,ignore);throw;}
 throw std::runtime_error("Quahog saved SMS_MEM_MB = 1024 in the game's settings. Close the game and press Play once more. This build does not restart or hide the game.\nSettings: "+file.u8string()+"\nBackup: "+backup.u8string());
}
struct Child {Handle process,thread;};
inline Child start_preparation(const std::filesystem::path& python,const std::wstring& command){
 // Only the user-installed Python interpreter is launched, in a visible console.
 // No game relaunch, inherited handles, suspended thread, job or hidden window.
 STARTUPINFOW startup={};startup.cb=sizeof(startup);wchar_t title[]=L"Quahog map preparation";startup.lpTitle=title;
 PROCESS_INFORMATION process={};std::vector<wchar_t> cmd(command.begin(),command.end());cmd.push_back(0);
 if(!CreateProcessW(python.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NEW_CONSOLE,nullptr,nullptr,&startup,&process))throw std::runtime_error("Cannot start map preparation (Windows error "+std::to_string(GetLastError())+")");
 Child child;child.process=Handle(process.hProcess);child.thread=Handle(process.hThread);return child;
}
inline DWORD wait_preparation(Child& child){
 bool closing=false;int quit_code=0;
 for(;;){
  HANDLE process=child.process.get();DWORD status=MsgWaitForMultipleObjects(1,&process,FALSE,250,QS_ALLINPUT);
  if(status==WAIT_OBJECT_0){DWORD code=1;GetExitCodeProcess(process,&code);if(closing){PostQuitMessage(quit_code);throw std::runtime_error("Game closed during preparation. No map overrides were installed.");}return code;}
  if(status==WAIT_FAILED)throw std::runtime_error("Cannot wait for map preparation");
  MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
   if(message.message==WM_QUIT){closing=true;quit_code=int(message.wParam);continue;}
   TranslateMessage(&message);DispatchMessageW(&message);
  }
 }
}
inline std::filesystem::path prepare_maps(const std::filesystem::path& mod,const std::filesystem::path& data,bool force=false){
 const auto disc=env(L"SMS_DISC_IMAGE");if(disc.empty())throw std::runtime_error("Start this mod through SMS Launcher so it can detect the active ISO automatically.");
 auto cache=data/L"prepared-v4";std::filesystem::create_directories(cache);
 auto result=cache/(L"active-"+std::to_wstring(GetCurrentProcessId())+L".txt");std::error_code ignore;std::filesystem::remove(result,ignore);
 auto log_path=data/L"automatic-setup.log";
 auto python=qpython::ensure(mod,data),script=mod/L"tools/boot_prepare.py";
 if(!std::filesystem::is_regular_file(script))throw std::runtime_error("The mod ZIP is incomplete. Extract the entire mod folder, including tools.");
 std::wstring command=quote_arg(python.wstring())+L" -I -u "+quote_arg(script.wstring())+L" --mod "+quote_arg(mod.wstring())+L" --disc "+quote_arg(disc)+L" --cache "+quote_arg(cache.wstring())+L" --result "+quote_arg(result.wstring())+L" --log "+quote_arg(log_path.wstring())+(force?L" --force":L"");
 Child child=start_preparation(python,command);DWORD code=wait_preparation(child);
 if(code){std::string detail="Preparation failed or its window was closed (exit "+std::to_string(code)+")";
  try{auto bytes=qworld::read_file(log_path,4u*1024*1024);std::string text(bytes.begin(),bytes.end());auto at=text.rfind("[Native map] ERROR:");if(at!=std::string::npos){detail=text.substr(at);auto end=detail.find('\n');if(end!=std::string::npos)detail.resize(end);}}catch(const std::exception&){}
  throw std::runtime_error(detail+"\nRequires installed 64-bit Python 3.10 or newer.\nPython: "+python.u8string()+"\nLog: "+log_path.u8string());
 }
 auto bytes=qworld::read_file(result,32768);std::string path(bytes.begin(),bytes.end());while(!path.empty()&&(path.back()=='\n'||path.back()=='\r'))path.pop_back();
 auto prepared=std::filesystem::u8path(path);auto relative=prepared.lexically_relative(cache);
 if(relative.empty()||relative.is_absolute())throw std::runtime_error("Invalid prepared-map path");
 for(const auto& part:relative)if(part==L"..")throw std::runtime_error("Prepared-map path escaped its cache");
 std::filesystem::remove(result,ignore);return prepared;
}
inline void show_error(const std::string& message){auto body=wide(message);MessageBoxW(nullptr,body.c_str(),L"Quahog startup",MB_OK|MB_ICONINFORMATION);}
}
#endif
