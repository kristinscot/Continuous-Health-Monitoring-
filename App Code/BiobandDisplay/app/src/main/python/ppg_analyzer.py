import numpy as np
from scipy.signal import find_peaks, butter, filtfilt
import csv
import os

def process_ppg_data(raw_data_string, csv_path):
    """
    Processes PPG data. 
    Reads from the provided csv_path.
    """
    try:
        INPUT_HEIGHT = 1.75 # height of user in meters
        fs = 123.6  # sampling frequency

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

        with open(csv_path, newline="") as csvfile:
            reader = csv.DictReader(csvfile)
            for row in reader:
                time_ms.append(float(row["Time_ms"]))
                red_mv.append(float(row["Red_lp"]))
                ir_mv.append(float(row["IR_lp"]))

        # Convert to numpy arrays
        red_mv = np.array(red_mv)
        ir_mv = np.array(ir_mv)
        t_filtered = np.array(time_ms)

        if len(red_mv) < 10: # Not enough data to process
             return {
                "points_red": red_mv.tolist(), "points_ir": ir_mv.tolist(),
                "bpm": 0, "artStiffness": 0, "breathingRate": 0, "SpO2": 0
            }

        # Remove baseline wander
        b, a = butter(2, 0.2, btype='low', fs=fs)
        baseline = filtfilt(b, a, red_mv)


        # Find heartbeat period peaks
        peaks = []
        peaks_time = []
        filtered = red_mv - baseline
        peaks_idx, garb = find_peaks(filtered, prominence=5)
        bpms = []
        DISPLAY_BPM = 0
        i_bpm = 0

        for i in range(len(peaks_idx)):
            peaks.append(filtered[peaks_idx[i]])
            peaks_time.append(t_filtered[peaks_idx[i]])
            if i > 0:
                intervals = (peaks_time[i] - peaks_time[i-1])*10**(-3)
                inst_bpm = 60 / intervals
                bpms.append(inst_bpm)
                if i_bpm > 0:
                    if abs(bpms[i_bpm] - bpms[i_bpm-1]) > 5:
                        DISPLAY_BPM = bpms[i_bpm]
                i_bpm += 1

        # Find systolic and diastolic peaks for arterial stiffness
        peaks_AS = []
        peaks_AS_time = []
        as_list = []
       
        time_diff = 0
        filtered_AS = filtered*-1
        peaks_idx_AS, garbAS = find_peaks(filtered_AS, prominence=0.01)
        for i in range(len(peaks_idx_AS)):
            if filtered_AS[peaks_idx_AS[i]] > 0:
                peaks_AS.append(filtered_AS[peaks_idx_AS[i]])
                peaks_AS_time.append(t_filtered[peaks_idx_AS[i]])
                
        for i in range(len(peaks_AS)):
            if i > 0:
                time_diff = peaks_AS_time[i] - peaks_AS_time[i-1]
                if time_diff < 450:
                    arterialStiffness = INPUT_HEIGHT / (time_diff*0.001)
                    as_list.append(arterialStiffness)
                    print("V:", round(peaks_AS[i],2), "\tt1:", round(peaks_AS_time[i-1],2), "\tt2:", round(peaks_AS_time[i],2), "\ttd:", round(time_diff,2), "\tAS:", round(arterialStiffness,2))   
                    DISPLAY_AS = np.mean(as_list)

        # Find breathing rate
        breathing_rate = []
        d, c = butter(2, 0.2, btype='low', fs=fs)
        breathing_mod = filtfilt(d, c, ir_mv)
        breathing_peaks_idx, garb_breathing = find_peaks(breathing_mod, prominence=0.001)

        for i in range(len(breathing_peaks_idx)):
            if i > 0 and i < len(breathing_peaks_idx):
                DISPLAY_BR =    60 / ((time_ms[breathing_peaks_idx[i]] - time_ms[breathing_peaks_idx[i-1]])*0.001)
                breathing_rate.append(DISPLAY_BR)

        # Calculate SpO2
        if len(red_mv) > 0 and len(ir_mv) > 0:

            red = np.array(red_mv)
            ir = np.array(ir_mv)

            peaks_red, peaks_red_time = peaks_function(red_mv, time_ms, 1)
            troughs_red, troughs_red_time = peaks_function(red_mv, time_ms, -1)
            peaks_ir, peaks_ir_time = peaks_function(ir_mv, time_ms, 1)
            troughs_ir, troughs_ir_time = peaks_function(ir_mv, time_ms, -1)

            red_ac_list = []
            red_ac = 0
            ir_ac_list = []
            ir_ac = 0
            most_recent_ir_ac = 0
            most_recent_red_ac = 0

            for i in range(min(len(peaks_red), len(troughs_red))):
                print('Peaks:', peaks_red[i], peaks_red_time[i], '\tTroughs:', troughs_red[i], troughs_red_time[i])
                red_ac = peaks_red[i] - troughs_red[i]
                red_ac_list.append(round(red_ac,6))
                print('red_ac',red_ac)

            for i in range(min(len(peaks_ir), len(troughs_ir))):
                print('Peaks:', peaks_ir[i], peaks_ir_time[i], '\tTroughs:', troughs_ir[i], troughs_ir_time[i])
                ir_ac = peaks_ir[i] - troughs_ir[i]
                ir_ac_list.append(round(ir_ac,6))
                print('ir_ac',ir_ac)

            red_dc = np.mean(red)
            ir_dc = np.mean(ir)

            if red_dc > 0 and ir_dc > 0:
                R = (red_ac/red_dc) / (ir_ac/ir_dc)
                spo2 = 110 - 25 * R
                spo2 = np.clip(spo2, 70, 100)
                print(f"Estimated SpO2: {spo2:.2f}%")
                DISPLAY_SPO2 = spo2

        
        return {
            "points_red": red_mv.tolist(),
            "points_ir": ir_mv.tolist(),
            "bpm": float(DISPLAY_BPM),
            "artStiffness": float(DISPLAY_AS),
            "breathingRate": float(DISPLAY_BR),
            "SpO2": float(DISPLAY_SPO2)
        }

    except Exception as e:
        print(f"Error processing PPG data: {e}")
        return {
            "points_red": [], "points_ir": [],
            "bpm": 0, "artStiffness": 0, "breathingRate": 0, "SpO2": 0
        }
