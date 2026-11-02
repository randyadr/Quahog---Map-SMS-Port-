// Read-only PE/COFF symbol resolution. No offsets are guessed or patched.
#pragma once
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <cstring>
namespace qpe {
using Bytes=std::vector<unsigned char>;
inline void need(const Bytes& b,size_t p,size_t n){if(p>b.size()||n>b.size()-p)throw std::runtime_error("Truncated PE metadata");}
inline uint16_t u16(const Bytes& b,size_t p){need(b,p,2);return uint16_t(b[p])|(uint16_t(b[p+1])<<8);}
inline uint32_t u32(const Bytes& b,size_t p){return uint32_t(u16(b,p))|(uint32_t(u16(b,p+2))<<16);}
inline std::string text(const Bytes& b,size_t p,size_t end){if(end>b.size())end=b.size();need(b,p,0);size_t q=p;while(q<end&&b[q])++q;if(q==end)throw std::runtime_error("Unterminated PE string");return std::string(reinterpret_cast<const char*>(b.data()+p),q-p);}
inline uint32_t symbol(const Bytes& b,const std::string& wanted,bool code){
 if(b.size()<64||u16(b,0)!=0x5a4d)throw std::runtime_error("Not a Windows executable");
 size_t pe=u32(b,60);need(b,pe,24);
 if(u32(b,pe)!=0x4550||u16(b,pe+4)!=0x8664)throw std::runtime_error("This mod requires a 64-bit Windows game");
 size_t nsec=u16(b,pe+6),table=u32(b,pe+12),count=u32(b,pe+16),optional=pe+24,sections=optional+u16(b,pe+20);
 if(!table||!count||count>10000000)return 0;
 need(b,table,count*18);size_t strings=table+count*18;uint32_t string_size=u32(b,strings);need(b,strings,string_size);need(b,sections,nsec*40);
 for(size_t i=0;i<count;){size_t s=table+i*18;std::string name;
  if(u32(b,s)==0){uint32_t off=u32(b,s+4);if(off>=4&&off<string_size)name=text(b,strings+off,strings+string_size);}
  else {size_t n=0;while(n<8&&b[s+n])++n;name.assign(reinterpret_cast<const char*>(b.data()+s),n);}
  int16_t sec=static_cast<int16_t>(u16(b,s+12));
  if(name==wanted&&sec>0&&size_t(sec)<=nsec){size_t sh=sections+size_t(sec-1)*40;uint32_t flags=u32(b,sh+36),size=u32(b,sh+8),raw=u32(b,sh+16),value=u32(b,s+8);
   if(code && !(flags&0x20000000u))throw std::runtime_error("Native function is outside an executable section: "+wanted);
   if(value>=(size>raw?size:raw))throw std::runtime_error("Native symbol is outside its section: "+wanted);
   uint32_t rva=u32(b,sh+12)+value;if(rva>=u32(b,optional+56))throw std::runtime_error("Native symbol exceeds image: "+wanted);return rva;
  }
  size_t next=i+1+b[s+17];if(next<=i||next>count)throw std::runtime_error("Invalid COFF auxiliary records");i=next;
 }
 return 0;
}
}
