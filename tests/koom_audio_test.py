#!/usr/bin/env python3
"""Render real Freedoom audio without opening an audio device."""
import array
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
helper = root / ('build-koom-windows/kalwer-koom.exe' if os.name == 'nt' else 'build-koom/kalwer-koom')
with tempfile.TemporaryDirectory(prefix='kalwer-koom-audio-zażółć-') as folder:
    patch = Path(folder) / 'świat.wad'
    patch.write_bytes(b'PWAD' + struct.pack('<II', 1, 12) + struct.pack('<II8s', 12, 0, b'KALWER'))
    for music in (False, True):
        pcm = Path(folder) / ('music.f32' if music else 'effects.f32')
        env = dict(os.environ, KALWER_KOOM_AUDIO_CAPTURE=str(pcm))
        command = [str(helper), '-iwad', str(root / 'assets/koom/freedoom2.wad'),
                   '-soundfont', str(root / 'assets/koom/TimGM6mb.sf2'),
                   '-config', str(Path(folder) / 'doom.cfg'), '-file', str(patch), '-warp', '1', '-skill', '2', '-nomonsters']
        if music:
            command += ['-kalwer-fieldkit']
        if not music:
            command += ['-nomusic']
        with open(Path(folder) / 'runtime.log', 'wb') as log:
            child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, env=env)
            try:
                frames = 90
                for index in range(frames):
                    keys = bytearray(256)
                    if not music and index > 10:
                        keys[0xa3] = 1
                    child.stdin.write(keys)
                    child.stdin.flush()
                    header = child.stdout.read(16)
                    assert len(header) == 16 and struct.unpack('<4I', header)[:3] == (0x4b4f4f4d, 320, 200)
                    pixels = child.stdout.read(320 * 200 * 4)
                    assert len(pixels) == 320 * 200 * 4
                child.stdin.close()
                assert child.wait(timeout=10) == 0
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
        kit_log = (Path(folder) / 'runtime.log').read_text(errors='replace')
        assert kit_log.count('Kalwer field kit: health 150, armor 150, map 1:1') == (1 if music else 0), kit_log[-3000:]
        samples = array.array('f')
        samples.frombytes(pcm.read_bytes())
        assert len(samples) == frames * 1392 * 2
        assert all(math.isfinite(value) and -1 <= value <= 1 for value in samples)
        peak = max(abs(value) for value in samples)
        assert peak > .01, (music, peak)
        print(('Music' if music else 'Sound effects') + f': {len(samples)//2} stereo frames, peak {peak:.3f}')
