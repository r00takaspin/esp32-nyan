import os, time, termios, select

port = '/dev/cu.usbmodem1101'
total = b''

def configure(fd):
    a = termios.tcgetattr(fd)
    a[0] = 0
    a[1] = 0
    a[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    a[3] = 0
    a[4] = termios.B115200
    a[5] = termios.B115200
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, a)

start = time.time()
while time.time() - start < 15:
    try:
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        configure(fd)
        # read up to 0.4s this pass
        p = time.time()
        while time.time() - p < 0.4:
            r, _, _ = select.select([fd], [], [], 0.2)
            if r:
                try:
                    d = os.read(fd, 512)
                    if d:
                        total += d
                except OSError:
                    break
        os.close(fd)
    except OSError:
        pass
    time.sleep(0.1)

print('=== captured ===')
print(total.decode('utf-8', 'replace'))
