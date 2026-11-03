#!/usr/bin/env python3
"""Generate ABI-transparent, PE-resident jump entries for native COM methods."""
from pathlib import Path
import sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
n=256
s=['.text']
for i in range(n):s += [f'.globl thunk_{i}','.p2align 5',f'thunk_{i}:','.rept 16','nop','.endr',f'jmp *targets+{i*8}(%rip)']
s+=['.section .rdata,"dr"','.globl thunks','thunks:']+[f'.quad thunk_{i}' for i in range(n)]
(out/'thunks.S').write_text('\n'.join(s)+'\n')
s=['EXTERN targets:QWORD','PUBLIC thunks','.code']
for i in range(n):s += ['ALIGN 32',f'thunk_{i} PROC','REPT 16','nop','ENDM',f'jmp QWORD PTR [targets+{i*8}]',f'thunk_{i} ENDP']
s+=['.const','ALIGN 8','thunks LABEL QWORD']+[f'DQ thunk_{i}' for i in range(n)]+['END']
(out/'thunks.asm').write_text('\n'.join(s)+'\n')
