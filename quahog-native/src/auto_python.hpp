#pragma once
#include "installed_python.hpp"
#include "python_install_policy.hpp"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
namespace qpython {
class WinHandle {
 HANDLE value=nullptr;
public:
 explicit WinHandle(HANDLE h=nullptr):value(h){}
 ~WinHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
 WinHandle(const WinHandle&)=delete;WinHandle& operator=(const WinHandle&)=delete;
 WinHandle(WinHandle&& other)noexcept:value(other.value){other.value=nullptr;}
 HANDLE get()const{return value;}explicit operator bool()const{return value&&value!=INVALID_HANDLE_VALUE;}
};
struct Internet {
 HINTERNET value=nullptr;explicit Internet(HINTERNET v):value(v){if(!v)throw std::runtime_error("Cannot open HTTPS connection for Python setup");}
 ~Internet(){WinHttpCloseHandle(value);}Internet(const Internet&)=delete;Internet& operator=(const Internet&)=delete;
};
inline void check(bool success,const char* operation){if(!success)throw std::runtime_error(std::string(operation)+" (Windows error "+std::to_string(GetLastError())+")");}
class Status {
 HWND window=nullptr,label=nullptr;bool cancelled=false;int quit_code=0;bool quitting=false;
public:
 Status(){
  window=CreateWindowExW(WS_EX_DLGMODALFRAME,L"STATIC",L"Quahog - automatic Python setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,560,145,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!window)throw std::runtime_error("Cannot show Python setup progress");
  label=CreateWindowExW(0,L"STATIC",L"Python is missing. Preparing an automatic per-user install...",WS_CHILD|WS_VISIBLE,18,18,510,75,window,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(label)SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
  UpdateWindow(window);
 }
 ~Status(){if(IsWindow(window))DestroyWindow(window);if(quitting)PostQuitMessage(quit_code);}
 void text(const std::wstring& value){if(IsWindow(label)){SetWindowTextW(label,value.c_str());UpdateWindow(label);}}
 bool pump(){
  MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
   if(msg.message==WM_QUIT){quitting=true;quit_code=int(msg.wParam);cancelled=true;continue;}
   TranslateMessage(&msg);DispatchMessageW(&msg);
  }
  if(!IsWindow(window))cancelled=true;
  return !cancelled;
 }
 void require_open(){if(!pump())throw std::runtime_error("Automatic Python setup was cancelled. Launch the game again to retry.");}
};
struct Lease {
 WinHandle mutex;bool held=false;
 explicit Lease(HANDLE h):mutex(h){}
 ~Lease(){if(held)ReleaseMutex(mutex.get());}
 Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
};
inline std::string sha256(HANDLE file){
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot initialize installer SHA-256 verification");
 try{
  if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("Cannot create SHA-256 verifier");
  LARGE_INTEGER zero={};check(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN),"Cannot read installer");
  unsigned char buffer[65536];DWORD count=0;
  for(;;){check(ReadFile(file,buffer,sizeof(buffer),&count,nullptr),"Cannot hash installer");if(!count)break;
   if(BCryptHashData(hash,buffer,count,0)<0)throw std::runtime_error("Cannot hash installer bytes");
  }
  unsigned char digest[32];if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0)throw std::runtime_error("Cannot finish SHA-256 verification");
  BCryptDestroyHash(hash);hash=nullptr;BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;
  static const char hex[]="0123456789abcdef";std::string out;for(auto byte:digest){out+=hex[byte>>4];out+=hex[byte&15];}return out;
 }catch(...){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
inline WinHandle verify_installer(const std::filesystem::path& path){
 // Hold a read-only share across verification AND installation so another
 // process cannot rewrite/delete the executable after we have checked it.
 WinHandle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
 check(bool(file),"Cannot open downloaded Python installer");
 if(sha256(file.get())!=installer_sha256)throw std::runtime_error("Python installer checksum mismatch. Nothing was installed; launch again to download a fresh copy.");
 WINTRUST_FILE_INFO info={};info.cbStruct=sizeof(info);info.pcwszFilePath=path.c_str();info.hFile=file.get();
 WINTRUST_DATA data={};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&info;data.dwStateAction=WTD_STATEACTION_VERIFY;
 GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG result=WinVerifyTrust(nullptr,&action,&data);
 data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);
 if(result!=ERROR_SUCCESS)throw std::runtime_error("Windows could not verify the official Python installer's signature (code "+std::to_string(result)+"). Check internet access and Windows date/time, then retry.");
 return file;
}
inline void download_installer(const std::filesystem::path& path,std::atomic<bool>& cancelled,std::atomic<unsigned long>& received){
 Internet session(WinHttpOpen(L"Quahog/0.4.4 PythonSetup",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
 check(WinHttpSetTimeouts(session.value,15000,15000,15000,15000),"Cannot set download timeout");
 Internet connection(WinHttpConnect(session.value,installer_host,INTERNET_DEFAULT_HTTPS_PORT,0));
 Internet request(WinHttpOpenRequest(connection.value,L"GET",installer_path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
 DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
 check(WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)),"Cannot enforce download origin");
 check(WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0),"Cannot download Python from python.org");
 check(WinHttpReceiveResponse(request.value,nullptr),"Cannot receive Python download");
 DWORD code=0,length=sizeof(code);check(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&length,WINHTTP_NO_HEADER_INDEX),"Cannot check download status");
 if(code!=200)throw std::runtime_error("Python download returned HTTP "+std::to_string(code)+". Nothing was installed.");
 WinHandle output(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));check(bool(output),"Cannot save Python installer");
 unsigned char buffer[65536];DWORD count=0;unsigned long total=0;
 while(!cancelled.load()){
  check(WinHttpReadData(request.value,buffer,sizeof(buffer),&count),"Python download was interrupted");if(!count)break;
  if(count>installer_limit-total)throw std::runtime_error("Python installer download exceeded the size limit");
  DWORD written=0;check(WriteFile(output.get(),buffer,count,&written,nullptr)&&written==count,"Cannot write Python installer");total+=count;received.store(total);
 }
 if(cancelled.load())throw std::runtime_error("Python download was cancelled");
 if(total==0)throw std::runtime_error("Python download was empty");
 check(FlushFileBuffers(output.get()),"Cannot finish Python download");
}
class Installer {
 std::filesystem::path mod,data,temporary;std::unique_ptr<Status> status;
 void log(const std::string& line){std::ofstream out(data/"python-bootstrap.log",std::ios::app);out<<line<<'\n';}
public:
 Installer(const std::filesystem::path& m,const std::filesystem::path& d):mod(m),data(d){}
 ~Installer(){if(!temporary.empty()){std::error_code ignored;std::filesystem::remove_all(temporary,ignored);}}
 std::filesystem::path discover(){return find(mod);}
 std::unique_ptr<Lease> acquire(){
  std::filesystem::create_directories(data);log("Quahog 0.4.4: no compatible Python found; automatic setup starting.");
  status=std::make_unique<Status>();status->text(L"Waiting for Python setup, if another game is installing it...");
  auto lease=std::make_unique<Lease>(CreateMutexW(nullptr,FALSE,L"Local\\Randyadr.Quahog.PythonInstall"));check(bool(lease->mutex),"Cannot acquire Python setup lock");
  for(;;){status->require_open();DWORD result=WaitForSingleObject(lease->mutex.get(),100);
   if(result==WAIT_OBJECT_0||result==WAIT_ABANDONED){lease->held=true;return lease;}
   if(result!=WAIT_TIMEOUT)throw std::runtime_error("Cannot wait for Python setup lock");
  }
 }
 std::filesystem::path download(){
  status->require_open();
  for(int attempt=0;attempt<8;++attempt){
   unsigned char random[16];if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create private setup directory");
   std::wstring name=L"quahog-python-";const wchar_t* hex=L"0123456789abcdef";for(auto b:random){name+=hex[b>>4];name+=hex[b&15];}
   auto dir=std::filesystem::temp_directory_path()/name;if(std::filesystem::create_directory(dir)){temporary=dir;break;}
  }
  if(temporary.empty())throw std::runtime_error("Cannot create Python download directory");
  auto file=temporary/L"python-3.14.10-amd64.exe";std::atomic<bool> cancelled{false};std::atomic<unsigned long> received{0};
  log("Downloading https://www.python.org/ftp/python/3.14.10/python-3.14.10-amd64.exe");
  auto task=std::async(std::launch::async,[&]{download_installer(file,cancelled,received);});
  while(task.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready){
   if(!status->pump())cancelled.store(true);
   status->text(L"Downloading official Python 3.14.10 (64-bit) from python.org...\n"+std::to_wstring(received.load()/1024)+L" KB received. Close this window to cancel.");
  }
  task.get();status->require_open();return file;
 }
 WinHandle verify(const std::filesystem::path& file){
  status->text(L"Verifying Python's SHA-256 checksum and Windows signature...");status->require_open();
  auto task=std::async(std::launch::async,[&]{return verify_installer(file);});
  while(task.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)status->pump();
  auto verified=task.get();status->require_open();log("Installer checksum and Authenticode verification passed.");return verified;
 }
 unsigned long install(const std::filesystem::path& file){
  status->require_open();status->text(L"Installing Python for your Windows account...\nThe official installer shows progress. Map setup continues afterward.");
  auto command=install_command(file,data/L"python-install.log");std::vector<wchar_t> args(command.begin(),command.end());args.push_back(0);
  STARTUPINFOW startup={};startup.cb=sizeof(startup);PROCESS_INFORMATION process={};
  check(CreateProcessW(file.c_str(),args.data(),nullptr,nullptr,FALSE,0,nullptr,temporary.c_str(),&startup,&process),"Cannot start verified Python installer");
  WinHandle child(process.hProcess),thread(process.hThread);bool cancelled=false;
  for(;;){if(!status->pump())cancelled=true;DWORD result=WaitForSingleObject(child.get(),100);
   if(result==WAIT_OBJECT_0)break;
   if(result!=WAIT_TIMEOUT)throw std::runtime_error("Cannot wait for Python installation");
  }
  DWORD code=1;check(GetExitCodeProcess(child.get(),&code),"Cannot read Python installer result");log("Python installer exit: "+std::to_string(code));
  // Never forcibly terminate an MSI install. Closing the game/window stops
  // map startup after the official installer has finished or been cancelled.
  if(cancelled)throw std::runtime_error("Quahog startup was cancelled. Python setup has finished; launch the game again when ready.");
  return code;
 }
};
inline std::filesystem::path ensure(const std::filesystem::path& mod,const std::filesystem::path& data){
 try{Installer backend(mod,data);return ensure_with(backend);}
 catch(const std::exception& error){std::ofstream out(data/"python-bootstrap.log",std::ios::app);out<<"ERROR: "<<error.what()<<'\n';throw;}
}
}
#endif
