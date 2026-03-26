# Author: Ciaran McDonald-Jensen
# Date Created: Mar 26, 2026
# Purpose: This is to make some plots to visualize the raw data outputted from PPG_DC_IDAC March version outputs
# (Ciaran and Andrew version of PPG_DC_IDAC that outputs time, AC Values*4, DC Values*4, IMU Values)



# Order of values is green, red, IR, all off


import numpy as np
from matplotlib import pyplot as plt
import csv



print("Program Started")


# YOU NEED TO REPLACE THIS TO BE THE FILE YOU WANT (Note: Using absolute file path right now)
data_folder_path = "/home/ciaran-mcdj/Documents/School/ELEC4908_Capstone/Code/Continuous-Health-Monitoring-/PPG_code/four_led_sampling_data/Data/PPG_and_IMU_DATA"
data_filename = "attempt_2.txt"


# PARSE DATA FROM FILE
with open(data_folder_path+data_filename, "r") as csvFile:
    # print(file.read())
    i = 0
    csvReader = csv.reader(csvFile, delimiter='\t')

    freqList = []
    amplitudeList = []
    
    # Extract data
    for row in csvReader:
        freqList.append(float(row[0]))
        amplitudeList.append(float(row[1]))
        
        
        

print("Program Ended, the formatted file should now be in the same folder as the input folder with the same name with _formatted at the end")




print("Program Ended")