# Author: Ciaran McDonald-Jensen
# Date Created: January 22, 2026
# Purpose: This program converts the data from the serial terminal outputted from four_led_sampling to csv format
# NOTE: You need to manually remove the fisrt X lines of errors that are not data

print("Program Started")

# YOU NEED TO REPLACE THIS TO BE THE FILE YOU WANT (Note: Using absolute file path right now)
data_folder_path = "/home/ciaran-mcdj/Documents/School/ELEC4908_Capstone/Code/Continuous-Health-Monitoring-/PPG_code/four_led_sampling_data/Data/"
data_filename = "serial-terminal-22012026_182729.txt"
# NOTE: Output file name is same place as input with same name with '_formatted' at end


with open(data_folder_path+data_filename, "r") as infile, open(data_folder_path+data_filename[:-4]+"_formatted.txt", 'w') as outfile:
    # print(file.read())
    i = 0
    for row in infile:
        try:
            int(row[0])
        except ValueError:
            print("WARNING: I think you didn't remove the random stuff at the beginning of the file. You need to do this manually. There is a line that is:\n", row)

        if i == 2:
            outfile.write(row) #Allows new line to print at end
        else:
            outfile.write(row[:-1]) #Removes new line at end of line

        i = (i+1) % 3

print("Program Ended, the formatted file should now be in the same folder as the input folder with the same name with _formatted at the end")