import os
import sys
import argparse
import struct

try:
    import numpy as np
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "numpy"])
    import numpy as np

try:
    import miniaudio
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "miniaudio"])
    import miniaudio

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "matplotlib"])
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt


INDEX_TABLE = [
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
]

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
]

INDEX_TABLE_2BIT = [-1, 1, -1, 2]

BLOCK_FRAMES = 256
NATIVE_RATE  = 44100

RMS_SILENCE_THRESHOLD = 64
DC_DOMINANCE_RATIO    = 0.99
SNR_THRESHOLDS        = (22.0, 30.0)
NOISE_SHAPING_ENABLED = True


def detect_profile(filename):
    name = os.path.basename(filename).lower()
    stem = os.path.splitext(name)[0]
    if ('_music' in stem or '_bgm' in stem
            or '_ambient' in stem or 'loop' in stem
            or 'theme' in stem or 'song' in stem):
        return 'music'
    return 'sfx'


def decode_audio_file(file_path):
    try:
        decoded = miniaudio.decode_file(
            file_path,
            output_format=miniaudio.SampleFormat.SIGNED16,
            nchannels=1,
            sample_rate=NATIVE_RATE
        )
        samples = np.frombuffer(decoded.samples, dtype=np.int16).astype(np.float32)
        return samples, decoded.sample_rate, 1
    except Exception:
        decoded = miniaudio.decode_file(file_path, output_format=miniaudio.SampleFormat.SIGNED16)
        samples = np.frombuffer(decoded.samples, dtype=np.int16).astype(np.float32)
        nch = decoded.nchannels
        if nch == 2:
            samples = samples.reshape(-1, 2).mean(axis=1)
        return samples, decoded.sample_rate, 1


def pick_source_rate(samples, native_rate, profile_name):
    if native_rate <= 22050:
        return min(native_rate, 22050)

    WINDOW = 4096
    HOP    = 2048
    n = len(samples)
    if n < WINDOW:
        return NATIVE_RATE

    s = samples - np.mean(samples)
    if np.max(np.abs(s)) < 1.0:
        return 11025

    win = np.hanning(WINDOW)
    fft_bins = np.fft.rfftfreq(WINDOW, d=1.0 / native_rate)
    bin_11k = np.searchsorted(fft_bins, 11000.0)

    high11_count = 0
    n_windows    = 0

    for start in range(0, n - WINDOW, HOP):
        chunk = s[start:start + WINDOW] * win
        spec  = np.abs(np.fft.rfft(chunk))
        total = np.sum(spec * spec) + 1e-12
        high11_ratio = np.sum(spec[bin_11k:] ** 2) / total
        if high11_ratio > 0.15:
            high11_count += 1
        n_windows += 1

    if n_windows == 0:
        return NATIVE_RATE

    high11_pct = high11_count / n_windows

    if profile_name == 'music':
        if high11_pct < 0.60:
            return 22050
        return 44100
    else:
        if high11_pct > 0.30:
            return 44100
        if high11_pct > 0.10:
            return 22050
        return 11025


def design_lowpass(num_taps, cutoff_hz, fs):
    if num_taps % 2 == 0:
        num_taps += 1
    n = np.arange(num_taps)
    mid = (num_taps - 1) / 2.0
    fc = cutoff_hz / fs
    h = np.sinc(2.0 * fc * (n - mid))
    h *= np.hanning(num_taps)
    h /= np.sum(h)
    return h


def lowpass_and_downsample(samples, native_rate, target_rate):
    if target_rate >= native_rate:
        return samples.astype(np.float32)
    cutoff = target_rate * 0.45
    num_taps = 64 if target_rate == 22050 else 127
    h = design_lowpass(num_taps, cutoff, native_rate)

    n = len(samples)
    conv_len = n + num_taps - 1
    fft_len = 1
    while fft_len < conv_len:
        fft_len *= 2
    H = np.fft.rfft(h, fft_len)
    X = np.fft.rfft(samples, fft_len)
    y = np.fft.irfft(X * H, fft_len)[:conv_len]
    delay = (num_taps - 1) // 2
    y = y[delay:delay + n]

    step = native_rate // target_rate
    y = y[::step]
    return y.astype(np.float32)


