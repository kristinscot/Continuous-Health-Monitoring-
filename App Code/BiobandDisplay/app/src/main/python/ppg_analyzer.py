import numpy as np
from scipy.signal import find_peaks, butter, filtfilt
import csv
import os

def peaks_function(signal, time, sign):
    peaks = []
    peaks_time = []
    peaks_index, garbage = find_peaks(sign*signal, prominence=0.2)
    for i in range(len(peaks_index)):
        peaks.append(signal[peaks_index[i]])
        peaks_time.append(time[peaks_index[i]])

    return peaks, peaks_time
    
def process_ppg_data(raw_data_string, csv_path):
    """
    Processes PPG data. 
    Reads from the provided csv_path.
    """
    try:
        INPUT_HEIGHT = 1.75 # height of user in meters
        fs = 123.6  # sampling frequency (Hz)

        # Lists for time and all LED data
        time_ms = []
        red_mv = []
        ir_mv = []

        # If the file doesn't exist, we return defaults
        if not csv_path or not os.path.exists(csv_path):
            print(f"File not found: {csv_path}")
            return {
                "points_red": [], "points_ir": [],
                "bpm": 0, "artStiffness": 0, "breathingRate": 0, "SpO2": 0
            }

        # Assume data is added to a csv, taking 300 samples at a time (~ 9 seconds)
        with open(csv_path, newline="") as csvfile:
            reader = csv.DictReader(csvfile)
            row_count = len(list(reader))
            overshoot = row_count - 300
            count = 0
            for row in reader:
                if (row_count < 300) or (row_count >= 300 and count >= overshoot):
                    time_ms.append(float(row["Time_ms"]))
                    red_mv.append(float(row["Red_lp"]))
                    ir_mv.append(float(row["IR_lp"]))
                count += 1

        # Convert to numpy arrays
        red_mv = np.array(red_mv)
        ir_mv = np.array(ir_mv)
        t_filtered = np.array(time_ms)

        if len(red_mv) < 10: # Not enough data to process
             return {
                "points_red": red_mv.tolist(), "points_ir": ir_mv.tolist(),
                "bpm": 0,"SpO2": 0
            }
            
        num_red, den_red = butter(2, 5, btype='low', fs=123.6)
        num_ir, den_ir = butter(2, 5, btype='low', fs=123.6)
        red_mv = filtfilt(num_red, den_red, red_mv)
        ir_mv = filtfilt(num_ir, den_ir, ir_mv)
        
        t_filtered = np.array(time_ms)
        
        # Remove baseline wander
        b, a = butter(2, 2, btype='low', fs=fs)
        baseline_red = filtfilt(b, a, red_mv)
        baseline_ir = filtfilt(b, a, ir_mv)
        
        # Find heartbeat period peaks
        peaks = []
        peaks_time = []
        filtered = ir_mv - baseline_ir
        peaks_idx, garb = find_peaks(filtered, prominence=1)
        bpms = []
        DISPLAY_BPM = 0
        i_bpm = 0
        
        for i in range(len(peaks_idx)):
            peaks.append(filtered[peaks_idx[i]])
            peaks_time.append(t_filtered[peaks_idx[i]])
            if i > 0:
                intervals = (peaks_time[i] - peaks_time[i-1])*10**(-3)
                inst_bpm = 60 / intervals
                inst_bpm = np.clip(inst_bpm, 40, 180)
                bpms.append(round(inst_bpm, 2))
                if len(bpms) > 1:
                    if abs(bpms[i_bpm] - bpms[i_bpm-1]) > 5:
                        DISPLAY_BPM = bpms[i_bpm]
                
        
        # Calculate SpO2
        if len(red_mv) > 0 and len(ir_mv) > 0:
            red_ac = red_mv - baseline_red
            ir_ac = ir_mv - baseline_ir
        
            peaks_red, peaks_red_time = peaks_function(red_ac, time_ms, 1)
            troughs_red, troughs_red_time = peaks_function(red_ac, time_ms, -1)
            peaks_ir, peaks_ir_time = peaks_function(ir_ac, time_ms, 1)
            troughs_ir, troughs_ir_time = peaks_function(ir_ac, time_ms, -1)
        
            red_ac_list = []
            red_ac = 0
            ir_ac_list = []
            ir_ac = 0
            most_recent_ir_ac = 0
            most_recent_red_ac = 0
            spo2_list = []
        
            for i in range(min(len(peaks_red), len(troughs_red))):
                red_ac = peaks_red[i] - troughs_red[i]
                red_ac_list.append(round(red_ac,6))
        
            for i in range(min(len(peaks_ir), len(troughs_ir))):
                ir_ac = peaks_ir[i] - troughs_ir[i]
                ir_ac_list.append(round(ir_ac,6))
        
            red_dc = np.mean(baseline_red[-10:-1])
            ir_dc = np.mean(baseline_ir[-10:-1])
        
            if red_dc > 0 and ir_dc > 0:
                R = (red_ac/red_dc) / (ir_ac/ir_dc)
                spo2 = 110 - 25 * R
                spo2 = np.clip(spo2, 70, 100)
                spo2_list.append(spo2)
                if len(spo2_list) > 1:
                    if abs(spo2_list[-1] - spo2_list[-2]) > 2:
                        DISPLAY_SPO2 = spo2_list[-1]
        
        return {
            "points_red": red_mv.tolist(),
            "points_ir": ir_mv.tolist(),
            "bpm": float(DISPLAY_BPM),
            "SpO2": float(DISPLAY_SPO2)
        }

    except Exception as e:
        print(f"Error processing PPG data: {e}")
        return {
            "points_red": [], "points_ir": [],
            "bpm": 0,"SpO2": 0
        }
