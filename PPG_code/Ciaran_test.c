// Author: Ciaran McDonald-Jensen
// Date Created: Jan 9, 2026
// Purpose: Messing around with code for the nRF52840. Will move this code to a different file or change this description when it is useful


// NOTE: Specifications page 926 has details about all the pins and which ones we should use







// PLAN FOR FINAL CODE:
// Initial Setup stuff 
    // GPIO pin setup
    // Data storage file setup
// Main Loop (loop for every sampling cycle)
    // Go through the 7 states, for each state:
        // Change output pins to have proper LED on
        // Wait rise time
        // Start sampling
        // Wait remaining time
        // Stop sampling
        // NOTE: eventually should (probably) average the values during one sample to get one sample value
    // Store all samples into data file



// PLAN FOR WHAT I NEED TO DO NEXT

// Figure out:
    // How to upload any code
    // How to read digital data with input GPIO pin (verify the input impedance and write it down somewhere)
    // How to output voltage/current to an output GPIO pin
    // How to log info in a csv file
    // How to export csv file to my computer (bluetooth???)
// Phases
    // Can read continuous data while changing output pins, and log continuously in csv file that I can look at. Can