def _encode_4bit_sample(sample, predictor, step_index):
    step = STEP_TABLE[step_index]
    diff = int(sample) - predictor
    sign = 0
    if diff < 0:
        sign = 8
        diff = -diff

    code = 0
    delta = step >> 3
    if diff >= step:
        code |= 4
        diff -= step
        delta += step
    if diff >= (step >> 1):
        code |= 2
        diff -= (step >> 1)
        delta += (step >> 1)
    if diff >= (step >> 2):
        code |= 1
        delta += (step >> 2)

    code |= sign
    if sign:
        predictor = max(-32768, predictor - delta)
    else:
        predictor = min(32767, predictor + delta)

    step_index = max(0, min(88, step_index + INDEX_TABLE[code & 7]))
    return code, predictor, step_index


def _encode_6bit_sample(sample, predictor, step_index):
    step = STEP_TABLE[step_index]
    diff = int(sample) - predictor
    sign = 0
    if diff < 0:
        sign = 32
        diff = -diff

    code = 0
    delta = step >> 5
    if diff >= step:
        code |= 16
        diff -= step
        delta += step
    if diff >= (step >> 1):
        code |= 8
        diff -= (step >> 1)
        delta += (step >> 1)
    if diff >= (step >> 2):
        code |= 4
        diff -= (step >> 2)
        delta += (step >> 2)
    if diff >= (step >> 3):
        code |= 2
        diff -= (step >> 3)
        delta += (step >> 3)
    if diff >= (step >> 4):
        code |= 1
        delta += (step >> 4)

    code |= sign
    if sign:
        predictor = max(-32768, predictor - delta)
    else:
        predictor = min(32767, predictor + delta)

    adapt_idx = (code >> 1) & 0x0F
    step_index = max(0, min(88, step_index + INDEX_TABLE[adapt_idx]))
    return code, predictor, step_index


def _encode_2bit_sample(sample, predictor, step_index):
    step = STEP_TABLE[step_index]
    diff = int(sample) - predictor

    if diff >= 0:
        if diff >= step:
            code = 1
            delta = step
        else:
            code = 0
            delta = step >> 2
        predictor = min(32767, predictor + delta)
    else:
        if -diff >= step:
            code = 3
            delta = step
        else:
            code = 2
            delta = step >> 2
        predictor = max(-32768, predictor - delta)

    step_index = max(0, min(88, step_index + INDEX_TABLE_2BIT[code]))
    return code, predictor, step_index


def _initial_state(block_samples):
    predictor = int(np.clip(int(block_samples[0]), -32768, 32767))

    n_analyze = min(16, len(block_samples))
    if n_analyze < 2:
        return predictor, 0

    diffs = np.abs(np.diff(block_samples[:n_analyze].astype(np.int32)))
    avg_diff = int(np.mean(diffs))
    max_diff = int(np.max(diffs))

    step_index = 0
    for idx, st in enumerate(STEP_TABLE):
        if st >= avg_diff:
            step_index = idx
            break
    else:
        step_index = 88

    if max_diff > avg_diff * 4 and step_index < 80:
        step_index = min(88, step_index + 4)

    return predictor, step_index


