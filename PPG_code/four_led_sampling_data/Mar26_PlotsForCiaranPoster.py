# Author: Ciaran McDonald-Jensen
# Date Created: Mar 26, 2026
# Purpose: This is to make some plots to visualize the raw data outputted from PPG_DC_IDAC March version outputs
# (Ciaran and Andrew version of PPG_DC_IDAC that outputs time, AC Values*4, DC Values*4, IMU Values)



# Order of values is green, red, IR, all off


import numpy as np
from matplotlib import pyplot as plt
import csv
from matplotlib.figure import Figure
from matplotlib.lines import Line2D
from matplotlib.backend_bases import PickEvent, MouseButton

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


print("Program Started")

FIRST_STAGE_RESISTOR_K = 270
SECOND_STAGE_GAIN = 33
VREF = 1500

colours = [
    'green',
    'red',
    'maroon', #IR
    'blue' #All off
]
names = [
    'Green',
    'Red',
    'IR',
    'Background'
]


# YOU NEED TO REPLACE THIS TO BE THE FILE YOU WANT (Note: Using absolute file path right now)
data_folder_path = "/home/ciaran-mcdj/Documents/School/ELEC4908_Capstone/Code/Continuous-Health-Monitoring-/PPG_code/four_led_sampling_data/Data/PPG_and_IMU_DATA/"
data_filename = "attempt_2.txt"


# PARSE DATA FROM FILE
deltaTs_us:list[int] = []
acAmplitude_raw:list[tuple[int,int,int,int]] = []
dcAmplitude_raw:list[tuple[int,int,int,int]] = []
with open(data_folder_path+data_filename, "r") as csvFile:
    # print(file.read())
    i = 0
    csvReader = csv.reader(csvFile)

    # freqList = []
    # amplitudeList = []
    
    # Extract data
    for row in csvReader:
        deltaTs_us.append(int(row[0]))
        acAmplitude_raw.append((
            -int(row[1]),
            -int(row[2]),
            -int(row[3]),
            -int(row[4])
        ))
        dcAmplitude_raw.append((
            int(row[5]),
            int(row[6]),
            int(row[7]),
            int(row[8])
        ))

# Convert to useful numpy arrays
Ts_us_list = []
time_running_total = 0
for deltaT in deltaTs_us:
    time_running_total += deltaT
    Ts_us_list.append(time_running_total)
sampleTime_us = np.array(Ts_us_list)

acAmplitude_mV_afterGain = np.array(acAmplitude_raw)
acAmplitude_uA = np.divide(
    np.divide(
        np.subtract(acAmplitude_mV_afterGain, -VREF),
        SECOND_STAGE_GAIN),
    FIRST_STAGE_RESISTOR_K)

dcAmplitude_mV_afterGain = np.array(dcAmplitude_raw)
dcAmplitude_uA = np.divide(
    np.divide(
        np.subtract(dcAmplitude_mV_afterGain, (VREF*SECOND_STAGE_GAIN)),
        SECOND_STAGE_GAIN),
    FIRST_STAGE_RESISTOR_K)

print(dcAmplitude_mV_afterGain)
print(dcAmplitude_uA)





lines:list[Line2D] = [] #List of lines that get made for use with the legend

# MAKE PLOT OF RAW DATA
fig = plt.figure()
ax = fig.add_subplot()
ax.set_title("Measured PPG Signal", fontsize=18)
ax.set_xlabel("Time (s)", fontsize=16)
ax.set_ylabel("Photodiode Signal (uA)", fontsize=16)

# AC
for i in range(4):
    acLine, = ax.plot(np.divide(sampleTime_us, 10**6), -acAmplitude_uA[:,i], linestyle="-", marker=".", label="AC signal", color=colours[i])
    lines.append(acLine)

# DC
for i in range(4):
    dcLine, = ax.plot(np.divide(sampleTime_us, 10**6), -dcAmplitude_uA[:,i], linestyle="-", marker=".", label="DC signal", color=colours[i])
    lines.append(dcLine)

