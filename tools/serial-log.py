import serial, sys, time, os
port, logf, pidf = sys.argv[1], sys.argv[2], sys.argv[3]
open(pidf, 'w').write(str(os.getpid()))
first = True
def note(f, m): f.write(("\n=== %s %s ===\n" % (time.strftime("%H:%M:%S"), m)).encode())
with open(logf, 'ab', buffering=0) as f:
    while True:
        try:
            s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.5
            s.dtr = False; s.rts = False
            s.open()
        except Exception:
            time.sleep(1); continue
        note(f, "logger connected" + (" (reset)" if first else ""))
        if first:
            s.rts = True; time.sleep(0.1); s.rts = False; first = False
        try:
            while True:
                d = s.read(4096)
                if d: f.write(d)
        except Exception as e:
            note(f, "port lost: %s" % type(e).__name__)
            try: s.close()
            except Exception: pass
            time.sleep(1)
