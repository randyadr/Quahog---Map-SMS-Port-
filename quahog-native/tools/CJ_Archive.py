#!/usr/bin/env python3
"""Super Mario Sunshine PC Port: user-supplied J3D BMD model -> data-file mod.

Requires only Python 3.9+ standard library. Never modifies the source disc.
"""
from __future__ import annotations
import argparse
import os
import re
import struct
import sys
import tempfile
import stat
import time
from pathlib import Path
from contextlib import contextmanager

WINDOWS_FILE_ACCESS = os.name == 'nt'


def retry_file_access(operation):
    """Wait briefly for Windows readers/scanners; never truncate a locked file."""
    for attempt in range(16):
        try:
            return operation()
        except OSError as error:
            if not (isinstance(error, PermissionError) or getattr(error, 'winerror', None) in (5, 32, 33)) or attempt == 15:
                raise
            time.sleep(min(.05 * (attempt + 1), .3))


def file_access_error(path, error, action='replace'):
    code = getattr(error, 'winerror', None)
    reason = 'Windows reports a sharing/lock conflict.' if code in (32, 33) else 'The file is locked, read-only, or its folder denies replacement.'
    return PermissionError('Cannot '+action+' '+str(path)+'. '+reason+
        ' Close the game, SMS Launcher and any other installer, then retry setup.'
        ' Setup keeps verified compiler work for the retry.'+
        (' Windows error '+str(code)+'.' if code else ''))


@contextmanager
def writable_owned_files(paths):
    """Temporarily clear only the read-only bit on explicitly owned mod files.

    Windows chmod changes this attribute only, not ACLs. Restore it on both
    success and rollback; never apply this to the game executable or disc.
    """
    changed = []
    try:
        if WINDOWS_FILE_ACCESS:
            for path in dict.fromkeys(Path(p) for p in paths):
                if not path.is_file() or path.is_symlink():
                    continue
                mode = path.stat().st_mode
                if not mode & stat.S_IWRITE:
                    try:
                        retry_file_access(lambda: os.chmod(path, mode | stat.S_IWRITE))
                    except OSError as error:
                        raise file_access_error(path, error, 'make the owned mod file writable:') from error
                    changed.append((path, mode))
        yield
    finally:
        for path, mode in reversed(changed):
            if path.is_file():
                retry_file_access(lambda: os.chmod(path, mode))