# AC+DC
for i in range(4):
    acdcLine, = ax.plot(np.divide(sampleTime_us, 10**6), -np.add(acAmplitude_uA[:,i],dcAmplitude_uA[:,i]), linestyle="-", marker=".", label="DC+AC signal", color=colours[i])
    lines.append(acdcLine)










# # # PLOT ZOOMED IN AC + DC DATA WITH LINE BREAKS SO ALL IS VISIBLE
# weightOfPlot1 = 1
# weightOfPlot2 = 1
# weightOfPlot3 = 1
# rangeOfWeight1_nA = 0.35
# minPlot1 = -0.2
# minPlot2 = -2.03
# minPlot3 = -5.65
# boundsOfTime = (9,15)  #(5, 20)

# fig, (ax1, ax2, ax3) = plt.subplots(3, 1, sharex=True, gridspec_kw={'height_ratios': [weightOfPlot1, weightOfPlot2, weightOfPlot3]})
# fig.subplots_adjust(hspace=0.1)  # adjust space between Axes
# ax1.set_title("Measured PPG Signal", fontsize=18)
# ax3.set_xlabel("Time (s)", fontsize=16)
# ax2.set_ylabel("Photodiode Signal (uA)", fontsize=16)

# # plot the same data on both Axes
# for ax in [ax1, ax2, ax3]:
#     for i in [0,3,1,2]: #Weird order to make legend have nice order for plot
#         if i == 0:
#             # Don't plot the green one
#             continue
#         acdcLine, = ax.plot(np.divide(sampleTime_us, 10**6), -np.add(acAmplitude_uA[:,i],dcAmplitude_uA[:,i]), linestyle="", marker=".", label=names[i]+" signal", color=colours[i])
#         lines.append(acdcLine)

# # zoom-in / limit the view to different portions of the data
# ax1.set_ylim(minPlot1, minPlot1+rangeOfWeight1_nA*weightOfPlot1)
# ax2.set_ylim(minPlot2, minPlot2+rangeOfWeight1_nA*weightOfPlot2) 
# ax3.set_ylim(minPlot3, minPlot3+rangeOfWeight1_nA*weightOfPlot3)
# ax1.set_xlim(boundsOfTime[0], boundsOfTime[1]) #All x-axes are linked

# # hide the spines between axes
# ax1.spines.bottom.set_visible(False)
# ax2.spines.top.set_visible(False)
# ax2.spines.bottom.set_visible(False)
# ax3.spines.top.set_visible(False)
# ax1.xaxis.tick_top()
# ax1.tick_params(labeltop=False)  # don't put tick labels at the top
# ax2.xaxis.set_ticks_position('none')
# ax3.xaxis.tick_bottom()

# # Now, let's turn towards the cut-out slanted lines.
# # We create line objects in axes coordinates, in which (0,0), (0,1),
# # (1,0), and (1,1) are the four corners of the Axes.
# # The slanted lines themselves are markers at those locations, such that the
# # lines keep their angle and position, independent of the Axes size or scale
# # Finally, we need to disable clipping.

# d = .5  # proportion of vertical to horizontal extent of the slanted line
# kwargs = dict(marker=[(-1, -d), (1, d)], markersize=12,
#               linestyle="none", color='k', mec='k', mew=1, clip_on=False)
# ax1.plot([0, 1], [0, 0], transform=ax1.transAxes, **kwargs)
# ax2.plot([0, 1], [1, 1], transform=ax2.transAxes, **kwargs)
# ax2.plot([0, 1], [0, 0], transform=ax2.transAxes, **kwargs)
# ax3.plot([0, 1], [1, 1], transform=ax3.transAxes, **kwargs)















leg = plt.legend(fontsize=12)
leg.set_draggable(True)

activateLegendLineToggling(fig, legLines=leg.get_lines(), togglingLines=lines)

plt.show()




print("Program Ended")