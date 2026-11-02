#include "../src/python_install_policy.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
struct Fake {
 std::vector<std::filesystem::path> found;
 size_t index=0;unsigned long code=0;bool fail_download=false,fail_verify=false;
 std::vector<std::string> calls;
 struct Token {~Token(){}};
 std::filesystem::path discover(){calls.push_back("discover");return found.at(index++);}
 std::unique_ptr<Token> acquire(){calls.push_back("lock");return std::make_unique<Token>();}
 std::filesystem::path download(){calls.push_back("download");if(fail_download)throw std::runtime_error("offline");return "/temp/setup.exe";}
 std::unique_ptr<Token> verify(const std::filesystem::path&){calls.push_back("verify");if(fail_verify)throw std::runtime_error("checksum/signature mismatch");return std::make_unique<Token>();}
 unsigned long install(const std::filesystem::path&){calls.push_back("install");return code;}
};
int main(){
 {Fake b;b.found={"/installed/python.exe"};assert(qpython::ensure_with(b)==b.found[0]);assert(b.calls==std::vector<std::string>{"discover"});}
 {Fake b;b.found={"","/other-game/python.exe"};assert(qpython::ensure_with(b)==b.found[1]);assert((b.calls==std::vector<std::string>{"discover","lock","discover"}));}
 for(auto code:{0ul,3010ul}){Fake b;b.found={"","","/new/python.exe"};b.code=code;assert(qpython::ensure_with(b)==b.found[2]);assert((b.calls==std::vector<std::string>{"discover","lock","discover","download","verify","install","discover"}));}
 for(bool hash_failure:{false,true}){
  Fake b;b.found={"",""};b.fail_download=!hash_failure;b.fail_verify=hash_failure;
  bool thrown=false;try{qpython::ensure_with(b);}catch(const std::runtime_error&){thrown=true;}assert(thrown);
  for(const auto& call:b.calls)assert(call!="install");
 }
 for(auto code:{1602ul,1223ul,1603ul,1618ul}){
  Fake b;b.found={"",""};b.code=code;bool thrown=false;
  try{qpython::ensure_with(b);}catch(const std::runtime_error& e){thrown=true;if(code==1602||code==1223)assert(std::string(e.what()).find("cancelled")!=std::string::npos);}
  assert(thrown&&b.index==2);
 }
 for(auto code:{0ul,3010ul}){Fake b;b.found={"","",""};b.code=code;bool thrown=false;
  try{qpython::ensure_with(b);}catch(const std::runtime_error& e){thrown=true;if(code==3010)assert(std::string(e.what()).find("restart")!=std::string::npos);}assert(thrown);
 }
 auto command=qpython::install_command("/temp with spaces/setup.exe","/data with spaces/python-install.log");
 assert(command.find(L"\"/temp with spaces/setup.exe\"")==0);
 for(auto flag:{L"/passive",L"/norestart",L"InstallAllUsers=0",L"Include_launcher=0",L"PrependPath=0",L"AppendPath=0",L"AssociateFiles=0",L"Shortcuts=0",L"/log \"/data with spaces/python-install.log\""})assert(command.find(flag)!=std::wstring::npos);
 assert(command.find(L"/quiet")==std::wstring::npos);assert(std::string(qpython::installer_sha256).size()==64);
 std::cout<<"Automatic Python setup: reuse, lock/recheck, verification gate, install, cancellation, failure, rediscovery and restart policies passed\n";
}
