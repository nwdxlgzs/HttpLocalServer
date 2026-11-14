"""Small auditable bytecode fixtures. No SDK, Rust or downloaded compiler needed."""
def u(value):
    result=bytearray()
    while True:
        byte=value & 127;value >>= 7;result.append(byte | (128 if value else 0))
        if not value:return bytes(result)

def vec(items):return u(len(items))+b''.join(items)
def text(value):
    data=value.encode('utf-8');return u(len(data))+data
def section(kind,data):return bytes([kind])+u(len(data))+data
def signature(params=(),results=()):return b'\x60'+vec([bytes([p]) for p in params])+vec([bytes([r]) for r in results])
def body(code):
    data=b'\x00'+code+b'\x0b';return u(len(data))+data
def export(name,kind,index):return text(name)+bytes([kind])+u(index)
MAGIC=b'\0asm\1\0\0\0'

def arithmetic():
    types=[signature([127,127],[127]),signature([127],[127]),signature([126],[126]),signature([124,124],[124]),signature()]
    functions=[0,1,1,2,3,4]
    codes=[b'\x20\x00\x20\x01\x6a',b'\x20\x00\x40\x00',b'\x20\x00\x28\x02\x00',b'\x20\x00\x42\x01\x7c',b'\x20\x00\x20\x01\xa0',b'\x03\x40\x0c\x00\x0b']
    exports=[export(name,0,i) for i,name in enumerate(['add','grow','load','increment64','add64','spin'])]+[export('memory',2,0)]
    return MAGIC+section(1,vec(types))+section(3,vec([u(x) for x in functions]))+section(5,vec([b'\x01\x01\x03']))+section(7,vec(exports))+section(10,vec([body(x) for x in codes]))

def imported():
    return MAGIC+section(1,vec([signature([127],[127])]))+section(2,vec([text('env')+text('twice')+b'\x00\x00']))+section(3,vec([u(0)]))+section(7,vec([export('call',0,1)]))+section(10,vec([body(b'\x20\x00\x10\x00')]))

def start_loop():
    return MAGIC+section(1,vec([signature()]))+section(3,vec([u(0)]))+section(8,u(0))+section(10,vec([body(b'\x03\x40\x0c\x00\x0b')]))

def wasi_args():
    # call args_sizes_get(0,4), exposing memory so the JS shim can fill it.
    return MAGIC+section(1,vec([signature([127,127],[127]),signature((),[127])]))+section(2,vec([text('wasi_snapshot_preview1')+text('args_sizes_get')+b'\x00\x00']))+section(3,vec([u(1)]))+section(5,vec([b'\x01\x01\x01']))+section(7,vec([export('memory',2,0),export('args',0,1)]))+section(10,vec([body(b'\x41\x00\x41\x04\x10\x00')]))

def enormous_memory():return MAGIC+section(5,vec([b'\x01'+u(65535)+u(65536)]))+section(7,vec([export('memory',2,0)]))
def invalid_ref_null():return MAGIC+section(6,vec([b'\x7e\x00\xd0\x7e\x0b']))
