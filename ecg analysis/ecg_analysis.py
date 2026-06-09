"""
ECG Signal Simulation, Filtering, R-Peak Detection & Heart Rate Calculation
============================================================================
Biomedical Engineering Demo | SciPy + Matplotlib
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from scipy import signal
from scipy.signal import butter, filtfilt, find_peaks

# ── Matplotlib style ──────────────────────────────────────────────────────────
plt.rcParams.update({
    "figure.facecolor":  "#0d1117",
    "axes.facecolor":    "#0d1117",
    "axes.edgecolor":    "#30363d",
    "axes.labelcolor":   "#c9d1d9",
    "xtick.color":       "#8b949e",
    "ytick.color":       "#8b949e",
    "text.color":        "#c9d1d9",
    "grid.color":        "#21262d",
    "grid.linestyle":    "--",
    "grid.linewidth":    0.6,
    "font.family":       "monospace",
})

ACCENT   = "#00d4aa"   # teal  – filtered / clean signal
RAW_COL  = "#ff6b6b"   # coral – raw noisy signal
PEAK_COL = "#ffd166"   # amber – R-peaks
BG_PANEL = "#161b22"   # slightly lighter panel bg


# ═══════════════════════════════════════════════════════════════════════════════
# 1.  SYNTHETIC ECG WAVEFORM
# ═══════════════════════════════════════════════════════════════════════════════

def gaussian(t, mu, sigma, amp):
    return amp * np.exp(-((t - mu) ** 2) / (2 * sigma ** 2))


def single_beat(t_beat):
    """Construct one PQRST complex centred in a unit-time window."""
    ecg = np.zeros_like(t_beat)
    # P wave
    ecg += gaussian(t_beat, 0.20, 0.025, 0.15)
    # Q dip
    ecg += gaussian(t_beat, 0.35, 0.012, -0.10)
    # R spike
    ecg += gaussian(t_beat, 0.40, 0.015,  1.00)
    # S dip
    ecg += gaussian(t_beat, 0.45, 0.012, -0.20)
    # T wave
    ecg += gaussian(t_beat, 0.60, 0.040,  0.25)
    return ecg


def synthesise_ecg(duration=10.0, fs=500.0, bpm=72, noise_level=0.15,
                   seed=42):
    """
    Generate a realistic synthetic ECG with layered noise.

    Parameters
    ----------
    duration    : signal length in seconds
    fs          : sampling frequency (Hz)
    bpm         : target heart rate
    noise_level : amplitude of added noise
    seed        : random seed for reproducibility

    Returns
    -------
    t    : time axis
    ecg  : clean ECG
    noisy: ECG + noise
    """
    rng = np.random.default_rng(seed)
    N   = int(duration * fs)
    t   = np.linspace(0, duration, N, endpoint=False)
    ecg = np.zeros(N)

    beat_period = 60.0 / bpm          # seconds per beat
    beat_len    = int(beat_period * fs)
    t_beat      = np.linspace(0, 1, beat_len, endpoint=False)
    template    = single_beat(t_beat)

    beat_start = 0
    while beat_start + beat_len <= N:
        ecg[beat_start: beat_start + beat_len] += template
        # slight variability in RR interval (±3 %)
        jitter     = int(rng.uniform(-0.03, 0.03) * beat_len)
        beat_start += beat_len + jitter

    # ── Noise layers ──────────────────────────────────────────────────────────
    # 1. White (muscle artifact / EMG)
    white = rng.normal(0, noise_level, N)

    # 2. Baseline wander (respiratory, ~0.3 Hz)
    wander_freq = 0.3
    wander = 0.12 * np.sin(2 * np.pi * wander_freq * t)

    # 3. Power-line interference (50 Hz)
    powerline = 0.05 * np.sin(2 * np.pi * 50 * t)

    noisy = ecg + white + wander + powerline
    return t, ecg, noisy


# ═══════════════════════════════════════════════════════════════════════════════
# 2.  BANDPASS FILTER
# ═══════════════════════════════════════════════════════════════════════════════

def bandpass_filter(data, lowcut=0.5, highcut=40.0, fs=500.0, order=4):
    """
    Zero-phase Butterworth bandpass filter.

    Clinical ECG bandwidth: 0.5 – 40 Hz  (AHA recommendation for diagnostics).
    Uses `filtfilt` for zero phase distortion.
    """
    nyq = fs / 2.0
    b, z = butter(order, [lowcut / nyq, highcut / nyq], btype="band")
    return filtfilt(b, z, data)


# ═══════════════════════════════════════════════════════════════════════════════
# 3.  R-PEAK DETECTION  (Pan–Tompkins inspired)
# ═══════════════════════════════════════════════════════════════════════════════

def detect_r_peaks(ecg_filtered, fs=500.0):
    """
    Robust R-peak detection via differentiation + squaring + moving average.

    Steps
    -----
    1. Differentiate   – enhance fast slopes of R-wave
    2. Square          – make all values positive, amplify large slopes
    3. Moving-average  – smooth the feature signal (window ≈ 150 ms)
    4. Adaptive threshold peak picking with minimum refractory period 200 ms
    """
    # Step 1 – derivative
    diff_ecg = np.diff(ecg_filtered, prepend=ecg_filtered[0])

    # Step 2 – square
    squared = diff_ecg ** 2

    # Step 3 – moving average (150 ms window)
    win = int(0.150 * fs)
    kernel = np.ones(win) / win
    smoothed = np.convolve(squared, kernel, mode="same")

    # Step 4 – peak detection
    min_distance = int(0.200 * fs)          # 200 ms refractory period
    threshold    = 0.35 * np.max(smoothed)  # 35 % of max as threshold
    peaks, props = find_peaks(smoothed,
                              height=threshold,
                              distance=min_distance)
    return peaks, smoothed


# ═══════════════════════════════════════════════════════════════════════════════
# 4.  HEART RATE  &  HRV
# ═══════════════════════════════════════════════════════════════════════════════

def calculate_bpm(r_peaks, fs=500.0):
    """
    Compute instantaneous BPM from RR intervals.

    Returns
    -------
    mean_bpm  : float
    bpm_series: array of instantaneous BPM values per RR interval
    rr_ms     : RR intervals in milliseconds
    """
    if len(r_peaks) < 2:
        return 0.0, np.array([]), np.array([])

    rr_samples = np.diff(r_peaks)
    rr_ms      = rr_samples / fs * 1000.0    # convert to ms
    bpm_series = 60_000.0 / rr_ms            # instantaneous BPM
    mean_bpm   = float(np.mean(bpm_series))
    return mean_bpm, bpm_series, rr_ms


# ═══════════════════════════════════════════════════════════════════════════════
# 5.  FREQUENCY SPECTRUM
# ═══════════════════════════════════════════════════════════════════════════════

def compute_spectrum(sig, fs):
    freqs = np.fft.rfftfreq(len(sig), d=1.0 / fs)
    power = np.abs(np.fft.rfft(sig)) ** 2
    return freqs, power


# ═══════════════════════════════════════════════════════════════════════════════
# 6.  VISUALISATION
# ═══════════════════════════════════════════════════════════════════════════════

def plot_ecg_analysis(t, ecg_noisy, ecg_filtered, r_peaks, bpm_series,
                      rr_ms, detection_signal, fs=500.0):
    fig = plt.figure(figsize=(18, 13))
    fig.patch.set_facecolor("#0d1117")

    gs = gridspec.GridSpec(
        4, 2,
        figure=fig,
        hspace=0.55,
        wspace=0.35,
        left=0.06, right=0.97,
        top=0.91,  bottom=0.06,
    )

    ax_raw   = fig.add_subplot(gs[0, :])
    ax_filt  = fig.add_subplot(gs[1, :])
    ax_det   = fig.add_subplot(gs[2, 0])
    ax_bpm   = fig.add_subplot(gs[2, 1])
    ax_rr    = fig.add_subplot(gs[3, 0])
    ax_spec  = fig.add_subplot(gs[3, 1])

    for ax in (ax_raw, ax_filt, ax_det, ax_bpm, ax_rr, ax_spec):
        ax.set_facecolor(BG_PANEL)
        ax.grid(True)

    time_window = (0, min(10, t[-1]))

    # ── Panel A: Noisy ECG ────────────────────────────────────────────────────
    ax_raw.plot(t, ecg_noisy, color=RAW_COL, lw=0.7, alpha=0.85, label="Noisy ECG")
    ax_raw.set_xlim(time_window)
    ax_raw.set_ylabel("Amplitude (mV)", fontsize=9)
    ax_raw.set_title("A — Raw ECG  (white noise + baseline wander + 50 Hz PLI)",
                     fontsize=10, color=RAW_COL, loc="left", pad=6)
    ax_raw.legend(fontsize=8, loc="upper right")

    # ── Panel B: Filtered ECG + R-peaks ──────────────────────────────────────
    mask = (t[r_peaks] >= time_window[0]) & (t[r_peaks] <= time_window[1])
    vis_peaks = r_peaks[mask]

    ax_filt.plot(t, ecg_filtered, color=ACCENT, lw=1.1, label="Filtered ECG (0.5–40 Hz)")
    ax_filt.scatter(t[vis_peaks], ecg_filtered[vis_peaks],
                    color=PEAK_COL, s=60, zorder=5,
                    label=f"R-peaks  ({len(r_peaks)} total)", marker="^")
    ax_filt.set_xlim(time_window)
    ax_filt.set_ylabel("Amplitude (mV)", fontsize=9)
    ax_filt.set_xlabel("Time (s)", fontsize=9)
    ax_filt.set_title("B — Bandpass-Filtered ECG with Detected R-peaks",
                      fontsize=10, color=ACCENT, loc="left", pad=6)
    ax_filt.legend(fontsize=8, loc="upper right")

    # ── Panel C: Pan–Tompkins detection signal ────────────────────────────────
    det_t = np.linspace(0, t[-1], len(detection_signal))
    ax_det.plot(det_t, detection_signal, color="#a78bfa", lw=0.9,
                label="Squared + MA signal")
    ax_det.axhline(0.35 * np.max(detection_signal), color=PEAK_COL,
                   lw=1.0, ls="--", label="Adaptive threshold")
    det_mask = (t[r_peaks] >= time_window[0]) & (t[r_peaks] <= time_window[1])
    ax_det.scatter(t[r_peaks[det_mask]],
                   detection_signal[r_peaks[det_mask]],
                   color=PEAK_COL, s=40, zorder=5)
    ax_det.set_xlim(time_window)
    ax_det.set_xlabel("Time (s)", fontsize=9)
    ax_det.set_ylabel("Feature amplitude", fontsize=9)
    ax_det.set_title("C — Pan–Tompkins Feature Signal", fontsize=10,
                     color="#a78bfa", loc="left", pad=6)
    ax_det.legend(fontsize=7, loc="upper right")

    # ── Panel D: Instantaneous BPM ───────────────────────────────────────────
    beat_times = t[r_peaks[1:]]
    ax_bpm.plot(beat_times, bpm_series, color=PEAK_COL, lw=1.4,
                marker="o", markersize=4, label="Inst. BPM")
    mean_bpm = float(np.mean(bpm_series))
    ax_bpm.axhline(mean_bpm, color="#f97316", lw=1.2, ls="--",
                   label=f"Mean = {mean_bpm:.1f} BPM")
    ax_bpm.set_xlabel("Time (s)", fontsize=9)
    ax_bpm.set_ylabel("BPM", fontsize=9)
    ax_bpm.set_title("D — Instantaneous Heart Rate", fontsize=10,
                     color=PEAK_COL, loc="left", pad=6)
    ax_bpm.legend(fontsize=8, loc="upper right")

    # ── Panel E: RR interval tachogram ───────────────────────────────────────
    ax_rr.bar(range(len(rr_ms)), rr_ms, color=ACCENT, alpha=0.75, width=0.7)
    ax_rr.axhline(np.mean(rr_ms), color="#f97316", lw=1.2, ls="--",
                  label=f"Mean RR = {np.mean(rr_ms):.1f} ms")
    ax_rr.set_xlabel("Beat index", fontsize=9)
    ax_rr.set_ylabel("RR interval (ms)", fontsize=9)
    ax_rr.set_title("E — RR Interval Tachogram", fontsize=10,
                    color=ACCENT, loc="left", pad=6)
    ax_rr.legend(fontsize=8)
    sdnn = float(np.std(rr_ms))
    ax_rr.text(0.97, 0.10, f"SDNN = {sdnn:.1f} ms",
               transform=ax_rr.transAxes, ha="right", fontsize=8,
               color="#8b949e")

    # ── Panel F: Power spectrum ───────────────────────────────────────────────
    f_raw,  p_raw  = compute_spectrum(ecg_noisy,    fs)
    f_filt, p_filt = compute_spectrum(ecg_filtered, fs)
    ax_spec.semilogy(f_raw,  p_raw,  color=RAW_COL, lw=0.8, alpha=0.7,
                     label="Noisy")
    ax_spec.semilogy(f_filt, p_filt, color=ACCENT,  lw=1.1,
                     label="Filtered")
    ax_spec.axvspan(0.5, 40, alpha=0.07, color=ACCENT, label="Bandpass window")
    ax_spec.set_xlim(0, 80)
    ax_spec.set_xlabel("Frequency (Hz)", fontsize=9)
    ax_spec.set_ylabel("Power (log)", fontsize=9)
    ax_spec.set_title("F — Power Spectrum: Noisy vs Filtered", fontsize=10,
                      color="#c9d1d9", loc="left", pad=6)
    ax_spec.legend(fontsize=8)

    # ── Global title ─────────────────────────────────────────────────────────
    fig.suptitle(
        f"ECG Signal Analysis  ·  Mean HR: {mean_bpm:.1f} BPM  ·  "
        f"Detected beats: {len(r_peaks)}  ·  Duration: {t[-1]:.0f} s  ·  Fs: {fs:.0f} Hz",
        fontsize=12, color="#e6edf3", y=0.965, fontweight="bold",
    )

    plt.savefig("/mnt/user-data/outputs/ecg_analysis.png",
                dpi=150, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.show()
    print("\n  ✔  Figure saved → ecg_analysis.png")


# ═══════════════════════════════════════════════════════════════════════════════
# 7.  MAIN
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    FS         = 500.0    # Hz
    DURATION   = 10.0     # seconds
    TARGET_BPM = 72       # beats per minute
    NOISE      = 0.18     # noise amplitude (relative to R-peak ≈ 1.0 mV)

    print("=" * 60)
    print("  ECG Signal Analysis Pipeline")
    print("=" * 60)

    # ── 1. Synthesise ─────────────────────────────────────────────────────────
    print(f"\n[1] Synthesising ECG  |  target {TARGET_BPM} BPM  |  {DURATION} s  |  Fs={FS} Hz")
    t, ecg_clean, ecg_noisy = synthesise_ecg(
        duration=DURATION, fs=FS, bpm=TARGET_BPM, noise_level=NOISE
    )
    snr_raw = 10 * np.log10(np.var(ecg_clean) / np.var(ecg_noisy - ecg_clean))
    print(f"    Input SNR  : {snr_raw:.1f} dB")

    # ── 2. Filter ─────────────────────────────────────────────────────────────
    print("\n[2] Applying bandpass filter  (0.5 – 40 Hz, Butterworth order 4)")
    ecg_filtered = bandpass_filter(ecg_noisy, lowcut=0.5, highcut=40.0,
                                   fs=FS, order=4)
    snr_filt = 10 * np.log10(np.var(ecg_clean) /
                              np.var(ecg_filtered - ecg_clean))
    print(f"    Output SNR : {snr_filt:.1f} dB  (improvement: {snr_filt - snr_raw:+.1f} dB)")

    # ── 3. Detect R-peaks ─────────────────────────────────────────────────────
    print("\n[3] Detecting R-peaks  (Pan–Tompkins method)")
    r_peaks, det_signal = detect_r_peaks(ecg_filtered, fs=FS)
    print(f"    R-peaks found : {len(r_peaks)}")

    # ── 4. Heart rate ─────────────────────────────────────────────────────────
    print("\n[4] Calculating heart rate")
    mean_bpm, bpm_series, rr_ms = calculate_bpm(r_peaks, fs=FS)
    sdnn = float(np.std(rr_ms)) if len(rr_ms) > 1 else 0.0
    print(f"    Mean HR  : {mean_bpm:.1f} BPM")
    print(f"    Std  HR  : {float(np.std(bpm_series)):.2f} BPM")
    print(f"    Mean RR  : {float(np.mean(rr_ms)):.1f} ms")
    print(f"    SDNN     : {sdnn:.1f} ms  (HRV index)")
    print(f"    Min / Max RR: {float(np.min(rr_ms)):.1f} / {float(np.max(rr_ms)):.1f} ms")

    # ── 5. Plot ───────────────────────────────────────────────────────────────
    print("\n[5] Rendering visualisation …")
    plot_ecg_analysis(t, ecg_noisy, ecg_filtered, r_peaks,
                      bpm_series, rr_ms, det_signal, fs=FS)

    print("\n" + "=" * 60)
    print("  Pipeline complete.")
    print("=" * 60)


if __name__ == "__main__":
    main()