def _windows_replace_probe(path):
    """Request DELETE access without deleting, renaming or writing the file."""
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    create.restype = wintypes.HANDLE
    close = kernel.CloseHandle
    close.argtypes = [wintypes.HANDLE]
    close.restype = wintypes.BOOL
    handle = create(str(path.resolve()), 0x00010000, 7, None, 3, 0, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    close(handle)


def preflight_replace(paths):
    """Check target handles and same-directory replacement before costly work."""
    parents = set()
    for path in dict.fromkeys(Path(p) for p in paths):
        names = []
        try:
            if path.exists():
                if not path.is_file():
                    raise ValueError('Expected an installed file, found a directory: '+str(path))
                if WINDOWS_FILE_ACCESS:
                    if not path.stat().st_mode & stat.S_IWRITE:
                        raise PermissionError('The destination is read-only')
                    retry_file_access(lambda: _windows_replace_probe(path))
            # Probe the nearest existing ancestor for a first install. Do not
            # leave new mod directories behind when later validation fails.
            parent = path.parent
            while not parent.exists():
                parent = parent.parent
            if not parent.is_dir():
                raise ValueError('Install directory is blocked by a file: '+str(parent))
            if parent not in parents:
                for _ in range(2):
                    fd, name = tempfile.mkstemp(prefix='.cj_access_', dir=parent)
                    names.append(name)
                    with os.fdopen(fd, 'wb') as stream:
                        stream.write(b'file access check');stream.flush();os.fsync(stream.fileno())
                retry_file_access(lambda: os.replace(names[0], names[1]))
                parents.add(parent)
        except PermissionError as error:
            raise file_access_error(path, error, 'access') from error
        finally:
            for name in names:
                if os.path.exists(name):
                    retry_file_access(lambda: os.unlink(name))

MARIO_ARCHIVE = ("data", "mario.szs")
GAME_ID = b"GMSE01"
GAME_IDS = {b"GMSE01": "Sunshine", b"GMSE04": "Eclipse"}
DEFAULT_MOD = "cj_character_replace"
DEFAULT_TARGET = "ma_mdl1.bmd"


def u16(b: bytes, off: int) -> int:
    return struct.unpack_from(">H", b, off)[0]


def u32(b: bytes, off: int) -> int:
    return struct.unpack_from(">I", b, off)[0]


def set32(b: bytearray, off: int, value: int) -> None:
    if value < 0 or value >= (1 << 32):
        raise ValueError("Archive exceeds 32-bit offset limits")
    struct.pack_into(">I", b, off, value)


def align(n: int, block: int = 32) -> int:
    return (n + block - 1) // block * block


class Disc:
    def __init__(self, path: Path):
        self.path = path
        self.fp = open(path, "rb")
        try:
            self._open()
        except BaseException:
            self.fp.close()
            raise

    def _open(self):
        head = self.fp.read(0x8000)
        self.is_ciso = head[:4] == b"CISO"
        if self.is_ciso:
            self.block = struct.unpack_from("<I", head, 4)[0]
            if not 0x8000 <= self.block <= 0x400000:
                raise ValueError("Unsupported CISO block size")
            position = 0x8000
            self.offsets = []
            for flag in head[8:0x8000]:
                self.offsets.append(position if flag else None)
                if flag:
                    position += self.block
        elif head[:4] in (b"RVZ\x01", b"WIA\x01", b"\xb1\x0b\xc0\x01"):
            raise ValueError("RVZ/WIA/GCZ cannot be read; convert to ISO with Dolphin")
        if self.read(0x1C, 4) != b"\xC2\x33\x9F\x3D":
            raise ValueError("Not a recognized GameCube disc image")
        self.game_id = self.read(0, 6)
        if self.game_id not in GAME_IDS:
            raise ValueError("Expected USA Sunshine GMSE01 or Eclipse GMSE04, got " + repr(self.game_id))
        if self.read(7, 1) != b"\x00":
            raise ValueError("Expected GameCube revision 0")

    def read(self, offset: int, size: int) -> bytes:
        if not self.is_ciso:
            self.fp.seek(offset)
            data = self.fp.read(size)
            if len(data) != size:
                raise ValueError("Truncated game disc image")
            return data
        out = bytearray()
        while size:
            index, pos = divmod(offset, self.block)
            count = min(self.block - pos, size)
            physical = self.offsets[index] if index < len(self.offsets) else None
            if physical is None:
                out.extend(b"\0" * count)
            else:
                self.fp.seek(physical + pos)
                payload = self.fp.read(count)
                if len(payload) != count:
                    raise ValueError("Truncated CISO image")
                out.extend(payload)
            offset += count
            size -= count
        return bytes(out)

    def files(self):
        header = self.read(0, 0x440)
        fst_offset, fst_size = u32(header, 0x424), u32(header, 0x428)
        if not 12 <= fst_size <= 32 * 1024 * 1024:
            raise ValueError("Invalid disc file system table size")
        fst = self.read(fst_offset, fst_size)
        count = u32(fst, 8)
        if count < 2 or count * 12 > len(fst):
            raise ValueError("Invalid disc file system table")

        def name(i):
            start = count * 12 + (u32(fst, i * 12) & 0xFFFFFF)
            end = fst.index(b"\0", start)
            return fst[start:end].decode("ascii", "replace").lower()

        result = {}; stack = [(count, "")]
        for i in range(1, count):
            while stack and i >= stack[-1][0]: stack.pop()
            if not stack: raise ValueError("Malformed disc FST directory")
            leaf = name(i)
            if not leaf or leaf in (".", "..") or "/" in leaf or "\\" in leaf:
                raise ValueError("Malformed disc FST name")
            path = stack[-1][1] + leaf
            if fst[i * 12]:
                end = u32(fst, i * 12 + 8)
                if not i < end <= stack[-1][0]: raise ValueError("Malformed disc FST directory range")
                stack.append((end, path + "/"))
            else:
                if path in result: raise ValueError("Duplicate disc file: " + path)
                result[path] = (u32(fst, i * 12 + 4), u32(fst, i * 12 + 8))
        return result

    def archive(self) -> bytes:
        return self.file("data/mario.szs")

    def file(self, name: str) -> bytes:
        entry = self.files().get(name.lower().strip('/'))
        if not entry: raise ValueError("Disc lacks /" + name)
        if entry[1] > 256 * 1024 * 1024: raise ValueError("Disc resource exceeds 256 MiB: " + name)
        return self.read(*entry)

    def close(self):
        self.fp.close()


def read_archive(source: Path) -> bytes:
    if not source.is_file():
        raise FileNotFoundError("Source disc/archive not found: " + str(source))
    with source.open("rb") as f:
        signature = f.read(4)
    if signature in (b"Yaz0", b"RARC"):
        return source.read_bytes()
    disc = Disc(source)
    try:
        return disc.archive()
    finally:
        disc.close()


def yaz0_decode(src: bytes) -> bytes:
    if src[:4] != b"Yaz0":
        return src
    if len(src) < 16:
        raise ValueError("Truncated Yaz0 header")
    size = u32(src, 4)
    if size > 256 * 1024 * 1024:
        raise ValueError("Unexpectedly large Yaz0 archive")
    out = bytearray()
    pos = 16
    while len(out) < size:
        if pos >= len(src):
            raise ValueError("Truncated Yaz0 data")
        code = src[pos]
        pos += 1
        # Model/texture fixtures and literal archive segments can be copied in
        # complete eight-byte groups. Keep the same truncation and size guards.
        if code == 255:
            groups = min(4096, (size-len(out))//8, (len(src)-(pos-1))//9)
            if groups and src[pos-1:pos-1+groups*9:9] == b"\xff"*groups:
                begin=pos-1
                out.extend(b"".join(src[i+1:i+9] for i in range(begin,begin+groups*9,9)))
                pos=begin+groups*9
                continue
        for bit in range(8):
            if len(out) == size:
                break
            if code & (0x80 >> bit):
                if pos >= len(src):
                    raise ValueError("Truncated Yaz0 literal")
                out.append(src[pos]); pos += 1
            else:
                if pos + 2 > len(src):
                    raise ValueError("Truncated Yaz0 backreference")
                a, b = src[pos], src[pos + 1]
                pos += 2
                distance = ((a & 0xF) << 8 | b) + 1
                length = (a >> 4) + 2
                if (a >> 4) == 0:
                    if pos >= len(src):
                        raise ValueError("Truncated Yaz0 extended length")
                    length = src[pos] + 18; pos += 1
                if distance > len(out) or distance == 0:
                    raise ValueError("Invalid Yaz0 backreference")
                for _ in range(min(length, size - len(out))):
                    out.append(out[-distance])
    return bytes(out)


def yaz0_encode_literal(data: bytes) -> bytes:
    """Valid Yaz0 compression using literal runs (larger than optimized Yaz0)."""
    if len(data) >= (1 << 32):
        raise ValueError("Archive too large")
    dst = bytearray(b"Yaz0" + struct.pack(">I", len(data)) + b"\0" * 8)
    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]
        dst.append((0xFF << (8 - len(chunk))) & 0xFF)
        dst.extend(chunk)
    return bytes(dst)


class RARC:
    def __init__(self, src: bytes):
        if src[:4] != b"RARC" or len(src) < 0x60:
            raise ValueError("Not a valid uncompressed RARC archive")
        self.src = src
        self.data_start = 0x20 + u32(src, 0x0C)
        self.data_len = u32(src, 0x10)
        self.nodes = u32(src, 0x20)
        self.node_start = 0x20 + u32(src, 0x24)
        self.entries = u32(src, 0x28)
        self.entry_start = 0x20 + u32(src, 0x2C)
        self.str_start = 0x20 + u32(src, 0x34)
        if (self.data_start + self.data_len > len(src) or self.nodes > 10000
                or self.entries > 30000 or self.node_start + self.nodes * 16 > len(src)
                or self.entry_start + self.entries * 20 > len(src) or self.str_start >= self.data_start):
            raise ValueError("RARC header/offsets are inconsistent")
        self.items = self._discover_files()

    def text(self, rel: int) -> str:
        start = self.str_start + rel
        if not (self.str_start <= start < self.data_start):
            raise ValueError("Invalid RARC string")
        end = self.src.find(b"\0", start, self.data_start)
        if end == -1:
            raise ValueError("Unterminated RARC name")
        return self.src[start:end].decode("shift_jis", "replace")

    def _discover_files(self):
        nodes = []
        for n in range(self.nodes):
            addr = self.node_start + n * 16
            nodes.append((self.text(u32(self.src, addr + 4)), u16(self.src, addr + 10), u32(self.src, addr + 12)))
        seen = set()
        out = {}

        def walk(idx: int, prefix: str):
            if idx in seen:
                return
            if idx >= len(nodes):
                raise ValueError("Invalid RARC directory index")
            seen.add(idx)
            node_name, nfiles, first = nodes[idx]
            if first + nfiles > self.entries:
                raise ValueError("Invalid RARC file table")
            path = (prefix + "/" + node_name) if prefix else node_name
            for i in range(first, first + nfiles):
                e = self.entry_start + i * 20
                flags_and_name = u32(self.src, e + 4)
                flags = flags_and_name >> 24
                filename = self.text(flags_and_name & 0xFFFFFF)
                if filename in (".", ".."):
                    continue
                if flags & 0x02:
                    walk(u32(self.src, e + 8), path)
                else:
                    off, size = u32(self.src, e + 8), u32(self.src, e + 12)
                    if off + size > self.data_len:
                        raise ValueError("RARC file exceeds data area: " + filename)
                    out[(path + "/" + filename).lower()] = (e, off, size)
        walk(0, "")
        return out

    def models(self):
        return sorted(k for k in self.items if k.endswith(".bmd") and "/bmd/" in k)

    def add_files(self, additions):
        """Add local resources while retaining old file IDs, flags and data.

        Rebuild only metadata; insert new MRAM data before existing ARAM/DVD
        data. Existing directories and root name remain in their original order.
        Supports root files or one child directory, as used by mario/bck.
        """
        if not additions: return self.src
        nodes=[];groups=[]
        for index in range(self.nodes):
            at=self.node_start+index*16;node=bytearray(self.src[at:at+16])
            first=u32(node,12);count=u16(node,10)
            if first+count>self.entries: raise ValueError("Invalid RARC directory entries")
            nodes.append(node)
            groups.append([bytearray(self.src[self.entry_start+i*20:self.entry_start+(i+1)*20]) for i in range(first,first+count)])
        root=self.text(u32(nodes[0],4)).lower()
        string_size=u32(self.src,0x30)
        if self.str_start+string_size>self.data_start: raise ValueError("Invalid RARC string table length")
        strings=bytearray(self.src[self.str_start:self.str_start+string_size]);new_names={}
        def string(name):
            if name not in new_names:
                new_names[name]=len(strings);strings.extend(name.encode('ascii')+b'\0')
            if new_names[name]>0xFFFFFF: raise ValueError("RARC name offset overflow")
            return new_names[name]
        def key(name):
            value=0
            for char in name.encode('ascii'): value=(value*3+char)&0xFFFF
            return value
        def directory_entry(name,index):
            return bytearray(struct.pack('>HHIIII',0xFFFF,key(name),0x02000000|string(name),index,0x10,0))
        directories={}
        for entry in groups[0]:
            if entry[4]&2:
                name=self.text(u32(entry,4)&0xFFFFFF).lower()
                if name not in ('.','..'): directories[name]=u32(entry,8)
        old_ids=[u16(e,0) for group in groups for e in group if not e[4]&2 and u16(e,0)!=0xFFFF]
        next_id=max(u16(self.src,0x38),max(old_ids,default=-1)+1)
        mram=u32(self.src,0x14)
        if mram>self.data_len or mram+u32(self.src,0x18)>self.data_len:
            raise ValueError("Invalid RARC memory region lengths")
        extra=bytearray();normalized={}
        for path,payload in sorted(additions.items()):
            path=path.lower().strip('/');parts=path.split('/')
            if (parts[0]!=root or len(parts) not in (2,3) or any(not p or p in ('.','..') for p in parts)
                    or not all(re.fullmatch(r'[a-z0-9_.-]+',p) for p in parts)):
                raise ValueError("Unsupported local resource path: "+path)
            if path in self.items or path in normalized: raise ValueError("Resource already exists: "+path)
            parent=0
            if len(parts)==3:
                folder=parts[1]
                if folder not in directories:
                    parent=len(nodes);directories[folder]=parent
                    node=bytearray(struct.pack('>4sIHHI',folder.upper().encode()[:4].ljust(4,b' '),string(folder),key(folder),0,0))
                    nodes.append(node);groups.append([directory_entry('.',parent),directory_entry('..',0)])
                    groups[0].append(directory_entry(folder,parent))
                parent=directories[folder]
                if not 0<=parent<len(groups): raise ValueError("Invalid RARC child directory")
            if next_id>=0xFFFF: raise ValueError("RARC file ID capacity exceeded")
            name=parts[-1];offset=mram+len(extra)
            groups[parent].append(bytearray(struct.pack('>HHIIII',next_id,key(name),0x11000000|string(name),offset,len(payload),0)))
            next_id+=1;extra.extend(payload);extra.extend(bytes(-len(extra)%32))
            normalized[path]=bytes(payload)
        # Shift old non-MRAM resources by the amount inserted at the MRAM end.
        old_entries={self.src[self.entry_start+i*20:self.entry_start+(i+1)*20] for i in range(self.entries)}
        for group in groups:
            for entry in group:
                if bytes(entry) in old_entries and not entry[4]&2 and u32(entry,8)>=mram:
                    set32(entry,8,u32(entry,8)+len(extra))
        out=bytearray(self.src[:0x40]);set32(out,8,0x20)
        set32(out,0x20,len(nodes));set32(out,0x24,len(out)-0x20)
        total=0
        for node,group in zip(nodes,groups):
            if len(group)>0xFFFF: raise ValueError("RARC directory capacity exceeded")
            struct.pack_into('>H',node,10,len(group));set32(node,12,total);total+=len(group);out.extend(node)
        out.extend(bytes(-len(out)%32));set32(out,0x28,total);set32(out,0x2C,len(out)-0x20)
        for group in groups:
            for entry in group:out.extend(entry)
        out.extend(bytes(-len(out)%32));set32(out,0x34,len(out)-0x20)
        string_start=len(out);out.extend(strings);out.extend(bytes(-len(out)%32));set32(out,0x30,len(out)-string_start)
        set32(out,0x0C,len(out)-0x20);set32(out,0x10,self.data_len+len(extra));set32(out,0x14,mram+len(extra))
        struct.pack_into('>H',out,0x38,next_id);out[0x3A]=0  # explicit IDs, not table positions
        old_data=self.src[self.data_start:self.data_start+self.data_len]
        out.extend(old_data[:mram]);out.extend(extra);out.extend(old_data[mram:]);out.extend(self.src[self.data_start+self.data_len:])
        set32(out,4,len(out));result=RARC(bytes(out))
        if set(result.items)!=set(self.items)|set(normalized): raise ValueError("Added-resource table verification failed")
        for path in self.items:
            if item(result,path)!=item(self,path): raise ValueError("Existing resource changed while adding "+path)
        for path,payload in normalized.items():
            if item(result,path)!=payload: raise ValueError("Added-resource data verification failed: "+path)
        return bytes(out)

    def replace(self, path: str, data: bytes) -> bytes:
        return self.replace_many({path:data})

    def replace_many(self, replacements) -> bytes:
        """Repack data once, detaching changed aliases without overwriting peers.

        All source bytes are read before constructing output. Exact aliases may
        continue sharing only when their desired contents and memory bank agree.
        File IDs, names, hashes, flags, directories and reserved fields stay put.
        """
        targets={}
        for path,payload in replacements.items():
            path=path.lower().strip('/')
            if path not in self.items:raise ValueError('No resource called '+path+' in archive')
            entry=self.items[path][0];payload=bytes(payload)
            if entry in targets and targets[entry]!=payload:raise ValueError('Conflicting replacements for '+path)
            targets[entry]=payload
        if not targets:return self.src
        mram=u32(self.src,0x14);aram=u32(self.src,0x18)
        if mram+aram>self.data_len:raise ValueError('Invalid RARC memory region lengths')
        labels={entry:path for path,(entry,_,_) in self.items.items()}
        groups=[[],[],[]];changed=False
        # Include every file-table entry, even ones not reached by directory
        # traversal. No unmodified resource can be lost during compaction.
        for i in range(self.entries):
            entry=self.entry_start+i*20;flags=self.src[entry+4]
            if flags&2:continue
            offset=u32(self.src,entry+8);size=u32(self.src,entry+12)
            if offset+size>self.data_len:
                raise ValueError('Resource exceeds archive data: '+labels.get(entry,'entry '+str(i)))
            original=self.src[self.data_start+offset:self.data_start+offset+size]
            payload=targets.get(entry,original);changed |= payload!=original
            # Follow JKR preload flags. Older entries without a preload flag
            # retain the bank indicated by their original physical offset.
            if flags&0x10:bank=0
            elif flags&0x20:bank=1
            elif flags&0x40:bank=2
            else:bank=0 if offset<mram else (1 if offset<mram+aram else 2)
            groups[bank].append((entry,offset,size,payload))
        if not changed:return self.src
        out=bytearray(self.src[:self.data_start]);body=bytearray();bank_sizes=[]
        for entries in groups:
            begin=len(body);slots={}
            for entry,old_offset,old_size,payload in entries:
                # Different original resources are never newly aliased merely
                # because their contents happen to match.
                identity=(old_offset,old_size,payload)
                if identity not in slots:
                    slots[identity]=len(body);body.extend(payload);body.extend(bytes(-len(body)%32))
                set32(out,entry+8,slots[identity]);set32(out,entry+12,len(payload))
            bank_sizes.append(len(body)-begin)
        set32(out,0x10,len(body));set32(out,0x14,bank_sizes[0]);set32(out,0x18,bank_sizes[1])
        # 0x1C is reserved, not a DVD-size field; preserve it unchanged.
        out.extend(body);out.extend(self.src[self.data_start+self.data_len:]);set32(out,4,len(out))
        result=RARC(bytes(out))
        if result.items.keys()!=self.items.keys():raise ValueError('Repacked archive resource list changed')
        for path,(entry,_,_) in self.items.items():
            if item(result,path)!=targets.get(entry,item(self,path)):
                raise ValueError('Repacked resource verification failed: '+path)
        return bytes(out)


def j3d_joint_count(data: bytes):
    if not data.startswith(b"J3D2bmd"):
        raise ValueError("Replacement must be a J3D BMD file (J3D2bmd*). GLB, FBX, OBJ and BDL are not directly supported.")
    if len(data) < 0x20:
        raise ValueError("BMD file too small")
    offset = 0x20
    while offset + 8 <= len(data):
        tag = data[offset:offset + 4]
        length = u32(data, offset + 4)
        if length < 8 or offset + length > len(data):
            raise ValueError("Malformed BMD section")
        if tag == b"JNT1":
            if length < 12:
                raise ValueError("Malformed JNT1 section")
            return u16(data, offset + 8)
        offset += length
    return None


def safe_mod_name(s: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,50}", s):
        raise ValueError("Mod name can contain only letters, numbers, underscores and hyphens")
    return s


def enable_mod(settings_path: Path, mod_name: str):
    if settings_path.exists():
        text = settings_path.read_text(encoding="utf-8-sig")
    else:
        text = "# Super Mario Sunshine PC Port settings\n"
    pattern = re.compile(r"^\s*mod\s*=\s*(.*?)\s*$", re.MULTILINE | re.IGNORECASE)
    match = pattern.search(text)
    if match:
        names = [x.strip() for x in match.group(1).split(";") if x.strip() and x.strip().lower() not in ("off", "none")]
        names = [x for x in names if x.lower() != mod_name.lower()]
        names.append(mod_name)  # Last mod takes precedence.
        text = text[:match.start()] + "mod = " + ";".join(names) + text[match.end():]
    else:
        text = text.rstrip("\r\n") + "\nmod = " + mod_name + "\n"
    atomic_write(settings_path, text.encode("utf-8"))



def selector_archive(path):
    # Only our owned DVD overlay needs replacement while its read handle lives.
    return tuple(part.casefold() for part in Path(path).parts[-5:]) in (('mods','cj_character_replace','files','data','mario.szs'),('skinselector','data','files','data','mario.szs'))


def _windows_replace_file(source, destination, backup):
    """Replace a shared-open file; preserve ACLs and keep a recovery name."""
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    replace = kernel.ReplaceFileW
    replace.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR,
                        wintypes.DWORD, ctypes.c_void_p, ctypes.c_void_p]
    replace.restype = wintypes.BOOL
    if not replace(str(Path(destination).resolve()), str(Path(source).resolve()),
                   str(Path(backup).resolve()), 0, None, None):
        raise ctypes.WinError(ctypes.get_last_error())


