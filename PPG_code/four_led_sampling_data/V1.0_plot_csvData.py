# Author: Ciaran McDonald-Jensen
# Date Created: January 22, 2026
# Purpose: This program plots the data outputted from four_led_sampling_data after it has been formatted as a csv by convert_data_to_csv

# TODO - add a readme for all files that include how to use the project as well as the required dependancies
import numpy as np
import scipy.special
import csv
from typing import NamedTuple, List

from matplotlib import pyplot as plt
from matplotlib.figure import Figure
from matplotlib.lines import Line2D
from matplotlib.backend_bases import PickEvent, MouseButton

# YOU NEED TO REPLACE THIS TO BE THE FILE YOU WANT (Note: Using absolute file path right now) SHOULD BE FORMATTED ONE
dataFolderPath = "/home/ciaran-mcdj/Documents/School/ELEC4908_Capstone/Code/Continuous-Health-Monitoring-/PPG_code/four_led_sampling_data/Data/"
dataFilename = "serial-terminal-22012026_182729_formatted.txt"
# NOTE: Output file name is same place as input with same name with '_formatted' at end
if dataFilename[-14:] != "_formatted.txt":
    print("WARNING: Your data_filename doesn't end with '_formatted.txt', make sure you ran this data through convert_data_to_csv")



# This function is self contained and not relevant to the rest of code. Just allows toggling of lines for visibility
def activateLegendLineToggling(fig:Figure, legLines:list[Line2D], togglingLines:list[Line2D], pickRadius:float=5, enableLeftClickHide:bool=True, enableRightClickMarkerSwitch:bool=True):
    """Assign event handling so when click on legLines, toggles visibility of associated togglingLines on left click, on right click changes marker to be more visible
    used example from https://matplotlib.org/stable/gallery/event_handling/legend_picking.html#sphx-glr-gallery-event-handling-legend-picking-py
    
    NOTE: These should probably be two different functions, with the marker one having more functionality

    Inputs:
    fig: figure with canvas where legLines are
    legLines: leg.get_lines()
    togglingLines: each element is a lines that get toggled when associated legLine gets clicked
    pickRadius: I think in pixels?
    
    """
    mapLegendToAx:dict[Line2D,list[Line2D]] = {} # Will map legLines to togglingLines

    for legLine, togLines in zip(legLines, togglingLines):
        legLine.set_picker(pickRadius)  # Enable picking on the legend line.
        mapLegendToAx[legLine] = togLines
        # print(debug: legend_line)


    def onPick(event:PickEvent):
        # On the pick event, find the original line corresponding to the legend proxy line, and toggle its visibility.
        clickedLine = event.artist
        # print("debug: picked:", clickedLine)

        # Do nothing if the source of the event is not a legend line.
        if clickedLine not in mapLegendToAx:
            # print("debug: picked something else pickable that wasn't a legend line???:", clickedLine)
            return

        togLine = mapLegendToAx[clickedLine]
        if enableLeftClickHide and event.mouseevent.button == MouseButton.LEFT:
            visible = not togLine.get_visible() #Assume all lines in togLines in same state, if they aren't this will set them all to opposite state of first one in list
            #Toggle visibility
            togLine.set_visible(visible)
            # Change the alpha on the line in the legend, so we can see what lines have been toggled.
            clickedLine.set_alpha(1.0 if visible else 0.2)
            fig.canvas.draw()
        if enableRightClickMarkerSwitch and event.mouseevent.button == MouseButton.RIGHT:
            # print("Right clicked on rlevant thing!!!!")
            linesMarker = togLine.get_marker()
            if linesMarker == 's':
                # Set back to point (note: if wasnt originally point... of well, this should be improved)
                newMarker = '.'
            else:
                newMarker = 's'
            #Toggle marker
            togLine.set_marker(newMarker)
            clickedLine.set_marker(newMarker)
            fig.canvas.draw()

    fig.canvas.mpl_connect('pick_event', onPick)


class StateData(NamedTuple):
    stateStartTime: List[int] #length numSamples
    stateEndTime: List[int] #length numSamples
    readsTaken: List[int] #length numSamples
    readsRunningTotal: List[int] #length numSamples
    readsBuffer: List[int] #length numSamples*buffer
    readsBufferTimestamps: List[int] #length numSamples*buffer


