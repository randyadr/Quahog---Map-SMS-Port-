#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include "boot_policy.hpp"
namespace qpython {
inline constexpr wchar_t installer_host[]=L"www.python.org";
inline constexpr wchar_t installer_path[]=L"/ftp/python/3.14.10/python-3.14.10-amd64.exe";
inline constexpr char installer_sha256[]="92480239264744fe0386a1bd8275786fefa126e3129e56a68e22b52c73329965";
inline constexpr unsigned long installer_limit=64u*1024*1024;
inline std::wstring install_command(const std::filesystem::path& exe,const std::filesystem::path& log){
 return qboot::quote_arg(exe.wstring())+L" /passive /norestart InstallAllUsers=0 Include_launcher=0 InstallLauncherAllUsers=0 PrependPath=0 AppendPath=0 AssociateFiles=0 Shortcuts=0 Include_doc=0 Include_test=0 Include_pip=0 Include_tcltk=0 /log "+qboot::quote_arg(log.wstring());
}
// The backend holds the installation lock, download and verification handles.
// Verification must finish successfully before install() can be reached.
template<class Backend> std::filesystem::path ensure_with(Backend& backend){
 auto existing=backend.discover();if(!existing.empty())return existing;
 auto lease=backend.acquire();
 existing=backend.discover();if(!existing.empty())return existing;
 auto installer=backend.download();auto verified=backend.verify(installer);
 auto code=backend.install(installer);
 if(code==1602||code==1223)throw std::runtime_error("Python installation was cancelled. Start the game again to retry.");
 if(code!=0&&code!=3010)throw std::runtime_error("Python installation failed (exit "+std::to_string(code)+"). See python-install.log in the mod data folder.");
 existing=backend.discover();
 if(existing.empty())throw std::runtime_error(code==3010?"Python requested a Windows restart. Restart Windows, then launch the game again.":"Python setup finished but no compatible interpreter was found. See python-install.log, or select your python.exe with python-path.txt.");
 return existing;
}
}
