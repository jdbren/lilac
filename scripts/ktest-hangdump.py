#!/usr/bin/env python3
"""Dump where each vCPU of a hung qemu is executing.

Usage: ktest-hangdump.py <monitor-unix-socket> <kernel-elf>

Talks to the qemu HMP monitor, reads every CPU's registers and walks the
frame-pointer chain (the kernel is built with -fno-omit-frame-pointer), then
symbolizes the addresses with addr2line.
"""
import re
import socket
import subprocess
import sys
import time

sock_path, elf = sys.argv[1], sys.argv[2]


class Monitor:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(2)
        self.s.connect(path)
        self._read_until_prompt()

    def _read_until_prompt(self):
        buf = b''
        deadline = time.time() + 5
        while not buf.rstrip().endswith(b'(qemu)') and time.time() < deadline:
            try:
                chunk = self.s.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            buf += chunk
        text = buf.decode('utf-8', 'replace')
        # strip terminal escape sequences
        return re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', text).replace('\r', '')

    def cmd(self, c):
        self.s.sendall(c.encode() + b'\n')
        out = self._read_until_prompt()
        lines = out.split('\n')
        # drop the echoed command and trailing prompt
        return '\n'.join(l for l in lines if not l.startswith('(qemu)') and l.strip() != c)


def symbolize(addrs):
    if not addrs:
        return {}
    try:
        out = subprocess.run(['x86_64-lilac-addr2line', '-f', '-C', '-e', elf] +
                             [hex(a) for a in addrs], capture_output=True, text=True).stdout
    except FileNotFoundError:
        return {}
    lines = out.split('\n')
    res = {}
    for i, a in enumerate(addrs):
        fn = lines[2 * i] if 2 * i < len(lines) else '??'
        loc = lines[2 * i + 1] if 2 * i + 1 < len(lines) else '??'
        loc = re.sub(r'^.*/lilac/', '', loc)
        res[a] = f'{fn} ({loc})'
    return res


def main():
    try:
        mon = Monitor(sock_path)
    except OSError as e:
        print(f'hang dump: cannot connect to monitor: {e}')
        return
    mon.cmd('stop')
    regs = mon.cmd('info registers -a')
    cpus = []
    for block in re.split(r'\n(?=CPU#\d+)', regs):
        m = re.match(r'CPU#(\d+)', block)
        rip = re.search(r'RIP=([0-9a-f]+)', block)
        rbp = re.search(r'RBP=([0-9a-f]+)', block)
        efl = re.search(r'RFL=([0-9a-f]+)', block)
        hlt = re.search(r'HLT=(\d)', block)
        if m and rip:
            cpus.append((int(m.group(1)), int(rip.group(1), 16),
                         int(rbp.group(1), 16) if rbp else 0,
                         int(efl.group(1), 16) if efl else 0,
                         hlt.group(1) == '1' if hlt else False))

    frames = {}
    for cpu, rip, rbp, _, _ in cpus:
        chain = [rip]
        if rip >= 0xffff800000000000:
            mon.cmd(f'cpu {cpu}')
            for _ in range(10):
                if rbp < 0xffff800000000000:
                    break
                out = mon.cmd(f'x/2gx 0x{rbp:x}')
                vals = re.findall(r'0x([0-9a-f]{16})', out.split(':', 1)[-1])
                if len(vals) < 2:
                    break
                rbp, ret = int(vals[0], 16), int(vals[1], 16)
                if ret < 0xffffffff80000000:
                    break
                chain.append(ret)
        frames[cpu] = chain

    syms = symbolize(sorted({a for c in frames.values() for a in c}))
    for cpu, rip, rbp, rfl, hlt in cpus:
        state = []
        if hlt:
            state.append('halted')
        state.append('IF=1' if rfl & 0x200 else 'IF=0 (interrupts off)')
        where = 'user' if rip < 0x800000000000 else 'kernel'
        print(f'CPU{cpu}: {where} rip={rip:#x} {", ".join(state)}')
        for i, a in enumerate(frames.get(cpu, [])):
            print(f'    #{i} {a:#x} {syms.get(a, "")}')
    mon.cmd('quit')


main()
