import numpy as np
from scipy import signal

# Expert parameters from your updated script
FS = 13333
NOTCH_FREQ = 60
NOTCH_Q = 50
MOVING_AVG_WIN = int(0.150 * FS)   # approx 150 ms window
MICRO_NOISE_FLOOR = 3              # noise clipping
THRESHOLD = 25
FREQ_LOW = 20
FREQ_HIGH = 450

def rms(voltage):
    """Calculates the root mean square of the voltage signal."""
    if len(voltage) == 0: return 0.0
    return np.sqrt(np.mean(voltage**2))

def meanFrequencyFFT(voltage, fs, f_low, f_high):
    """Calculates the mean frequency using RFFT and a Hanning window."""
    N = len(voltage)
    if N < 2: return 0.0
    # Apply Hanning window to reduce spectral leakage
    X = np.fft.rfft(voltage * np.hanning(N))
    Pxx = np.abs(X)**2  # power spectrum
    f = np.fft.rfftfreq(N, 1/fs)

    band = (f >= f_low) & (f <= f_high)
    if np.sum(Pxx[band]) == 0: return 0.0
    return np.sum(f[band] * Pxx[band]) / np.sum(Pxx[band])

def movingAverage(voltage, window):
    """Calculates moving average using convolution (more efficient)."""
    if len(voltage) == 0: return np.array([])
    effective_window = min(window, len(voltage))
    if effective_window < 1: effective_window = 1
    return np.convolve(voltage, np.ones(effective_window)/effective_window, mode='same')

def process_ble_data(raw_data_string):
    """
    Processes real-time EMG data using updated expert logic parameters.
    Returns metrics for display in the Android app.
    """
    try:
        # 1. Parse incoming data string
        values = [float(v) for v in raw_data_string.split(',') if v.strip()]
        if not values:
            return {
                "vrms": 0.0, 
                "tdmf": 0.0,
                "is_resting": True, 
                "voltages": [], 
                "ends_in_activation": False
            }
        
        voltage_data = np.array(values)
        
        # 2. Notch Filter (60Hz)
        b, a = signal.iirnotch(NOTCH_FREQ, NOTCH_Q, FS)
        filtered_voltage = signal.filtfilt(b, a, voltage_data)

        # 3. Rectification: abs(voltage - mean)
        rectified_voltage = np.abs(filtered_voltage - np.mean(filtered_voltage))
        
        # 4. Noise clipping
        rectified_voltage[rectified_voltage < MICRO_NOISE_FLOOR] = 0
        
        # 5. Moving Average Envelope
        mov = movingAverage(rectified_voltage, MOVING_AVG_WIN)
        
        # 6. Calculate Metrics
        # Frequency analysis is typically done on the filtered (unrectified) signal
        current_vrms = rms(filtered_voltage)
        current_tdmf = meanFrequencyFFT(filtered_voltage, FS, FREQ_LOW, FREQ_HIGH)
        
        # 7. Status Determination
        is_resting = bool(np.max(mov) < THRESHOLD)
        
        # Carry-over flag if chunk ends while muscle is still active
        ends_in_activation = bool(mov[-1] > THRESHOLD)

        return {
            "vrms": float(current_vrms),
            "tdmf": float(current_tdmf),
            "is_resting": is_resting,
            "voltages": [float(v) for v in mov], # Envelope values for the graph
            "ends_in_activation": ends_in_activation
        }

    except Exception as e:
        return {
            "error": str(e),
            "vrms": 0.0,
            "tdmf": 0.0,
            "is_resting": True,
            "voltages": [],
            "ends_in_activation": False
        }
