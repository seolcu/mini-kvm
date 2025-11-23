import subprocess
import time
import os
import signal

proc = subprocess.Popen(
    [
        'qemu-system-riscv64',
        '-machine', 'virt',
        '-cpu', 'rv64,h=true',
        '-bios', 'default',
        '-smp', '1',
        '-m', '128M',
        '-nographic',
        '-serial', 'mon:stdio',
        '--no-reboot',
        '-kernel', 'hypervisor.elf'
    ],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=0
)

time.sleep(2)

proc.stdin.write('\x01c')
proc.stdin.flush()
time.sleep(0.1)

proc.stdin.write('info registers\n')
proc.stdin.flush()
time.sleep(0.5)

proc.send_signal(signal.SIGTERM)
output, _ = proc.communicate(timeout=2)
print(output)
