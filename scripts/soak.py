#!/usr/bin/env python3
"""Bounded local regression loop. Runs tests, never edits code or uploads files."""
import argparse
from datetime import datetime, timedelta, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
STOP = False


def stop(_signal, _frame):
    global STOP
    STOP = True


def fingerprint():
    digest = hashlib.sha256()
    files = sorted([*ROOT.glob('src/*.c'), *ROOT.glob('include/*.h'),
                    *ROOT.glob('tests/*.*'), ROOT / 'Makefile'])
    for path in files:
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hours', type=float, default=10)
    parser.add_argument('--interval', type=float, default=60, help='Seconds between cycles')
    parser.add_argument('--rounds', type=int, default=10000, help='Random games per cycle')
    parser.add_argument('--log-dir', type=Path, default=ROOT / 'logs')
    args = parser.parse_args()
    if not 0 < args.hours <= 24 or not 0 <= args.interval <= 3600 or not 1 <= args.rounds <= 1000000:
        parser.error('hours: (0,24], interval: [0,3600], rounds: [1,1000000]')
    args.log_dir.mkdir(parents=True, exist_ok=True)
    lock = (args.log_dir / 'soak.lock').open('w')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        parser.error('A regression loop is already running in this log directory')
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, stop)
    started = time.monotonic()
    deadline = started + args.hours * 3600
    wall_start = datetime.now(timezone.utc)
    state = {
        'pid': os.getpid(), 'status': 'running', 'started_at': wall_start.isoformat(),
        'ends_at': (wall_start + timedelta(hours=args.hours)).isoformat(),
        'cycles': 0, 'games': 0, 'failures': 0,
        'interval_seconds': args.interval, 'rounds_per_cycle': args.rounds,
    }
    state_file = args.log_dir / 'soak-status.json'
    state_tmp = args.log_dir / 'soak-status.tmp'

    def save():
        state['updated_at'] = datetime.now(timezone.utc).isoformat()
        state['elapsed_seconds'] = round(time.monotonic() - started, 2)
        state_tmp.write_text(json.dumps(state, indent=2) + '\n')
        state_tmp.replace(state_file)

    def run(command, log):
        log.write('$ ' + ' '.join(command) + '\n')
        log.flush()
        remaining = deadline - time.monotonic()
        if remaining <= 0 or STOP:
            return False
        try:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=min(120, remaining), check=False)
            return result.returncode == 0
        except subprocess.TimeoutExpired:
            log.write('TIMEOUT\n')
            return False

    previous_hash = None
    save()
    try:
        with (args.log_dir / 'soak.log').open('a', buffering=1) as log:
            log.write('\nSTART ' + json.dumps(state) + '\n')
            while not STOP and time.monotonic() < deadline:
                cycle_started = time.monotonic()
                source_hash = fingerprint()
                state['source_sha256'] = source_hash
                seed = secrets.randbits(32)
                state['last_seed'] = seed
                log.write(f'\nCYCLE {state["cycles"] + 1} seed={seed} source={source_hash}\n')
                commands = [['make', 'all', 'build/test_game']]
                if source_hash != previous_hash:
                    commands += [['make', 'sanitize'], [sys.executable, 'tests/test_terminal.py', './burger']]
                commands += [['./build/test_game', str(seed), str(args.rounds)]]
                ok = True
                for command in commands:
                    if not run(command, log):
                        ok = False
                        break
                if not ok:
                    if STOP:
                        state['status'] = 'stopped'
                    elif time.monotonic() >= deadline:
                        state['status'] = 'completed'
                        state['last_cycle'] = 'interrupted_at_deadline'
                    else:
                        state['status'] = 'failed'
                        state['failures'] += 1
                        state['failed_command'] = command
                    break
                previous_hash = source_hash
                state['cycles'] += 1
                state['games'] += args.rounds
                state['last_cycle'] = 'passed'
                save()
                log.write('PASS ' + json.dumps(state) + '\n')
                # Bound each sleep so SIGTERM/STOP is handled promptly.
                next_cycle = min(deadline, cycle_started + args.interval)
                while not STOP and time.monotonic() < next_cycle:
                    time.sleep(min(1, next_cycle - time.monotonic()))
            if state['status'] == 'running':
                state['status'] = 'stopped' if STOP else 'completed'
            save()
            log.write('END ' + json.dumps(state) + '\n')
    except Exception as error:
        state['status'] = 'failed'
        state['error'] = str(error)
        state['failures'] += 1
        save()
        raise
    finally:
        lock.close()
    return 1 if state['status'] == 'failed' else 0


if __name__ == '__main__':
    raise SystemExit(main())
