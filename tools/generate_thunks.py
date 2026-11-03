#!/usr/bin/env python3
"""Generate ABI-transparent, PE-resident jump entries for native COM methods."""
from pathlib import Path
import sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
n=256
s=['.text']
for i in range(n):s += [f'.globl thunk_{i}','.p2align 5',f'thunk_{i}:','.rept 16','nop','.endr',f'jmp *targets+{i*8}(%rip)']

(out/'thunks.S').write_text('\n'.join(s)+'\n')
s=['EXTERN targets:QWORD','.code']
for i in range(n):s += [f'PUBLIC thunk_{i}','ALIGN 16',f'thunk_{i} PROC','REPT 16','nop','ENDM',f'jmp QWORD PTR [targets+{i*8}]',f'thunk_{i} ENDP']
s+=['END']
(out/'thunks.asm').write_text('\n'.join(s)+'\n')

# C takes the addresses so MSVC /guard:cf emits GFIDs for the assembly entries.
c=[f'extern void thunk_{i}(void);' for i in range(n)]
c+=['void *thunks[256] = {']+[f'    (void*)thunk_{i},' for i in range(n)]+['};']
(out/'thunk_table.c').write_text('\n'.join(c)+'\n')