def encode_block_adpcm(block_samples, bits, noise_shaping=True):
    predictor, step_index = _initial_state(block_samples)
    err_hist = 0.0

    state = struct.pack("<hB", predictor, step_index & 0xFF)
    data = bytearray()

    if bits == 2:
        packer = _encode_2bit_sample
        per_sample_bits = 2
    elif bits == 4:
        packer = _encode_4bit_sample
        per_sample_bits = 4
    elif bits == 6:
        packer = _encode_6bit_sample
        per_sample_bits = 6
    else:
        raise ValueError(f"Unsupported bits: {bits}")

    codes = []
    for i in range(1, BLOCK_FRAMES):
        target = int(block_samples[i])

        if noise_shaping and NOISE_SHAPING_ENABLED:
            shaped_target = target - int(err_hist * 0.5)
            shaped_target = max(-32768, min(32767, shaped_target))
        else:
            shaped_target = target

        code, predictor, step_index = packer(shaped_target, predictor, step_index)
        err_hist = (shaped_target - predictor)
        codes.append(code)

    codes.append(0)

    if per_sample_bits == 2:
        for i in range(0, len(codes), 4):
            b = 0
            for j in range(4):
                if i + j < len(codes):
                    b |= (codes[i + j] & 0x03) << (j * 2)
            data.append(b)
    elif per_sample_bits == 4:
        for i in range(0, len(codes), 2):
            b = 0
            for j in range(2):
                if i + j < len(codes):
                    b |= (codes[i + j] & 0x0F) << (j * 4)
            data.append(b)
    elif per_sample_bits == 6:
        for i in range(0, len(codes), 4):
            chunk = 0
            for j in range(4):
                if i + j < len(codes):
                    chunk |= (codes[i + j] & 0x3F) << (j * 6)
            data.append(chunk & 0xFF)
            data.append((chunk >> 8) & 0xFF)
            data.append((chunk >> 16) & 0xFF)

    return bytes(state), bytes(data)


def _decode_block_for_snr(block_samples, bits, state_bytes, data_bytes):
    predictor, = struct.unpack("<h", state_bytes[:2])
    if predictor >= 32768:
        predictor -= 65536
    step_index = state_bytes[2]
    recon = np.zeros(BLOCK_FRAMES, dtype=np.float32)
    recon[0] = predictor

    if bits == 2:
        codes = []
        for b in data_bytes:
            codes.append(b & 0x03)
            codes.append((b >> 2) & 0x03)
            codes.append((b >> 4) & 0x03)
            codes.append((b >> 6) & 0x03)
        for i in range(1, BLOCK_FRAMES):
            c = codes[i - 1]
            step = STEP_TABLE[step_index]
            mag = c & 1
            sign = (c >> 1) & 1
            delta = step if mag else (step >> 2)
            if sign:
                predictor = max(-32768, predictor - delta)
            else:
                predictor = min(32767, predictor + delta)
            step_index = max(0, min(88, step_index + INDEX_TABLE_2BIT[c]))
            recon[i] = predictor
    elif bits == 4:
        codes = []
        for b in data_bytes:
            codes.append(b & 0x0F)
            codes.append((b >> 4) & 0x0F)
        for i in range(1, BLOCK_FRAMES):
            c = codes[i - 1]
            step = STEP_TABLE[step_index]
            diff = step >> 3
            if c & 4: diff += step
            if c & 2: diff += step >> 1
            if c & 1: diff += step >> 2
            if c & 8:
                predictor = max(-32768, predictor - diff)
            else:
                predictor = min(32767, predictor + diff)
            step_index = max(0, min(88, step_index + INDEX_TABLE[c & 7]))
            recon[i] = predictor
    elif bits == 6:
        codes = []
        for i in range(0, len(data_bytes), 3):
            chunk = data_bytes[i] | (data_bytes[i + 1] << 8) | (data_bytes[i + 2] << 16)
            for j in range(4):
                codes.append((chunk >> (j * 6)) & 0x3F)
        for i in range(1, BLOCK_FRAMES):
            c = codes[i - 1]
            step = STEP_TABLE[step_index]
            diff = step >> 5
            if c & 16: diff += step
            if c & 8:  diff += step >> 1
            if c & 4:  diff += step >> 2
            if c & 2:  diff += step >> 3
            if c & 1:  diff += step >> 4
            if c & 32:
                predictor = max(-32768, predictor - diff)
            else:
                predictor = min(32767, predictor + diff)
            adapt_idx = (c >> 1) & 0x0F
            step_index = max(0, min(88, step_index + INDEX_TABLE[adapt_idx]))
            recon[i] = predictor

    return recon


