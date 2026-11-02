import struct
from types import SimpleNamespace
pe_check=SimpleNamespace(REQUIRED={"port_dvd_read_file":True,"port_dvd_override":True,"port_mem1_size":False})

def pe_fixture():
    data=bytearray(2048);data[:2]=b'MZ';struct.pack_into('<I',data,60,128)
    data[128:132]=b'PE\0\0';struct.pack_into('<HHIIIHH',data,132,0x8664,2,0,600,3,240,0)
    struct.pack_into('<I',data,128+24+56,0x5000)
    for at,rva,flags in ((392,0x1000,0x60000020),(432,0x3000,0xc0000040)):
        struct.pack_into('<IIII',data,at+8,0x1000,rva,0x1000,0);struct.pack_into('<I',data,at+36,flags)
    names=bytearray(4)
    for i,(name,code) in enumerate(pe_check.REQUIRED.items()):
        offset=len(names);names.extend(name.encode()+b'\0');struct.pack_into('<IIIhHBB',data,600+i*18,0,offset,32+i*32,1 if code else 2,0,2,0)
    struct.pack_into('<I',names,0,len(names));data[654:654+len(names)]=names
    return bytes(data)

