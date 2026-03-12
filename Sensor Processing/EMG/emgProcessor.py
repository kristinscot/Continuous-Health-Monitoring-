import numpy as np
import csv
import matplotlib.pyplot as mpl
from scipy import signal

# FILL IN CORRECT CSV FILE
dataPath = r"C:\Users\caitl\OneDrive - Carleton University\Capstone\teraterm_log_jan14.csv"

FS = 13333
NOTCH_FREQ = 60
NOTCH_Q = 50

MOVING_AVG_WIN = int(0.150 * FS)   # approx 150 ms window
END_HOLD_SAMPLES = 800             # 60 ms
MICRO_NOISE_FLOOR = 3              # noise clipping

MIN_LOG_LENGTH = 5000
THRESHOLD = 25

FREQ_LOW = 20
FREQ_HIGH = 450

STFT_WIN = 2048         # approx 150 ms
STFT_OVERLAP = 1536     # 75% overlap

# voltage = []
# VrmsLog = []
# TDMFLog = []
# startTimeLog = []
# endTimeLog = []

# Import designated csv for voltage values
def importCSV(path):
    voltage = []
    with open(path, newline='') as csvfile:
        data = list(csv.reader(csvfile))
    for line in data:
        val = abs(float(line[0]))
        if val < 3000:
            voltage.append(val)
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

    for i in range(len(voltage)): # Iterate through the entire voltage set
        # If voltage exceeds the threshold and not currently logging, start logging
        if not logging and mov[i] > threshold:
            logging = True
            burst = []
            start_idx = i
            below_count = 0
        # If already logging
        elif logging:
            # Append the current voltage value to the current burst
            burst.append(voltage[i])
            # If the moving avg drops below the threshold, increment the count of points below the threshold
            if mov[i] < threshold:
                below_count += 1
            else:
                below_count = 0
            # Stop logging if the number of moving avg samples below the threshold exceeds a certain amount
            if below_count >= END_HOLD_SAMPLES:
                logging = False
                end_idx = i - END_HOLD_SAMPLES
                # save the burst if the number of samples in the burst is large enough to be a burst and not just noise
                if len(burst) >= MIN_LOG_LENGTH:
                    VrmsLog.append(rms(np.array(burst)))
                    startTimeLog.append(start_idx)
                    endTimeLog.append(end_idx)

                burst = []
                below_count = 0

    return VrmsLog, startTimeLog, endTimeLog, logging

def meanFrequencyFFT(voltage, fs, f_low, f_high):
    N = len(voltage)
    X = np.fft.rfft(voltage * np.hanning(N)) # fourier transform
    Pxx = np.abs(X)**2  # power spectrum
    f = np.fft.rfftfreq(N, 1/fs)

    band = (f >= f_low) & (f <= f_high)
    return np.sum(f[band] * Pxx[band]) / np.sum(Pxx[band])

def stftMeanFrequency(voltage, fs, f_low, f_high, nperseg, noverlap):
    f, t, Zxx = signal.stft(voltage, fs=fs, window='hann', nperseg=nperseg, noverlap=noverlap, padded=False, boundary=None)

    P = np.abs(Zxx)**2
    band = (f >= f_low) & (f <= f_high)

    f_band = f[band]
    P_band = P[band, :]

    mnf = np.sum(f_band[:, None] * P_band, axis=0) / np.sum(P_band, axis=0)
    return t, mnf

def processEMG(dataPath):
    raw_voltage = importCSV(dataPath)

    # apply 60 Hz notch filter
    filtered_voltage = notchFilter(raw_voltage, FS, NOTCH_FREQ, NOTCH_Q)

    # rectify using the mean of the filtered voltage - workaround for not having a consistent reference
    rectified_voltage = np.abs(filtered_voltage - np.mean(filtered_voltage))

    # set very small voltage values to 0 to clean noise
    rectified_voltage[rectified_voltage < MICRO_NOISE_FLOOR] = 0

    mov_avg = movingAverage(rectified_voltage, MOVING_AVG_WIN)

    VrmsLog, startTimeLog, endTimeLog, logging = detectBursts(rectified_voltage, mov_avg, THRESHOLD)

    t_stft, mnf_stft = stftMeanFrequency(filtered_voltage, FS, FREQ_LOW, FREQ_HIGH, STFT_WIN, STFT_OVERLAP)

    return VrmsLog, t_stft, mnf_stft, logging

