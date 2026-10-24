"""Read only BENCH diagnostics; never accepts network credentials."""
import sys
import time
from pathlib import Path
import serial

command = sys.argv[1] if len(sys.argv) > 1 else 'STATUS'
if command not in ('STATUS', 'SCAN', 'REBOOT'):
    raise SystemExit('Allowed commands: STATUS SCAN REBOOT')
with serial.Serial('COM6',115200,timeout=.25) as device:
    device.write((command+'\n').encode())
    end=time.monotonic()+25
    pending=b''
    with (Path(__file__).parent/'bench-log.txt').open('a',encoding='utf-8') as log:
        while time.monotonic()<end:
            pending+=device.read(4096)
            while b'\n' in pending:
                raw,pending=pending.split(b'\n',1)
                text=raw.decode('utf-8','replace').strip()
                if 'BENCH ' in text:
                    text=text[text.index('BENCH '):]
                    print(text,flush=True)
                    log.write(time.strftime('%Y-%m-%d %H:%M:%S ')+text+'\n')
                    log.flush()
