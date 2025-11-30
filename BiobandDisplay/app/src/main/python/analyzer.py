    # In app/src/main/python/analyzer.py
    import numpy as np
    from scipy.signal import

    def process_ble_data(raw_data_string):
        """
        Analyzes a string of comma-separated numbers.
        - Parses the string into a numpy array.
        - Applies a Savitzky-Golay filter to smooth the data.
        - Returns the processed data as a list of floats.
        """
        try:
            # Split the string by commas and convert to float
            numeric_values = [float(val) for val in raw_data_string.split(',') if val]
            data_array = np.array(numeric_values)

            # Apply a smoothing filter (if there's enough data)
            # The window size must be odd and smaller than the data size.
            if len(data_array) > 5:
                # Applies a smoothing filter. You can adjust window_length and polyorder.
                smoothed_data = savgol_filter(data_array, window_length=5, polyorder=2)
                return smoothed_data.tolist()  # Return as a standard Python list
            else:
                return data_array.tolist() # Not enough data to filter, return as is

        except (ValueError, IndexError) as e:
            print(f"Error processing data: {e}")
            return [] # Return an empty list on error