def replace_owned_archive(source, destination, previous):
    """ReplaceFileW can replace the archive held open by the running DVD reader.

    A backup name matters: errors 1176/1177 can otherwise leave the old path
    absent. Never overwrite an unexpected destination or discard recovery data.
    """
    source, destination = Path(source), Path(destination)
    backup = source.with_name(source.name + '.previous')
    try:
        _windows_replace_file(source, destination, backup)
    except OSError as error:
        if backup.exists():
            if not backup.is_file() or backup.read_bytes() != previous:
                raise RuntimeError('Archive replacement failed; unexpected recovery file retained at '+str(backup)) from error
            if not destination.exists():
                try:
                    # Windows rename without replace fails if a new target appears.
                    retry_file_access(lambda: os.rename(backup, destination))
                except OSError as recovery:
                    raise RuntimeError('Archive replacement failed and could not restore its old path. Recovery file retained at '+str(backup)+': '+str(recovery)) from error
            if not destination.is_file() or destination.read_bytes() != previous:
                raise RuntimeError('Archive replacement failed; destination changed unexpectedly. Recovery file retained at '+str(backup)) from error
            if backup.exists():
                retry_file_access(lambda: os.unlink(backup))
        raise
    else:
        # A successful replacement must not be reported as a failed commit
        # merely because a scanner delays removal of the old snapshot name.
        if backup.exists():
            try:retry_file_access(lambda: os.unlink(backup))
            except OSError:pass # Keep the old snapshot as a harmless recovery file.


def atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    previous = path.read_bytes() if path.is_file() else None
    if previous == data:
        return
    fd, name = tempfile.mkstemp(prefix=".cj_write_", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            if WINDOWS_FILE_ACCESS and selector_archive(path) and previous is not None:
                retry_file_access(lambda: replace_owned_archive(name, path, previous))
            else:
                retry_file_access(lambda: os.replace(name, path))
        except PermissionError as error:
            raise file_access_error(path, error) from error
    finally:
        if os.path.exists(name):
            retry_file_access(lambda: os.unlink(name))


def item(arc, name):
    _, off, size = arc.items[name]
    return arc.src[arc.data_start+off:arc.data_start+off+size]
