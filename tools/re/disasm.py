import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
img=open("main.flat","rb").read()
md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
start=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 40
for insn in md.disasm(img[start:start+n*4], start):
    print(f"  {insn.address:#010x}: {insn.mnemonic:8} {insn.op_str}")