def estimate_snr(block_int16, bits):
    state, data = encode_block_adpcm(block_int16, bits, noise_shaping=False)
    recon = _decode_block_for_snr(block_int16, bits, state, data)
    err = block_int16.astype(np.float32) - recon
    sig_power = np.sum(block_int16.astype(np.float32) ** 2) + 1e-12
    err_power = np.sum(err ** 2) + 1e-12
    snr_db = 10.0 * np.log10(sig_power / err_power)
    return float(snr_db)


def compute_rms(block):
    return float(np.sqrt(np.mean(block.astype(np.float64) ** 2)))


def pick_block_mode(block_int16):
    rms = compute_rms(block_int16)
    if rms < RMS_SILENCE_THRESHOLD:
        return 0

    mean_val = float(np.mean(block_int16))
    peak_val = float(np.max(np.abs(block_int16 - mean_val)))
    if peak_val > 0 and abs(mean_val) > 0 and abs(mean_val) >= DC_DOMINANCE_RATIO * (abs(mean_val) + peak_val):
        return 4

    thr_2bit, thr_4bit = SNR_THRESHOLDS

    snr2 = estimate_snr(block_int16, 2)
    if snr2 >= thr_2bit:
        return 1

    snr4 = estimate_snr(block_int16, 4)
    if snr4 >= thr_4bit:
        return 2

    return 3


PAC_FLAG_LOOP = 1


def encode_pac(samples_float, native_rate, is_loop, source_rate=None, profile_name='music'):
    max_val = float(np.max(np.abs(samples_float)))
    if max_val > 0:
        scale = 29000.0 / max_val
        samples_float = samples_float * scale
    samples_int16 = np.clip(samples_float, -32768, 32767).astype(np.int16)

    if source_rate is None:
        source_rate = pick_source_rate(samples_int16, native_rate, profile_name)
    if source_rate not in (11025, 22050, 44100):
        if source_rate <= 16000:
            source_rate = 11025
        elif source_rate <= 32000:
            source_rate = 22050
        else:
            source_rate = 44100

    if source_rate != native_rate:
        ds = lowpass_and_downsample(samples_int16.astype(np.float32), native_rate, source_rate)
        samples_int16 = np.clip(ds, -32768, 32767).astype(np.int16)

    n_samples = len(samples_int16)
    pad = (-n_samples) % BLOCK_FRAMES
    if pad > 0:
        samples_int16 = np.concatenate([samples_int16, np.zeros(pad, dtype=np.int16)])
    n_blocks = len(samples_int16) // BLOCK_FRAMES
    frame_count_native = n_samples

    data = bytearray()
    block_modes = {0: 0, 1: 0, 2: 0, 3: 0, 4: 0}
    block_snrs = []
    block_modes_list = []

    for b in range(n_blocks):
        block = samples_int16[b * BLOCK_FRAMES:(b + 1) * BLOCK_FRAMES]
        mode = pick_block_mode(block)
        block_modes[mode] = block_modes.get(mode, 0) + 1
        block_modes_list.append(mode)

        if mode == 0:
            data.append(0x00)
            block_snrs.append(0.0)
        elif mode == 4:
            data.append(0x40)
            dc_val = int(np.clip(np.mean(block), -32768, 32767))
            data += struct.pack("<h", dc_val)
            block_snrs.append(99.0)
        else:
            mode_byte = mode << 4
            data.append(mode_byte)
            bits = {1: 2, 2: 4, 3: 6}[mode]
            state, payload = encode_block_adpcm(block, bits, noise_shaping=True)
            data += state
            data += payload
            snr = estimate_snr(block, bits)
            block_snrs.append(snr)

    flags = PAC_FLAG_LOOP if is_loop else 0
    header = struct.pack(
        "<4sHHIIIIIIIIII",
        b"PAC!", 0, flags,
        source_rate, NATIVE_RATE,
        frame_count_native,
        0, frame_count_native,
        n_blocks,
        48, len(data),
        0, 0
    )

    info = {
        'source_rate': source_rate,
        'native_rate': NATIVE_RATE,
        'frame_count': frame_count_native,
        'block_count': n_blocks,
        'data_size': len(data),
        'is_loop': is_loop,
        'mode_stats': block_modes,
        'mode_list': block_modes_list,
        'profile': profile_name,
        'compression_ratio': (frame_count_native * 2) / max(1, len(data)),
        'block_snrs': block_snrs,
        'samples_int16': samples_int16[:frame_count_native],
    }
    return header + bytes(data), info


