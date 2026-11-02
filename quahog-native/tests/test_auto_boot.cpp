#include "../src/boot_policy.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <chrono>
int main(){
 assert(qboot::needs_memory_setting(64u*1024*1024));
 assert(!qboot::needs_memory_setting(512u*1024*1024));
 assert(!qboot::needs_memory_setting(1024u*1024*1024));
 const std::string old="# user settings\r\nvsync = on\r\nSMS_MEM_MB = 64\r\nother = \xff\r\n";
 auto changed=qboot::memory_settings(old);assert(!changed.already_sufficient);
 assert(changed.updated=="# user settings\r\nvsync = on\r\nSMS_MEM_MB = 1024\r\nother = \xff\r\n");
 assert(qboot::memory_settings(changed.updated).already_sufficient);
 assert(qboot::memory_settings("SMS_MEM_MB = 2048 # keep\n").updated=="SMS_MEM_MB = 2048 # keep\n");
 assert(qboot::memory_settings("SMS_MEM_MB = 64\nSMS_MEM_MB = 1024\nvsync=on").updated=="SMS_MEM_MB = 1024\nvsync=on");
 assert(qboot::memory_settings("vsync=on").updated=="vsync=on\nSMS_MEM_MB = 1024\n");
 assert(qboot::memory_settings("").updated=="SMS_MEM_MB = 1024\n");
 assert(qboot::memory_settings("SMS_MEM_MB=bad").updated=="SMS_MEM_MB = 1024");
 assert(qboot::memory_settings("# SMS_MEM_MB=1\nx=2\n").updated=="# SMS_MEM_MB=1\nx=2\nSMS_MEM_MB = 1024\n");
 assert(qboot::memory_settings("\xef\xbb\xbfSMS_MEM_MB=64\n").updated=="\xef\xbb\xbf\nSMS_MEM_MB = 1024\n");
 try{qboot::memory_settings(std::string("S\0M",3));assert(false);}catch(const std::exception&){}
 auto root=std::filesystem::temp_directory_path()/("quahog-settings-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 auto cwd=root/"build/windows-x86_64";std::filesystem::create_directories(cwd);
 try{qboot::settings_path(cwd,{});assert(false);}catch(const std::exception&){}
 std::ofstream(root/"settings.txt")<<"x=1";
 assert(qboot::settings_path(cwd,{})==root/"settings.txt");
 std::ofstream(cwd/"settings.txt")<<"x=2";
 assert(qboot::settings_path(cwd,{})==cwd/"settings.txt");
 assert(qboot::settings_path(cwd,"selected.txt")==cwd/"selected.txt");
 assert(qboot::settings_path(cwd,root/"explicit.txt")==root/"explicit.txt");
 std::filesystem::remove_all(root);
 assert(qboot::quote_arg(L"G:\\Game Files\\sms.exe")==L"\"G:\\Game Files\\sms.exe\"");
 assert(qboot::quote_arg(L"G:\\Folder with spaces\\")==L"\"G:\\Folder with spaces\\\\\"");
 assert(qboot::quote_arg(L"a\"b")==L"\"a\\\"b\"");
 std::cout<<"Startup policy passed: memory thresholds, settings preservation, duplicates, BOM, invalid data, exact path precedence, argument quoting.\n";
}
