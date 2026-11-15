#!/usr/bin/env python3
"""Firmware v2 self-test over USB CDC (no pyserial needed).
Checks: boot hello / ping -> I|fw=2.0, battery reading, approve screen + YES tap."""
import os, sys, time, termios, select, glob

def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0
    a[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

def find_port():
    for _ in range(40):
        p = sorted(glob.glob('/dev/cu.usbmodem*'))
        if p: return p[0]
        time.sleep(0.5)
    return None

def main():
    port = find_port()
    if not port: print('SELFTEST_FAIL no /dev/cu.usbmodem* port'); return 1
    fd = None
    for _ in range(20):
        try: fd = open_port(port); break
        except OSError: time.sleep(0.5)
    if fd is None: print('SELFTEST_FAIL cannot open', port); return 1
    buf = b''
    def pump(t):
        nonlocal buf
        end = time.time() + t
        while time.time() < end:
            r,_,_ = select.select([fd],[],[],0.1)
            if r:
                try: d = os.read(fd, 1024)
                except OSError: d = b''
                if d:
                    buf += d; sys.stdout.write(d.decode('utf-8','replace')); sys.stdout.flush()
    def send(s): os.write(fd, (s+'\n').encode()); print('>>', s)
    pump(4)
    send('S|ping'); pump(1.5)
    ok = True
    info = [l for l in buf.decode('utf-8','replace').splitlines() if l.startswith('I|')]
    if not info: print('SELFTEST_FAIL no I| reply to ping (firmware v2 not running?)'); return 1
    kv = dict(x.split('=',1) for x in info[-1].split('|')[1:] if '=' in x)
    print('\nINFO', kv)
    if kv.get('fw') != '2.0': print('SELFTEST_FAIL fw', kv.get('fw')); ok = False
    print('BATTERY mv=%s pct=%s pwr=%s usb=%s' % (kv.get('bat'), kv.get('pct'), kv.get('pwr'), kv.get('usb')))
    print('\n=== ON THE BOARD: tap the green YES button (60 s) ===\n')
    buf = b''
    send('S|approve|t1|SELFTEST: tap YES to confirm the approve screen works')
    end = time.time() + 60
    while time.time() < end and b'E|approve|t1|' not in buf: pump(0.5)
    if b'E|approve|t1|1' in buf: print('\nAPPROVE_OK (YES tapped)')
    elif b'E|approve|t1|0' in buf: print('\nAPPROVE_OK (NO tapped - screen and touch work)')
    elif b'A|approve' in buf: print('\nAPPROVE_SHOWN_NO_TAP (screen shown, nobody tapped)'); ok = False
    else: print('\nSELFTEST_FAIL approve screen not acknowledged'); ok = False
    pump(4); send('S|idle'); pump(1)
    print('SELFTEST_OK' if ok else 'SELFTEST_DONE_WITH_ISSUES')
    return 0 if ok else 1

sys.exit(main())
