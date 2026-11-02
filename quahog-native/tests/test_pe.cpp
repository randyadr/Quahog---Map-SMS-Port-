#include "../src/pe_symbols.hpp"
#include <fstream>
#include <cassert>
#include <iostream>
static void put32(qpe::Bytes& b,size_t p,uint32_t x){for(int i=0;i<4;++i)b[p+i]=static_cast<unsigned char>(x>>(i*8));}
int main(int argc,char** argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);qpe::Bytes b((std::istreambuf_iterator<char>(f)),{});
 assert(qpe::symbol(b,"port_dvd_read_file",true)==0x1020);assert(qpe::symbol(b,"port_dvd_override",true)==0x1040);assert(qpe::symbol(b,"port_mem1_size",false)==0x3060);
 assert(qpe::symbol(b,"unknown",true)==0);
 auto invalid=b;put32(invalid,392+36,0x40000000);try{qpe::symbol(invalid,"port_dvd_read_file",true);assert(false);}catch(const std::exception&){}
 invalid=b;invalid.resize(650);try{qpe::symbol(invalid,"port_dvd_override",true);assert(false);}catch(const std::exception&){}
 invalid=b;invalid[617]=9;try{qpe::symbol(invalid,"port_mem1_size",false);assert(false);}catch(const std::exception&){}
 std::cout<<"PE resolver tests passed: exact section RVAs, data symbols, missing names, malformed executable, auxiliary records.\n";
}
