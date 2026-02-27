import numpy as np
from scipy.signal import find_peaks, savgol_filter

def process_ble_data(raw_data_string):
    """
    Processes EMG data to extract muscle pulses and status.
    """
    try:
        # Parse data
        values = [float(v) for v in raw_data_string.split(',') if v.strip()]
        if not values:
            return {
                "frequencies": [], 
                "voltages": [], 
                "vrms": 0.0,
                "is_resting": True,
                "ends_in_activation": False
            }
            
        data = np.array(values)
        
        # Calculate V(rms)
        vrms = np.sqrt(np.mean(data**2))
        
        # 1. Preprocessing (Rectification & Smoothing)
        rectified = np.abs(data)
        window_len = min(len(rectified), 15)
        if window_len % 2 == 0: window_len -= 1
        
        if window_len >= 3:
            envelope = savgol_filter(rectified, window_len, 2)
        else:
            envelope = rectified

        # 2. Pulse Detection
        threshold = np.mean(envelope) + 1.0 * np.std(envelope)
        peaks, _ = find_peaks(envelope, height=threshold, distance=10)
        voltages = [float(v) for v in envelope[peaks]]
        
        # Muscle Status logic (simple thresholding)
        is_resting = bool(np.max(envelope) < (np.mean(envelope) + 2 * np.std(envelope)))

        # Frequency calculation (placeholder)
        frequencies = []
        sampling_rate = 100 
        for peak in peaks:
            start = max(0, peak - 10)
            end = min(len(data), peak + 10)
            pulse_segment = data[start:end]
            if len(pulse_segment) > 1:
                zero_crossings = np.where(np.diff(np.sign(pulse_segment)))[0]
                freq = len(zero_crossings) * (sampling_rate / (2 * len(pulse_segment)))
                frequencies.append(float(freq))
            else:
                frequencies.append(0.0)

        # 3. Ends in activation?
        ends_in_activation = bool(envelope[-1] > threshold)

        return {
            "frequencies": frequencies,
            "voltages": voltages,
            "vrms": float(vrms),
            "is_resting": is_resting,
            "ends_in_activation": ends_in_activation
        }

    except Exception as e:
        return {
            "frequencies": [],
            "voltages": [],
            "vrms": 0.0,
            "is_resting": True,
            "ends_in_activation": False,
            "error": str(e)
        }