def export_hpp(clean_name, payload, info, src_channels, src_rate, output_hpp_path):
    var_name = clean_name.replace('-', '_').replace(' ', '_')
    source_file = os.path.basename(output_hpp_path).replace('.hpp', '')
    os.makedirs(os.path.dirname(output_hpp_path), exist_ok=True)

    downmix_str = "Yes" if src_channels == 2 else "No"
    stats = info['mode_stats']
    total_blocks = info['block_count']

    with open(output_hpp_path, 'w', encoding='utf-8') as out:
        out.write("/*\n")
        out.write(f" * Pip3D Sound Asset — {clean_name}\n")
        out.write(f" * Source      : {source_file} ({'Stereo' if src_channels == 2 else 'Mono'}, {src_rate} Hz)\n")
        out.write(f" * Format      : PAC\n")
        out.write(f" * Profile     : {info['profile']}\n")
        out.write(f" * Source rate : {info['source_rate']} Hz\n")
        out.write(f" * Native rate : {info['native_rate']} Hz\n")
        out.write(f" * Downmix     : {downmix_str}\n")
        out.write(f" * Loop        : {'Yes' if info['is_loop'] else 'No'}\n")
        out.write(f" * Duration    : {info['frame_count'] / info['source_rate']:.2f} s ({info['frame_count']} samples)\n")
        out.write(f" * Blocks      : {total_blocks}\n")
        out.write(f" * Flash       : {len(payload)} bytes ({len(payload) / 1024.0:.2f} KB)\n")
        out.write(f" * Compression : {info['compression_ratio']:.2f}x vs PCM16 mono\n")
        out.write(f" * Modes       : silence={stats.get(0,0)} 2bit={stats.get(1,0)} 4bit={stats.get(2,0)} 6bit={stats.get(3,0)} hold={stats.get(4,0)}\n")
        out.write(" */\n\n")
        out.write("#pragma once\n\n")
        out.write("#include <cstdint>\n")
        out.write("#include <cstddef>\n\n")
        out.write("namespace pip3D::sounds\n{\n")
        out.write(f"    alignas(4) static const uint8_t g_{var_name}Data[{len(payload)}] = {{\n")
        for i in range(0, len(payload), 16):
            chunk = payload[i:i + 16]
            hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
            out.write(f"        {hex_str},\n")
        out.write("    };\n")
        out.write(f"    static constexpr size_t g_{var_name}Size = sizeof(g_{var_name}Data);\n")
        out.write("}\n")


