import numpy as np
import csv
from scipy import signal

FS = 13333
NOTCH_FREQ = 60
NOTCH_Q = 50

MOVING_AVG_WIN = int(0.150 * FS)
END_HOLD_SAMPLES = 800
MICRO_NOISE_FLOOR = 3

MIN_LOG_LENGTH = 5000
THRESHOLD = 25

FREQ_LOW = 20
FREQ_HIGH = 450

STFT_WIN = 2048
STFT_OVERLAP = 1536

def importCSV(csvfile):
    voltage = []
    next(csvfile, None)
    reader = csv.reader(csvfile)
    for line in reader:
        try:
            val = abs(float(line[0]))
            if val < 3000:
                voltage.append(val)
        except:
            continue
    return np.array(voltage)

def notchFilter(voltage, sample_freq, notch_freq, notch_q):
    b, a = signal.iirnotch(notch_freq, notch_q, sample_freq)
    return signal.filtfilt(b, a, voltage)

def movingAverage(voltage, window):
    return np.convolve(voltage, np.ones(window)/window, mode='same')

def rms(voltage):
    return np.sqrt(np.mean(voltage**2))

def detectBursts(voltage, mov, threshold):
    VrmsLog = []
    startTimeLog = []
    endTimeLog = []

    logging = False
    burst = []
    below_count = 0

    for i in range(len(voltage)):
        if not logging and mov[i] > threshold:
            logging = True
            burst = []
            start_idx = i
            below_count = 0
        elif logging:
            burst.append(voltage[i])
            if mov[i] < threshold:
                below_count += 1
            else:
                below_count = 0
            if below_count >= END_HOLD_SAMPLES:
                logging = False
                end_idx = i - END_HOLD_SAMPLES
                if len(burst) >= MIN_LOG_LENGTH:
                    VrmsLog.append(rms(np.array(burst)))
                    startTimeLog.append(start_idx)
                    endTimeLog.append(end_idx)
                burst = []
                below_count = 0

    return VrmsLog, startTimeLog, endTimeLog, logging

def meanFrequencyFFT(voltage, fs, f_low, f_high):
    N = len(voltage)
    if N < 2: return 0.0
    X = np.fft.rfft(voltage * np.hanning(N))
    Pxx = np.abs(X)**2
    f = np.fft.rfftfreq(N, 1/fs)
    band = (f >= f_low) & (f <= f_high)
    if np.sum(Pxx[band]) == 0: return 0.0
    return np.sum(f[band] * Pxx[band]) / np.sum(Pxx[band])

def stftMeanFrequency(voltage, fs, f_low, f_high, nperseg, noverlap):
    f, t, Zxx = signal.stft(voltage, fs=fs, window='hann', nperseg=nperseg, noverlap=noverlap, padded=False, boundary=None)
    P = np.abs(Zxx)**2
    band = (f >= f_low) & (f <= f_high)
    f_band = f[band]
    P_band = P[band, :]
    mnf = np.sum(f_band[:, None] * P_band, axis=0) / np.sum(P_band, axis=0)
    return t, mnf

def analyze_EMG_array(raw_voltage):
    if len(raw_voltage) == 0:
        return {"vrms": 0.0, "tdmf": 0.0, "is_resting": True, "mov_avg": [], "rectified": [], "mnf_stft": [], "t_stft": []}

    filtered_voltage = notchFilter(raw_voltage, FS, NOTCH_FREQ, NOTCH_Q)
    rectified_voltage = np.abs(filtered_voltage - np.mean(filtered_voltage))
    rectified_voltage[rectified_voltage < MICRO_NOISE_FLOOR] = 0
    mov_avg = movingAverage(rectified_voltage, MOVING_AVG_WIN)

    VrmsLog, startTimeLog, endTimeLog, logging = detectBursts(rectified_voltage, mov_avg, THRESHOLD)

    t_stft, mnf_stft = stftMeanFrequency(filtered_voltage, FS, FREQ_LOW, FREQ_HIGH, STFT_WIN, STFT_OVERLAP)

    active_burst = len(VrmsLog) > 0
    if active_burst:
        last_vrms = VrmsLog[-1]
        last_mnf = meanFrequencyFFT(filtered_voltage[startTimeLog[-1]:endTimeLog[-1]], FS, FREQ_LOW, FREQ_HIGH)
    else:
        last_vrms = rms(filtered_voltage)
        last_mnf = meanFrequencyFFT(filtered_voltage, FS, FREQ_LOW, FREQ_HIGH)

    step = max(1, len(mov_avg) // 2000)
    return {
        "vrms": float(last_vrms),
        "tdmf": float(last_mnf),
        "is_resting": not (active_burst or logging),
        "mov_avg": [float(v) for v in mov_avg[::step]],
        "rectified": [float(v) for v in rectified_voltage[::step]]
        #"t_stft": [float(v) for v in t_stft],
        #"mnf_stft": [float(v) for v in mnf_stft],
    }

def analyze_EMG(csvfile):
    return analyze_EMG_array(importCSV(csvfile))
