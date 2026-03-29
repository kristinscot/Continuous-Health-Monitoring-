import csv
import os
import numpy as np
from emg_analyzer import analyze_EMG, analyze_EMG_array

def process_ble_data(raw_data_string, csv_path=None):
    try:
        if csv_path and os.path.exists(csv_path):
            with open(csv_path, newline='') as csvfile:
                return analyze_EMG(csvfile)

        if raw_data_string == "INITIAL_LOAD":
            return {"vrms": 0.0, "tdmf": 0.0, "is_resting": True, "mov_avg": [], "rectified": []}

        voltage_data = np.array([abs(float(v)) for v in raw_data_string.split(',') if v.strip()])
        if len(voltage_data) == 0:
            return {"vrms": 0.0, "tdmf": 0.0, "is_resting": True, "mov_avg": [], "rectified": []}

        return analyze_EMG_array(voltage_data)

    except Exception as e:
        return {"error": str(e), "vrms": 0.0, "tdmf": 0.0, "is_resting": True, "mov_avg": [], "rectified": []}
    
if __name__ == "__main__":
    print(process_ble_data(raw_data_string=None, csv_path=r'C:\Users\caitl\OneDrive - Carleton University\Capstone\teraterm_log_jan14.csv'))
