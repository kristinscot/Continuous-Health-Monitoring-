import matplotlib.pyplot as mpl
import numpy as np
import csv as csv

voltage = []
time = []
mov = []

def importCSV(path):
    voltage = []
    time = []
    with open(path, newline='') as csvfile:
        data = list(csv.reader(csvfile))
    for line in data:
        time.append(float(line[0]))
        voltage.append(abs(float(line[1])))
    return voltage, time

def movingAverages(voltage):
    mov = []
    r = len(voltage)
    w = 700 #width of the moving average
    for i  in range(r):
        if((i+w)>r):
            w = r-i
        mov.append(np.average(voltage[i:(i+w)]))
    return mov

def quantMusclePulse(time, voltage):

    for i in range(time):
        #if()
        break
    
voltage, time = importCSV("BPREAL01.CSV")


t = int(np.ceil(2/(time[2]-time[1])))
av = np.average(voltage[0:t])
stddev = np.std(voltage[0:t])

thresold = av+2*stddev
fs = 1/(np.average(np.diff(time)))
print("sampling rate: ",fs)
print("Threshold:"+str(av)+"+2*"+str(stddev)+"="+str(thresold))

mov = movingAverages(voltage)

mpl.style.use('_mpl-gallery')
fix, ax = mpl.subplots()

for i in range(len(time)):
    ax.bar(time[i], voltage[i], width=1, edgecolor="white", linewidth=0.7)

ax.set(xlim=(0, 8), xticks=np.arange(1, 8),
       ylim=(0, 8), yticks=np.arange(1, 8))

mpl.show()