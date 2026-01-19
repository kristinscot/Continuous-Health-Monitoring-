import matplotlib.pyplot as mpl
import numpy as np
import csv as csv
from scipy import signal #used for signal filtering
from scipy.fft import fft

MIN_LOG_LENGTH = 5000

voltage = []
#time = [] #legacy from early datasets where time was present
mov = [] #holds array of the moving average
VrmsLog = []
TDMFLog = []
startTimeLog = []
endTimeLog = []

def importCSV(path): #import designated csv for voltage and time values, time may be removed
    voltage = []
    #time = []
    with open(path, newline='') as csvfile:
        data = list(csv.reader(csvfile))
    for line in data:
        #time.append(float(line[0])) #because time is not needed
        val = abs(float(line[0]))
        if(val < 3000):
            voltage.append(val)
    return voltage#, time

def movingAverages(voltage):
    mov = []
    r = len(voltage)
    w = 700 #width of the moving average
    for i  in range(r):
        if((i+w)>r):
            w = r-i
        mov.append(np.average(voltage[i:(i+w)]))
    return mov

def rms(voltage):
    return np.sqrt(sum(np.square(voltage)/len(voltage)))

def tdmf(voltage, fs):
    #length of input burst
    N = len(voltage)
    #fast fourier transform
    X = np.fft.fft(voltage)
    # power spectrum
    P = np.abs(X)**2 / N   
    # frequency vector
    f = np.arange(N) * (fs / N)   
    # Use only positive frequencies
    half = np.arange(1, N // 2)
    igif = P[half]
    # tdmf = sum(f(half).*P(half)) / sum(P(half))
    return np.sum(f[half] * P[half]) / np.sum(P[half])

def quantMusclePulse(voltage, fs):
    startTimeLog = []
    endTimeLog = []
    VrmsLog = []
    TDMFLog = []
    logging = False
    for i in range(len(voltage)): #iterate through the entire voltage set
        if(logging == False):
            if(mov[i] > threshold): #when voltage exceed the threshold begin logging the data
                logging = True
                log = []
                startTimeTemp = i
        elif(logging == True):
            log.append(voltage[i])
            if(mov[i] < threshold): #when voltage drops below threshold cease logging and calculate the RMS and TDMF of the burst which just passed
                logging = False
                #log(6)
                if(len(log) >= MIN_LOG_LENGTH):
                    endTimeLog.append(i)
                    startTimeLog.append(startTimeTemp)
                    #calculate the VRMS
                    VrmsLog.append(rms(log))
                    #calculate the TDMF
                    TDMFLog.append(tdmf(log, fs))
    return VrmsLog, TDMFLog, startTimeLog, endTimeLog, logging


def processEMG(dataPath):
    #will obviously need to be changed to run on individual systems
    voltage = importCSV(dataPath)




    #60Hz filtering - may not be necessary when driving from nrf+batt
    #doesn't seem to be working, not too sure why but ultimatley kinda unnecessary 
    fs = 13333#1/(np.average(np.diff(time))) #will likely be hard coded in final implementation
    b, a = signal.iirnotch(60, 50, fs) #generate coeffecients of 60Hz filter
    voltageFilt = signal.filtfilt(b, a, voltage) #filter 60Hz out of voltage signal

    #rectify the voltage measurements
    av = np.average(voltageFilt) #get average voltage for rectification, could possibly be removed later and hard coded but due to the reference not being set and consistent this is the work around
    rectV = abs(voltageFilt-av)

    #legacy threshold setting
    #t = int(np.ceil(1/(time[2]-time[1])))
    #av = np.average(voltageFilt[0:t])
    #stddev = np.std(voltageFilt[0:t])

    threshold = 25 #temp value - when final configuration is determined threshold will be hard coded to be slightly above the standard noise level
    midPulse = False

    #take moving average of the voltage over the entire dataset
    mov = movingAverages(rectV) 

    #identify and quantify muscle pulses in the dataset
    VrmsLog, TDMFLog, startTimeLog, endTimeLog, midPulse = quantMusclePulse(rectV, fs)

    return VrmsLog, TDMFLog, midPulse

#will obviously need to be changed to run on individual systems
voltage = importCSV(r"C:\Continuous-Health-Monitoring-\Sensor Processing\EMG\teraterm_log_jan14.csv")

#60Hz filtering - may not be necessary when driving from nrf+batt
#doesn't seem to be working, not too sure why but ultimatley kinda unnecessary 
fs = 13333#1/(np.average(np.diff(time))) #will likely be hard coded in final implementation
b, a = signal.iirnotch(60, 50, fs) #generate coeffecients of 60Hz filter
voltageFilt = signal.filtfilt(b, a, voltage) #filter 60Hz out of voltage signal

#rectify the voltage measurements
av = np.average(voltageFilt) #get average voltage for rectification, could possibly be removed later and hard coded but due to the reference not being set and consistent this is the work around
rectV = abs(voltageFilt-av)

#legacy threshold setting
#t = int(np.ceil(1/(time[2]-time[1])))
#av = np.average(voltageFilt[0:t])
#stddev = np.std(voltageFilt[0:t])

threshold = 25 #temp value - when final configuration is determined threshold will be hard coded to be slightly above the standard noise level
midPulse = False

#take moving average of the voltage over the entire dataset
mov = movingAverages(rectV) 

#identify and quantify muscle pulses in the dataset
VrmsLog, TDMFLog, startTimeLog, endTimeLog, midPulse = quantMusclePulse(rectV, fs)

#function will ultimately return VrmsLog, TDMFLog and midPulse to app
#VrmsLog nad TDMFLog should be compared with previous Vrms' and TDMF's and midpulse is used to determine if the last packet of the previous should be repeated to catch the full burst
# could be better to not have midPulse and instead ignore pulses that start/end between packets 
#print returned VrmsLog, TDMFLog, midPulse
for i in range(len(VrmsLog)):
    print("VRMS: "+str(VrmsLog[i])+"mV, TDMF: "+str(TDMFLog[i]*1000)+"Hz, Start Time: "+str(startTimeLog[i])+"("+str(startTimeLog[i]*(1/fs))+") End Time: "+str(endTimeLog[i])+"("+str(endTimeLog[i]*(1/fs))+")")

#below here graphs and shows the data within pyplot, likely best to remove for app integration
#plot the rectified voltage and the moving average for visualisation 
mpl.plot(rectV, 'b')
#mpl.plot(voltage, 'b')
mpl.plot(mov, 'g')

maxV = np.max(rectV)

#add lines showing bounds of each muscle burst
for i in range(len(startTimeLog)):
    mpl.vlines(startTimeLog[i], 0, maxV, 'r')
    mpl.vlines(endTimeLog[i], 0, maxV, 'r')

#add line for the threshold for visualisation
mpl.hlines(threshold, 0, len(voltage), 'r')

#set graph bounds
mpl.ylim(0, maxV)
mpl.xlim(0, len(voltage))
mpl.show()