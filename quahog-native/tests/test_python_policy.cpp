#include "../src/python_policy.hpp"
#include <cassert>
#include <iostream>
namespace fs=std::filesystem;
void pe(const fs::path& path,bool x64=true){
 fs::create_directories(path.parent_path());std::vector<unsigned char> bytes(160,0);
 bytes[0]='M';bytes[1]='Z';bytes[60]=128;bytes[128]='P';bytes[129]='E';bytes[132]=x64?0x64:0x4c;bytes[133]=x64?0x86:1;
 std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
int main(int argc,char** argv){
 assert(argc==2);fs::path root=fs::absolute(argv[1]),mod=root/"mod",external=root/"Python 3"/"python.exe";
 fs::create_directories(mod);pe(external);
 assert(qpython::interpreter_file(external,mod));
 pe(mod/"runtime"/"python.exe");assert(!qpython::interpreter_file(mod/"runtime"/"python.exe",mod));
 assert(!qpython::interpreter_file(root/"missing.exe",mod));assert(!qpython::interpreter_file("python.exe",mod));
 pe(root/"32bit.exe",false);assert(!qpython::interpreter_file(root/"32bit.exe",mod));
 std::ofstream(root/"alias.exe");assert(!qpython::interpreter_file(root/"alias.exe",mod));
 fs::create_directory_symlink(mod,root/"linked-mod");assert(!qpython::interpreter_file(root/"linked-mod"/"runtime"/"python.exe",mod));
 pe(root/"mod-other"/"python.exe");assert(qpython::interpreter_file(root/"mod-other"/"python.exe",mod));
 for(auto v:{L"3.10",L"3.14.10",L"3.13-64"})assert(qpython::supported(qpython::version(v)));
 for(auto v:{L"3.9",L"2.7",L"4.0",L"garbage",L"3.",L"3.-1",L"9999999999999999.14"})assert(!qpython::supported(qpython::version(v)));
 assert(qpython::one_path_line("\xef\xbb\xbf  \"C:\\Program Files\\Python\\python.exe\"\r\n")=="C:\\Program Files\\Python\\python.exe");
 for(auto text:{std::string(),std::string("\"\""),std::string("path\nsecond"),std::string("a\0b",3),std::string(32761,'a')}){
  bool rejected=false;try{qpython::one_path_line(text);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
 }
 auto dirs=qpython::path_dirs(L";/one;relative;\"/two with spaces\";;.");
 assert(dirs.size()==2&&dirs[0]=="/one"&&dirs[1]=="/two with spaces");
 std::cout<<"Installed Python policy: passed\n";
}
