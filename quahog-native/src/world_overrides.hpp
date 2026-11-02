#pragma once
#include "pe_symbols.hpp"
#include <fstream>
#include <sstream>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <cwchar>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace qworld {
using Bytes=qpe::Bytes;
using Read=void* (*)(const char*,size_t*);
using Override=int (*)(const char*,const void*,uint32_t);
using Free=void (*)(void*);
struct Host {Read read=nullptr;Override replace=nullptr;Free release=nullptr;uint32_t* memory_size=nullptr;};
inline uint64_t hash(const void* data,size_t n){uint64_t h=14695981039346656037ull;auto p=static_cast<const unsigned char*>(data);for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;}
inline Bytes read_file(const std::filesystem::path& path,size_t limit=256u*1024*1024){
 std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("Missing mod file: "+path.u8string());auto n=f.tellg();
 if(n<0||uint64_t(n)>limit)throw std::runtime_error("Invalid mod file size");
 Bytes b(static_cast<size_t>(n));f.seekg(0);if(!b.empty()&&!f.read(reinterpret_cast<char*>(b.data()),n))throw std::runtime_error("Cannot read mod file");return b;
}
inline bool safe_relative(const std::string& p){
 if(p.empty()||p[0]=='/'||p.find('\\')!=std::string::npos||p.find(':')!=std::string::npos)return false;
 std::stringstream s(p);std::string part;while(std::getline(s,part,'/'))if(part.empty()||part=="."||part=="..")return false;return true;
}
#ifdef _WIN32
inline Host resolve_host(){
 wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);if(!n||n>=32768)throw std::runtime_error("Cannot identify game executable");
 Bytes pe=read_file(std::filesystem::path(std::wstring(exe,n)),1024u*1024*1024);
 auto image=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
 using SeparateWorlds=int(*)();auto capability=reinterpret_cast<SeparateWorlds>(GetProcAddress(reinterpret_cast<HMODULE>(image),"SunshineSkinAuto_ExternalWorlds"));
 const bool external_worlds=capability&&capability()==1;
 if(!external_worlds)for(const char* marker:{"wardrobe_v41.ini","[mandy]","skinselector_quahog"})if(std::search(pe.begin(),pe.end(),marker,marker+std::strlen(marker))!=pe.end())throw std::runtime_error("This game already contains the old Skin Selector world hooks. Use a separate clean official 64-bit launcher release for the standalone map mod.");

 auto resolve=[&](const char* name,bool code)->void*{
  if(code)if(auto p=GetProcAddress(reinterpret_cast<HMODULE>(image),name))return reinterpret_cast<void*>(p);
  uint32_t rva=qpe::symbol(pe,name,code);if(!rva)throw std::runtime_error(std::string("Game lacks native symbol ")+name+". Use an official unstripped 64-bit launcher build.");
  void* p=image+rva;MEMORY_BASIC_INFORMATION m={};if(!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT)throw std::runtime_error("Native symbol is not mapped");return p;
 };
 Host h;h.read=reinterpret_cast<Read>(resolve("port_dvd_read_file",true));h.replace=reinterpret_cast<Override>(resolve("port_dvd_override",true));h.memory_size=static_cast<uint32_t*>(resolve("port_mem1_size",false));
 // Free the reader's allocations with the HOST's CRT, never the DLL's CRT.
 auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
 auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
 if(directory.VirtualAddress){auto imp=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+directory.VirtualAddress);
  for(;imp->Name;++imp){if(!imp->OriginalFirstThunk)continue;auto names=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+imp->OriginalFirstThunk);auto addresses=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+imp->FirstThunk);
   for(size_t i=0;names[i].u1.AddressOfData;++i){if(IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal))continue;auto byname=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+names[i].u1.AddressOfData);
    if(std::strcmp(reinterpret_cast<const char*>(byname->Name),"free")==0){h.release=reinterpret_cast<Free>(addresses[i].u1.Function);break;}}
   if(h.release)break;
  }
 }
 if(!h.release)h.release=reinterpret_cast<Free>(resolve("free",true));
 return h;
}
// VFS byte buffers must outlive this DLL when it is disabled through F1.
// One bounded, process-owned cache survives DLL unload/reload; Windows reclaims
// its heap and one retained mapping handle when the game exits.
struct OriginalRecord {char path[160];uint64_t checksum;uint64_t data;uint32_t size;uint32_t unused;};
struct OriginalCache {uint64_t magic;uint64_t image;uint32_t pid,count;uint64_t bytes;OriginalRecord entries[64];};
class Originals {
 OriginalCache* cache=nullptr;
public:
 Originals(){
  wchar_t name[100];std::swprintf(name,100,L"Local\\Randyadr.Quahog.Originals.v2.%lu",GetCurrentProcessId());
  HANDLE map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(OriginalCache),name);if(!map)throw std::runtime_error("Cannot retain original map data");
  bool existing=GetLastError()==ERROR_ALREADY_EXISTS;cache=static_cast<OriginalCache*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(OriginalCache)));
  if(!cache){CloseHandle(map);throw std::runtime_error("Cannot map original data cache");}
  if(existing)CloseHandle(map); // the first handle remains process-owned
  else {std::memset(cache,0,sizeof(*cache));cache->magic=0x514e415449564532ull;cache->image=reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));cache->pid=GetCurrentProcessId();}
  if(cache->magic!=0x514e415449564532ull||cache->pid!=GetCurrentProcessId()||cache->image!=reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr))||cache->count>64)throw std::runtime_error("Invalid original map cache");
 }
 ~Originals(){if(cache)UnmapViewOfFile(cache);}
 const void* keep(const std::string& path,const Bytes& b){
  uint64_t sum=hash(b.data(),b.size());
  for(uint32_t i=0;i<cache->count;++i){auto& r=cache->entries[i];if(path==r.path){if(r.checksum!=sum||r.size!=b.size())throw std::runtime_error("Another mod changed a cached stage: "+path);return reinterpret_cast<const void*>(r.data);}}
  if(path.size()>=160||cache->count==64||cache->bytes+b.size()>512u*1024*1024)throw std::runtime_error("Original map cache capacity exceeded");
  void* copy=HeapAlloc(GetProcessHeap(),0,b.size()?b.size():1);if(!copy)throw std::runtime_error("Cannot preserve original map data");if(!b.empty())std::memcpy(copy,b.data(),b.size());
  auto& r=cache->entries[cache->count++];std::memcpy(r.path,path.c_str(),path.size()+1);r.checksum=sum;r.size=uint32_t(b.size());r.data=reinterpret_cast<uint64_t>(copy);cache->bytes+=b.size();return copy;
 }
};
#else
// Test-process ownership models the Win32 process cache across plugin unloads.
inline Host resolve_host(){throw std::runtime_error("Windows x64 release required");}
class Originals {
public:
 const void* keep(const std::string&,const Bytes& b){static std::vector<std::unique_ptr<Bytes>> originals;originals.emplace_back(new Bytes(b));return originals.back()->data();}
};
#endif
struct Entry {std::string path;Bytes replacement;const void* original=nullptr;const void* prepared=nullptr;uint32_t old_size=0,new_size=0;};
class Overrides {
 Host host;Originals originals;std::vector<Entry> entries;size_t applied=0;
public:
 int area=-1,episode=-1;
 explicit Overrides(Host h):host(h){}
 ~Overrides(){restore();}
 void restore() noexcept {while(applied){auto& e=entries[--applied];host.replace(e.path.c_str(),e.original,e.old_size);}}
 void install(const std::filesystem::path& folder){
  if(!host.read||!host.replace||!host.release||!host.memory_size)throw std::runtime_error("Incomplete host interface");
  if(*host.memory_size<512u*1024*1024)throw std::runtime_error("Map needs at least 512 MB game memory. Set SMS_MEM_MB = 1024 in the game settings, then close the game and press Play again.");
  std::ifstream f(folder/"overrides.txt");if(!f)throw std::runtime_error("Automatic map preparation did not create its manifest. See automatic-setup.log.");
  std::string line;if(!std::getline(f,line)||line!="QUAHOG_NATIVE_OVERRIDES_2")throw std::runtime_error("Invalid map manifest");
  if(!std::getline(f,line))throw std::runtime_error("Missing stage target");
  {std::istringstream s(line);if(!(s>>area>>episode)||(area!=1&&area<16)||area>254||episode<0||episode>254)throw std::runtime_error("Invalid native stage target");}
  while(std::getline(f,line)){
   if(line.empty())continue;
   std::istringstream s(line);std::string path,file,old_hash,new_hash,extra;uint32_t old_size=0;
   if(!(s>>path>>file>>old_hash>>new_hash>>old_size)||(s>>extra)||!safe_relative(path)||!safe_relative(file)||path.rfind("data/scene/",0)!=0||entries.size()>=32)throw std::runtime_error("Invalid map manifest entry");
   uint64_t expected=std::stoull(old_hash,nullptr,16),changed=std::stoull(new_hash,nullptr,16);
   Entry e;e.path="/"+path;
   for(const auto& previous:entries)if(previous.path==e.path)throw std::runtime_error("Duplicate stage override");
   e.replacement=read_file(folder/std::filesystem::u8path(file));
   if(e.replacement.size()<64||std::memcmp(e.replacement.data(),"RARC",4)||hash(e.replacement.data(),e.replacement.size())!=changed)throw std::runtime_error("Damaged prepared map: "+file);
   size_t n=0;void* raw=host.read(e.path.c_str(),&n);if(!raw)throw std::runtime_error("Selected game has no stage "+path);
   Bytes original;try {if(n!=old_size||n>256u*1024*1024||hash(raw,n)!=expected)throw std::runtime_error("ISO or stage data changed; cached maps or a conflicting stage mod must be checked: "+path);original.assign(static_cast<unsigned char*>(raw),static_cast<unsigned char*>(raw)+n);}catch(...){host.release(raw);throw;}
   host.release(raw);e.original=originals.keep(e.path,original);e.old_size=uint32_t(n);
   // In-flight DVD reads may finish after an F1 unload. Both byte buffers are
   // process-owned, so neither contains a pointer into an unloaded DLL heap.
   e.prepared=originals.keep("@prepared"+e.path,e.replacement);e.new_size=uint32_t(e.replacement.size());Bytes().swap(e.replacement);
   entries.push_back(std::move(e));
  }
  if(entries.empty())throw std::runtime_error("Incomplete map manifest");
  // No override is applied until EVERY file has passed the disc and asset checks.
  try {for(auto& e:entries){if(!host.replace(e.path.c_str(),e.prepared,e.new_size))throw std::runtime_error("Cannot activate native stage: "+e.path);++applied;}}catch(...){restore();throw;}
 }
};
}
