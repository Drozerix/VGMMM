#!/usr/bin/env python3
"""Differential test: run the original vgm_mm.exe code (decomp/oracle) and the port's CLI on the
same module and compare every output byte for byte.

usage: compare.py [--tfe|--mod] module [module...]
Needs decomp/tools/venv (unicorn) and a built build/vgmmm-cli.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PY = os.path.join(ROOT, 'decomp', 'tools', 'venv', 'bin', 'python')
ORACLE = os.path.join(ROOT, 'decomp', 'oracle', 'oracle.py')
CLI = os.path.join(HERE, '..', 'build', 'vgmmm-cli')


def first_diff(a, b):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return i
    return n if len(a) != len(b) else -1


def run(module, kind):
    res = []
    with tempfile.TemporaryDirectory() as td:
        o = lambda n: os.path.join(td, 'o_' + n)
        p = lambda n: os.path.join(td, 'p_' + n)
        flag = [kind] if kind else []
        outs = ['tables', 'save', 'vgm', 'vgm160', 'tfd', 'text']
        r = subprocess.run([PY, ORACLE, module] + flag + ['--tables', o('tables'), '--save', o('save'),
                           '--vgm', o('vgm'), '--vgm160', o('vgm160'), '--tfd', o('tfd'), '--text', o('text')],
                           capture_output=True, text=True)
        if r.returncode:
            return [('oracle', 'FAILED: ' + (r.stderr.strip().splitlines() or ['?'])[-1])]
        r = subprocess.run([CLI, module] + flag + ['--date', '2011-11-15', '--no-optimise',
                           '--tables', p('tables'), '--save', p('save'), '--vgm', p('vgm'),
                           '--vgm160', p('vgm160'), '--tfd', p('tfd'), '--text', p('text')],
                           capture_output=True, text=True)
        if r.returncode:
            return [('port', 'FAILED: ' + r.stderr.strip())]
        for sysn in (1, 2):
            subprocess.run([PY, ORACLE, module] + flag + ['--system', str(sysn), '--vgm', o('vgm%d' % sysn)],
                           capture_output=True)
            subprocess.run([CLI, module] + flag + ['--date', '2011-11-15', '--no-optimise', '--system', str(sysn),
                            '--vgm', p('vgm%d' % sysn)], capture_output=True)
            outs.append('vgm%d' % sysn)
        for n in outs:
            try:
                a, b = open(o(n), 'rb').read(), open(p(n), 'rb').read()
            except OSError as e:
                res.append((n, 'MISSING ' + str(e)))
                continue
            if n == 'text':
                a = a.replace(b'\r\n', b'\n')
            if n.startswith('vgm') and len(a) > 0x24 and len(b) > 0x24:
                # Loop sample count: the original reads an uninitialised stack variable when the
                # loop point is not position 0; the port counts from the loop point. Not compared.
                a = a[:0x20] + b'\0\0\0\0' + a[0x24:]
                b = b[:0x20] + b'\0\0\0\0' + b[0x24:]
            d = first_diff(a, b)
            res.append((n, 'ok (%d bytes)' % len(a) if d < 0 else
                        'DIFF at 0x%x (oracle %d bytes, port %d bytes): %s | %s' % (
                            d, len(a), len(b), a[d:d + 12].hex(), b[d:d + 12].hex())))
    return res


def main():
    args = sys.argv[1:]
    kind = None
    if args and args[0] in ('--tfe', '--mod'):
        kind = args.pop(0)
    bad = 0
    for m in args:
        res = run(os.path.abspath(m), kind)
        fails = [r for r in res if not r[1].startswith('ok')]
        print('%-60s %s' % (os.path.relpath(m)[-60:], 'PASS' if not fails else 'FAIL'))
        for n, msg in fails:
            print('    %-8s %s' % (n, msg))
        bad += bool(fails)
    print('%d/%d modules identical' % (len(args) - bad, len(args)))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
