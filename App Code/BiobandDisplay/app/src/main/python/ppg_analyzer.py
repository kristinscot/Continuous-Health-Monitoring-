import numpy as np

def process_ppg_data(raw_data_string):
    """
    Analyzes PPG data to extract heart rate and blood flow.
    """
    try:
        numeric_values = [float(val) for val in raw_data_string.split(',') if val]
        if not numeric_values:
            return {"points": [], "heart_rate": 0, "blood_flow": 0}
            
        data_array = np.array(numeric_values)

        # Placeholder logic for Heart Rate and Blood Flow
        # In a real scenario, you'd use peak detection on the PPG signal
        heart_rate = 72 # Dummy value
        blood_flow = 95 # Dummy value %

        return {
            "points": data_array.tolist(),
            "heart_rate": int(heart_rate),
            "blood_flow": int(blood_flow)
        }

    except Exception as e:
        print(f"Error processing PPG data: {e}")
        return {"points": [], "heart_rate": 0, "blood_flow": 0}