def plot_results():

    raw_voltage = importCSV(dataPath)
    filtered_voltage = notchFilter(raw_voltage, FS, NOTCH_FREQ, NOTCH_Q)
    rectified_voltage = np.abs(filtered_voltage - np.mean(filtered_voltage))
    rectified_voltage[rectified_voltage < MICRO_NOISE_FLOOR] = 0
    mov_avg = movingAverage(rectified_voltage, MOVING_AVG_WIN)
    VrmsLog, startTimeLog, endTimeLog, logging = detectBursts(rectified_voltage, mov_avg, THRESHOLD)
    t_stft, mnf_stft = stftMeanFrequency(filtered_voltage, FS, FREQ_LOW, FREQ_HIGH, STFT_WIN, STFT_OVERLAP)

    for i in range(len(VrmsLog)):
        print(
            f"Burst {i+1}: "
            f"VRMS = {VrmsLog[i]:.4f} mV | "
            f"MNF = {meanFrequencyFFT(filtered_voltage[startTimeLog[i]:endTimeLog[i]], FS, FREQ_LOW, FREQ_HIGH):.1f} Hz |"
            f"Start time: {startTimeLog[i]*(1/FS):.2f} |"
            f"End time: {endTimeLog[i]*(1/FS):.2f} | "
        )

    time = np.arange(len(raw_voltage)) / FS
    fig, ax = mpl.subplots()
    # Plot of RMS voltage
    ax.plot(time, rectified_voltage, linewidth=0.3, alpha=0.6, label="Rectified EMG")
    ax.plot(time, mov_avg, linewidth=1.0, label="Moving Average")
    ax.hlines(THRESHOLD, time[0], time[-1], colors='r', linestyles='--', label="Threshold")

    # grey background over muscle burst
    for i, (s, e) in enumerate(zip(startTimeLog, endTimeLog)):
        ax.axvspan(s/FS, e/FS, color='gray', alpha=0.15)
        ax.hlines(
            VrmsLog[i],
            s/FS,
            e/FS,
            linewidth=3,
            label="RMS Voltage" if i == 0 else None
        )

    ax.set_ylim(bottom=0)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Voltage (mV)")
    ax.set_title("RMS Voltage of Muscle Bursts")
    ax.legend()
    mpl.show()

    # Plot for frequency domain
    t_stft, mnf_stft = stftMeanFrequency(filtered_voltage, FS, FREQ_LOW, FREQ_HIGH, STFT_WIN, STFT_OVERLAP)

    fig, ax = mpl.subplots()
    ax.plot(t_stft, mnf_stft, linewidth=1.2, label="STFT Mean Frequency")

    # grey background over muscle burst
    for i, (s, e) in enumerate(zip(startTimeLog, endTimeLog)):
        ax.axvspan(s/FS, e/FS, color='gray', alpha=0.12)
        burst_sig = filtered_voltage[s:e]
        mnf_burst = meanFrequencyFFT(burst_sig, FS, FREQ_LOW, FREQ_HIGH)
        ax.hlines(
            mnf_burst,
            s/FS,
            e/FS,
            linewidth=2,
            color='r',
            label="Burst Mean Frequency" if i == 0 else None
        )

    ax.set_ylim(bottom=0)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Mean Frequency (Hz)")
    ax.set_title("Time Dependent Mean Frequency (TDMF)")
    ax.legend()
    mpl.show()

if __name__ == "__main__":
    plot_results()
