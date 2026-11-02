#include "../src/world_overrides.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
using qworld::Bytes;
static std::map<std::string,std::pair<const void*,uint32_t>> vfs;
static Bytes before(64,'o'),changed(64,'x');
static int writes=0,fail_at=0,allocations=0;
static uint32_t memory=1024u*1024*1024;
static void* read(const char* path,size_t* n){auto i=vfs.find(path);if(i==vfs.end())return nullptr;*n=i->second.second;void* b=std::malloc(*n);std::memcpy(b,i->second.first,*n);++allocations;return b;}
static void release(void* b){--allocations;std::free(b);}
static int replace(const char* path,const void* data,uint32_t n){if(++writes==fail_at)return 0;vfs[path]={data,n};return 1;}
static qworld::Host host(){return {read,replace,release,&memory};}
static bool is_original(){for(const auto& item:vfs)if(item.second.second!=before.size()||std::memcmp(item.second.first,before.data(),before.size()))return false;return true;}
static void fixture(const std::filesystem::path& folder,const char* error=""){
 writes=fail_at=0;vfs.clear();std::filesystem::create_directories(folder);std::memcpy(changed.data(),"RARC",4);
 std::ofstream manifest(folder/"overrides.txt");manifest<<"QUAHOG_NATIVE_OVERRIDES_2\n20 0\n";
 for(int i=0;i<3;++i){std::string path="data/scene/stage"+std::to_string(i)+".szs",file="map"+std::to_string(i)+".arc";vfs["/"+path]={before.data(),uint32_t(before.size())};
  std::ofstream output(folder/file,std::ios::binary);output.write(reinterpret_cast<const char*>(changed.data()),changed.size());
  auto old_hash=qworld::hash(before.data(),before.size()),new_hash=qworld::hash(changed.data(),changed.size());
  if(i==2&&std::string(error)=="iso")old_hash++;
  if(i==2&&std::string(error)=="corrupt")new_hash++;
  manifest<<path<<" "<<file<<" "<<std::hex<<old_hash<<" "<<new_hash<<" "<<std::dec<<before.size()<<"\n";
 }
}
int main(int argc,char** argv){
 assert(argc==2);std::filesystem::path folder=argv[1];fixture(folder);
 for(int cycle=0;cycle<5;++cycle){
  {qworld::Overrides maps(host());maps.install(folder);assert(!is_original());assert(allocations==0);maps.restore();assert(is_original());maps.restore();}
  assert(is_original()); // VFS original pointers remain valid after plugin destruction.
 }
 for(const char* fault:{"iso","corrupt"}){fixture(folder,fault);try{qworld::Overrides maps(host());maps.install(folder);assert(false);}catch(const std::exception&){}assert(writes==0&&is_original()&&allocations==0);}
 fixture(folder);fail_at=2;try{qworld::Overrides maps(host());maps.install(folder);assert(false);}catch(const std::exception&){}assert(is_original()&&allocations==0&&writes==3);
 fixture(folder);memory=64u*1024*1024;try{qworld::Overrides maps(host());maps.install(folder);assert(false);}catch(const std::exception&){}assert(writes==0&&is_original());
 assert(!qworld::safe_relative("../x")&&!qworld::safe_relative("C:/x")&&!qworld::safe_relative("maps/../x")&&qworld::safe_relative("maps/x.arc"));
 assert(qworld::hash("hello",5)==0xa430d84680aabd0bull);
 std::cout<<"VFS tests passed: activation, repeat toggles, lifetime, corrupt map, ISO mismatch, low memory, partial-failure rollback, path safety.\n";
}
