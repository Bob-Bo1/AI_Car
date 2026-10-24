"""Bounded bench command, one selected motor at a time; default is status."""
import argparse
import time
from pathlib import Path
import serial

parser=argparse.ArgumentParser()
parser.add_argument('action',choices=['status','invalid','forward','reverse','stop','measure_forward','measure_reverse','start_forward','start_reverse'],default='status',nargs='?')
parser.add_argument('--duty',type=int,choices=[64,89,128],default=64)
parser.add_argument('--motor-disconnected',action='store_true')
parser.add_argument('--motor',choices=['A','B'],default='A')
parser.add_argument('--port',default='COM6')
parser.add_argument('--wheels-raised',action='store_true')
parser.add_argument('--duration-ms',type=int,choices=[300,2000],default=300)
args=parser.parse_args()
if args.motor=='B' and args.action not in ('status','stop','start_forward','start_reverse'):
    parser.error('Motor B currently supports only bounded start tests')
if args.action in ('start_forward','start_reverse') and not args.wheels_raised:
    parser.error('Starting test requires raised/free wheels and --wheels-raised')
if args.action.startswith('measure_') and not args.motor_disconnected:
    parser.error('Measurement requires physical motor disconnection and --motor-disconnected')
commands={
    'status':b'STATUS\n',
    'invalid':b'MOTOR FWD 129 500\nMOTOR REV 64 1001\nMOTOR BAD 64 500\nSTATUS\n',
    'forward':f'MOTOR FWD {args.duty} 500\n'.encode(),
    'reverse':f'MOTOR REV {args.duty} 500\n'.encode(),
    'stop':b'STOP\nSTATUS\n',
    'measure_forward':b'MEASURE_MOTOR_DISCONNECTED FWD\n',
    'measure_reverse':b'MEASURE_MOTOR_DISCONNECTED REV\n',
    'start_forward':f'MOTOR_START_TEST {args.motor} FWD {args.duration_ms}\n'.encode(),
    'start_reverse':f'MOTOR_START_TEST {args.motor} REV {args.duration_ms}\n'.encode(),
}
observed=[]
with serial.Serial(args.port,115200,timeout=.15) as device:
    pending=b''
    with (Path(__file__).parent/'motor-bench-log.txt').open('a',encoding='utf-8') as log:
        def collect(seconds):
            global pending
            lines=[]
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                pending+=device.read(4096)
                while b'\n' in pending:
                    raw,pending=pending.split(b'\n',1)
                    text=raw.decode('utf-8','replace').strip()
                    if text:
                        with (Path(__file__).parent/'motor-serial-raw.log').open('a',encoding='utf-8') as rawlog:
                            rawlog.write(time.strftime('%Y-%m-%d %H:%M:%S ')+text+'\n')
                    if 'BENCH ' in text:
                        text=text[text.index('BENCH '):]
                        lines.append(text)
                        stamp=time.strftime('%Y-%m-%d %H:%M:%S ')
                        print(stamp+text,flush=True);log.write(stamp+text+'\n');log.flush()
            return lines
        try:
            collect(1)
            # Delimit an incomplete line left by an earlier serial disconnect.
            device.write(b'\nSTOP\nSTATUS\n');device.flush()
            baseline=collect(2)
            if not any('BENCH STATUS' in x and 'motor=stopped' in x for x in baseline):
                raise SystemExit('No idle handshake; test command was not sent')
            device.write(commands[args.action]);device.flush()
            observed+=collect(10 if args.action.startswith('measure_') else 3)
            device.write(b'STATUS\n');device.flush()
            observed+=collect(2)
        finally:
            device.write(b'STOP\n');device.flush();collect(.4)
if args.action in ('forward','reverse','measure_forward','measure_reverse','start_forward','start_reverse'):
    if any('BENCH BOOT' in x or 'MOTOR_ERROR' in x for x in observed):raise SystemExit('Reset or PWM error during test')
    if not any('MOTOR_START' in x for x in observed):raise SystemExit('No motor-start acknowledgment')
    if not any('MOTOR_STOP reason=timeout' in x for x in observed):raise SystemExit('No automatic-stop acknowledgment')
    if args.action.startswith('start_') and not any(f'MOTOR_START motor={args.motor} ' in x for x in observed):
        raise SystemExit('No acknowledgment for the selected motor')
    if not any('BENCH STATUS' in x and 'motor=stopped' in x for x in observed):raise SystemExit('No final stopped status')
if args.action=='invalid':
    if sum('MOTOR_REJECT' in x for x in observed)!=3 or any('MOTOR_START' in x for x in observed):
        raise SystemExit('Invalid-command rejection check failed')
print('Serial checks complete; physical motor result requires observation.')