def export_visualisation(clean_name, info, src_channels, src_rate, output_png_path):
    from matplotlib.patches import Patch

    os.makedirs(os.path.dirname(output_png_path), exist_ok=True)

    samples = info['samples_int16']
    source_rate = info['source_rate']
    duration_s = len(samples) / source_rate
    block_snrs = info['block_snrs']
    mode_stats = info['mode_stats']

    fig = plt.figure(figsize=(16, 10), constrained_layout=True)
    gs = fig.add_gridspec(4, 1, height_ratios=[1.2, 1.5, 0.6, 0.6])

    ax_wav = fig.add_subplot(gs[0, 0])
    t = np.arange(len(samples)) / source_rate
    ax_wav.plot(t, samples, linewidth=0.3, color='#1f77b4')
    ax_wav.set_xlim(0, duration_s)
    ax_wav.set_ylim(-32768, 32767)
    ax_wav.set_ylabel('Amplitude')
    ax_wav.set_title(f'PAC visualisation: {clean_name}   '
                     f'({src_channels}ch @ {src_rate} Hz → PAC @ {source_rate} Hz, '
                     f'{len(samples)/source_rate:.2f}s, '
                     f'{info["data_size"]/1024:.1f} KB, '
                     f'{info["compression_ratio"]:.2f}x)')
    ax_wav.grid(True, alpha=0.3)

    ax_spec = fig.add_subplot(gs[1, 0])
    nperseg = 512
    noverlap = nperseg * 3 // 4
    window = np.hanning(nperseg)

    step = nperseg - noverlap
    n_frames = max(1, (len(samples) - nperseg) // step + 1)
    Pxx = np.zeros((nperseg // 2 + 1, n_frames), dtype=np.float32)
    for i in range(n_frames):
        start = i * step
        chunk = samples[start:start + nperseg].astype(np.float32) * window
        if len(chunk) < nperseg:
            chunk = np.pad(chunk, (0, nperseg - len(chunk)))
        spec = np.abs(np.fft.rfft(chunk)) ** 2
        Pxx[:, i] = spec

    Pxx_log = 10.0 * np.log10(Pxx + 1e-12)
    freqs = np.fft.rfftfreq(nperseg, d=1.0 / source_rate)
    times = np.arange(n_frames) * step / source_rate

    ax_spec.pcolormesh(times, freqs, Pxx_log, shading='auto', cmap='magma',
                       vmin=np.max(Pxx_log) - 80)
    ax_spec.set_ylim(0, source_rate / 2)
    ax_spec.set_xlim(0, duration_s)
    ax_spec.set_ylabel('Frequency (Hz)')
    ax_spec.set_xlabel('Time (s)')

    ax_modes = fig.add_subplot(gs[2, 0])
    block_count = info['block_count']
    mode_names = {0: 'silence', 1: '2-bit', 2: '4-bit', 3: '6-bit', 4: 'hold'}
    mode_colors = {0: '#222222', 1: '#2ca02c', 2: '#1f77b4', 3: '#d62728', 4: '#ff7f0e'}

    block_duration = BLOCK_FRAMES / source_rate
    block_times = np.arange(block_count) * block_duration
    modes_seq = info.get('mode_list', [3] * block_count)

    for i, m in enumerate(modes_seq):
        ax_modes.axvspan(block_times[i], block_times[i] + block_duration,
                         color=mode_colors.get(m, '#888'), alpha=0.7)

    ax_modes.set_xlim(0, duration_s)
    ax_modes.set_ylim(0, 1)
    ax_modes.set_yticks([])
    ax_modes.set_xlabel('Time (s)')
    ax_modes.set_title(f'Block modes  (silence={mode_stats.get(0,0)}  2bit={mode_stats.get(1,0)}  '
                       f'4bit={mode_stats.get(2,0)}  6bit={mode_stats.get(3,0)}  hold={mode_stats.get(4,0)})')

    legend_elements = [Patch(facecolor=mode_colors[k], label=v) for k, v in mode_names.items() if mode_stats.get(k, 0) > 0]
    ax_modes.legend(handles=legend_elements, loc='upper right', fontsize=8, ncol=5)

    ax_snr = fig.add_subplot(gs[3, 0])
    snr_array = np.array(block_snrs)
    snr_plot = snr_array.copy()
    snr_plot[snr_plot == 0] = np.nan
    snr_plot[snr_plot == 99] = np.nan

    ax_snr.plot(block_times, snr_plot, marker='.', linestyle='-', linewidth=0.5,
                markersize=2, color='#9467bd')
    ax_snr.axhline(y=SNR_THRESHOLDS[0], color='green', linestyle='--', alpha=0.5, label=f'2-bit threshold ({SNR_THRESHOLDS[0]} dB)')
    ax_snr.axhline(y=SNR_THRESHOLDS[1], color='blue', linestyle='--', alpha=0.5, label=f'4-bit threshold ({SNR_THRESHOLDS[1]} dB)')
    ax_snr.set_xlim(0, duration_s)
    ax_snr.set_ylim(0, 60)
    ax_snr.set_ylabel('SNR (dB)')
    ax_snr.set_xlabel('Time (s)')
    ax_snr.legend(loc='upper right', fontsize=8)
    ax_snr.grid(True, alpha=0.3)

    fig.savefig(output_png_path, dpi=120)
    plt.close(fig)
    return True


def process_audio(file_path, force_source_rate=None):
    filename = os.path.basename(file_path).lower()
    name_no_ext = os.path.splitext(filename)[0]

    profile = detect_profile(file_path)
    is_loop = ('loop' in name_no_ext
               or '_music' in name_no_ext
               or '_bgm' in name_no_ext
               or '_ambient' in name_no_ext)

    samples, src_rate, src_channels = decode_audio_file(file_path)
    if samples is None:
        return None, None, None, None, None

    payload, info = encode_pac(samples, src_rate, is_loop,
                               source_rate=force_source_rate,
                               profile_name=profile)
    return name_no_ext, payload, info, src_channels, src_rate


def convert_one(input_path, output_path, force_source_rate=None, no_viz=False):
    name, payload, info, src_ch, src_rate = process_audio(input_path, force_source_rate=force_source_rate)
    if payload is None:
        print(f"\033[91m[PAC] Failed to encode {input_path}\033[0m")
        sys.exit(1)

    export_hpp(name, payload, info, src_ch, src_rate, output_path)
    print(f"\033[32m[PAC]\033[0m {input_path} -> {output_path}")
    print(f"       Profile: {info['profile']}")
    print(f"       Source: {info['source_rate']} Hz, {info['frame_count']} samples, "
          f"{info['frame_count'] / info['source_rate']:.2f} s")
    print(f"       Output: {len(payload)} bytes ({len(payload) / 1024.0:.2f} KB), "
          f"compression {info['compression_ratio']:.2f}x vs PCM16")
    print(f"       Modes: silence={info['mode_stats'].get(0,0)} 2bit={info['mode_stats'].get(1,0)} "
          f"4bit={info['mode_stats'].get(2,0)} 6bit={info['mode_stats'].get(3,0)} hold={info['mode_stats'].get(4,0)}")

    if no_viz:
        return

    hpp_abs = os.path.abspath(output_path)
    parts = hpp_abs.split(os.sep)
    candidates = []
    if 'lib' in parts:
        lib_idx = parts.index('lib')
        project_root = os.sep.join(parts[:lib_idx])
        candidates.append(os.path.join(project_root, 'Tools', 'Audio', 'Output'))
    candidates.append(os.path.join(os.getcwd(), 'Tools', 'Audio', 'Output'))

    viz_dir = None
    for c in candidates:
        try:
            os.makedirs(c, exist_ok=True)
            test_file = os.path.join(c, '.viz_test')
            with open(test_file, 'w') as f:
                f.write('test')
            os.remove(test_file)
            viz_dir = c
            break
        except (OSError, PermissionError):
            continue

    if viz_dir is None:
        viz_dir = os.path.dirname(os.path.abspath(output_path))

    viz_path = os.path.join(viz_dir, f"{name}.png")
    ok = export_visualisation(name, info, src_ch, src_rate, viz_path)
    if ok:
        print(f"       Visualisation: {viz_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Pip3D PAC encoder")
    parser.add_argument("pairs", nargs="+",
                        help="input output, or in1 out1 in2 out2 ... for batch")
    parser.add_argument("--source-rate", type=int, default=None,
                        help="Force source rate (11025/22050/44100). Default: auto.")
    parser.add_argument("--no-viz", action="store_true",
                        help="Skip PNG visualisation output")
    args = parser.parse_args()

    files = args.pairs
    if len(files) % 2 != 0:
        parser.error("expected input/output pairs: in1 out1 [in2 out2 ...]")

    for i in range(0, len(files), 2):
        convert_one(files[i], files[i + 1],
                    force_source_rate=args.source_rate,
                    no_viz=args.no_viz)