def makeStateData():
    # Need this to remove the link between lists in the different StateData's
    return StateData([], [], [], [], [], [])

allData = [makeStateData(), makeStateData(), makeStateData(), makeStateData()] #4 StateData structures, each one for one state

def extractRowData(row, bufferSize:int):
    # Extracts one row of data into allData
    state = int(row[0])
    if state != 0 and state != 1 and state != 2 and state != 3:
        print("WARNING: What we expected to be a state (0, 1, 2, or 3) was actually:", state, ", This probably means the data isn't formatted as expected")

    allData[state].stateStartTime.append(int(row[1]))
    allData[state].stateEndTime.append(int(row[2]))
    readsTakenThisState = int(row[3])
    allData[state].readsTaken.append(readsTakenThisState)
    allData[state].readsRunningTotal.append(int(row[4]))
    for i in range(readsTakenThisState):
        allData[state].readsBuffer.append(int(row[6+i]))
        allData[state].readsBufferTimestamps.append(int(row[6+bufferSize+i]))


# Import data
with open(dataFolderPath+dataFilename, "r") as csvFile:
    csvReader = csv.reader(csvFile)

    # Getting first line seperately to get buffersize which will be used for the rest of the rows
    firstRow = next(csvFile).split(',')
    bufferSize = int(firstRow[5]) #This is the index for the buffer size in V1.0
    extractRowData(firstRow, bufferSize)

    for row in csvReader:
        extractRowData(row, bufferSize)
        
# print(allData)


fig = plt.figure()
ax = fig.add_subplot()
ax.set_title("Data measured from PPG sensor")
ax.set_xlabel("Time (us)")
ax.set_ylabel("Voltage (mV)")





# MAKE PLOT
lines:list[Line2D] = [] #List of lines that get made for use with the legend

# Get max and min points (used for vertical bars) - NOTE: Currently just setting it, could set up fancy logic to compute them, but this makes sense based on limit of adc
maxVolt = 3300
minVolt = 0

for state in [0,1,2,3]:
    # For each state, plot a bunch of things
    if state == 0:
        colour = "green"
        text_label = "green"
    elif state == 1:
        colour = "red"
        text_label = "red"
    elif state == 2:
        colour = "maroon"
        text_label = "IR"
    elif state == 3:
        colour = "blue"
        text_label = "none"

    # Plot the measurements done
    bufferLine, = ax.plot(allData[state].readsBufferTimestamps, allData[state].readsBuffer, linestyle="", marker=".", label="All Buffer for "+text_label, color=colour)
    lines.append(bufferLine)

    # Plot only the average points (one per sample). This is likely what the final program will keep
    averageVoltages = np.divide(np.array(allData[state].readsRunningTotal), np.array(allData[state].readsTaken))
    startTimes = np.array(allData[state].stateStartTime)
    endTimes = np.array(allData[state].stateEndTime)
    averageVoltageTimes = np.add(startTimes, endTimes)/2
    averagePoints, = ax.plot(averageVoltageTimes, averageVoltages, linestyle="", marker="o", label="Average points for "+text_label, color=colour)
    lines.append(averagePoints)

    # Plot vertical dotted lines representing the start times
    for startTime in startTimes:
        ax.vlines(startTime, minVolt, maxVolt, colors=colour, alpha=0.1)
    
    # Plot vertical dotted lines representing where data sampling is expected to start
    EXPECTED_DELAY_TIME = [10000,10000,10000,10000] #us
    for startTime in startTimes:
        ax.vlines(startTime+EXPECTED_DELAY_TIME[state], minVolt, maxVolt, colors=colour, alpha=0.1)

    # Plot vertical dotted lines representing the end times
    # for endTime in endTimes:
    #     ax.vlines(endTime, minVolt, maxVolt, colors=colour, alpha=0.1)




    





leg = plt.legend()

activateLegendLineToggling(fig, legLines=leg.get_lines(), togglingLines=lines)

plt.show() #When uncommented, this creates the first graph 



print("Program Ended")











