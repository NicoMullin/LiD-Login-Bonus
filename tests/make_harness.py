"""Write harness.c: the C part from harness.in.c plus generated assembly.

The harness stands in for BrgGame-Steam.exe. Its daily check, reward granter
and "open an elevator stop" function, and the two places that open the hub
stop, are written with the same machine-code bytes the plugin looks for in the
real game.

Usage: python tests/make_harness.py OUTPUT.c
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def asm(lines):
    return ''.join('    "%s\\n"\n' % l.replace('"', '\\"') for l in lines)


def b(hexes):
    return '.byte ' + ','.join('0x' + h for h in hexes.split())


GRANT_START = ('48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 68 B8 48 81 EC 20 01 00 00 '
               '48 C7 45 10 FE FF FF FF 48 89 58 08 48 89 70 10 48 89 78 18 4D 8B E0 48 8B DA 4C 8B E9')

code = ['.text',
        # The daily check, in the game's shape. Asks whether today is already
        # counted (fake_already), and if not, records the day with the Express
        # Pass flag the game works out from its end time (fake_record).
        '.globl fake_daily', 'fake_daily:',
        b('48 83 EC 28 48 8D 4C 24 30 48 83 CA FF'), 'call fake_ptr',
        b('48 8B 08'), 'call fake_pass',
        b('48 8B C8'), 'call fake_already',
        b('84 C0 74 07 32 C0 48 83 C4 28 C3'), 'call fake_now',
        b('33 C9 B2 01 48 39 05'), '.long g_vip_end - (. + 4)',
        b('0F 9D C1 85 C9 0F 95 C1'), 'call fake_record',
        b('B0 01 48 83 C4 28 C3'),
        # A function that starts like the granter but is not it (no message).
        '.globl decoy_grant', 'decoy_grant:', b(GRANT_START),
        b('48 8D 15'), '.long g_other_text - (. + 4)', '.fill 0x100, 1, 0xCC',
        # The reward granter: the game's prologue, its "rwdId not found!"
        # message, then it notes what it was given and answers yes.
        '.globl fake_grant', 'fake_grant:', b(GRANT_START),
        b('48 8D 15'), '.long g_grant_message - (. + 4)',
        'movq %r13, %rcx', 'movq %rbx, %rdx', 'movq %r12, %r8', 'call record_grant',
        'leaq 0x20(%rbp), %rsp', 'movq 0x30(%rsp), %rbx', 'movq 0x38(%rsp), %rsi', 'movq 0x40(%rsp), %rdi',
        'popq %r15', 'popq %r14', 'popq %r13', 'popq %r12', 'popq %rbp', 'ret',
        # Opens an elevator stop (only looked at, never run here).
        '.globl fake_open', 'fake_open:',
        b('40 57 48 83 EC 40 48 C7 44 24 20 FE FF FF FF 48 89 5C 24 50 48 8B DA'),
        b('48 8D B9 C8 02 00 00 48 8B 07 48 8B CF FF 50 28 48 85 C0 74 07 32 C0'),
        b('E9'), '.long open_out - (. + 4)',
        'movb $1, %al',
        'open_out:', 'movq 0x50(%rsp), %rbx', 'addq $0x40, %rsp', 'popq %rdi', 'ret']
# Records a new login day (only looked at, never run here): raises the login
# count, compares the last login 8 bytes after it, raises the days in a row 4
# after it.
code += ['.globl fake_login', 'fake_login:',
         b('40 53 48 83 EC 20 FF 05'), '.long g_login - (. + 4)',
         b('48 8B D9 48 8D 91 80 AE FE FF 48 8D 4C 24 30'), 'call fake_pass',
         b('48 8B 08'), 'call fake_pass',
         b('48 8B 0D'), '.long g_login + 8 - (. + 4)',
         b('48 3B C1 75 08 FF 05'), '.long g_login + 4 - (. + 4)', 'int3']
# The game's two places that open the hub stop for the player.
for name in ('hub_a', 'hub_b'):
    code += [f'.globl {name}', f'{name}:',
             b('48 8D 15'), '.long g_hub_name - (. + 4)',
             b('48 8D 0D'), '.long g_player - (. + 4)',
             'call fake_open', 'ret']

body = (HERE / 'harness.in.c').read_text().replace('@ASM@', asm(code))
Path(sys.argv[1]).write_text(body)
