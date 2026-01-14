# Bioband Display

**Bioband Display** is an Android application designed to connect to and visualize real-time data from a custom nRF52840-based bio-sensing device. It serves as the mobile interface for a complete embedded system, handling Bluetooth LE communication, data processing, and graphical representation.

## What It Does

The app scans for a specific bio-sensing device named `Test Device`, establishes a Bluetooth Low Energy (BLE) connection, and listens for incoming data notifications. Upon receiving data, the app uses an integrated Python environment (via Chaquopy) to process it and then displays the results as a live, auto-scrolling line graph.

This project demonstrates a full end-to-end data pipeline: from a hardware sensor, through an embedded microcontroller and BLE, to a mobile application for real-time analysis and visualization.

## Key Features

-   **Automatic BLE Connection:** Scans for and connects to a specific nRF52840 device automatically.
-   **Robust Connection Management:** Handles device disconnections gracefully and attempts to reconnect.
-   **Two-Screen Interface:**
    -   A **Connection Screen** (`MainActivity`) that shows the current BLE connection status.
    -   A **Graphing Screen** (`GraphActivity`) that displays the live data feed.
-   **On-Device Data Processing:** Utilizes a **Python** backend (`analyzer.py`) integrated directly into the app to process raw sensor data using libraries like `NumPy` and `SciPy`.
-   **Real-time Data Visualization:** Renders the processed data on a smooth, auto-scrolling line graph using the `MPAndroidChart` library.

## How to Get Started

### Prerequisites

1.  **Android Studio:** The latest stable version.
2.  **Android Device/Emulator:**
    -   A physical Android phone running Android 7.0 (Nougat, API 24) or newer with Bluetooth LE support.
    -   *(Alternatively)* An Android Emulator with Bluetooth passthrough configured.
3.  **Python for Build Machine:** A local installation of **Python 3.10** is required for the Chaquopy build process. Ensure it is added to your system's PATH.
4.  **Hardware:** The custom nRF52840 "Bioband" device, flashed with the corresponding firmware from this project.

### Setup and Installation

1.  **Clone the repository:**
    git clone <https://github.com/kristinscot/Continuous-Health-Monitoring-/tree/main>
2.  **Open in Android Studio:**
    -   Open Android Studio and select `Open`.
    -   Navigate to the cloned repository folder and open it.

3.  **Verify Device and App Configuration:**
    -   **Firmware:** Ensure your nRF52840 device firmware (`main.c`) is configured to advertise with the name **`Test Device`**.
    -   **Android App:** The app is hard-coded to look for this specific name in `MainActivity.kt`. If you change the name on the device, you must update the `DEVICE_NAME` variable in the app as well.

4.  **Sync Project:**
    -   Let Android Studio sync the project with Gradle. This will also trigger Chaquopy to download and configure the Python 3.10 environment and the specified packages (`numpy`, `scipy`). This may take a few minutes on the first run.

5.  **Run the App:**
    -   Connect your physical Android device or start your configured emulator.
    -   Click the **Run** button in Android Studio to build and install the app.

### Usage

1.  **Power On the nRF52840 Device:** Ensure the bio-sensing device is powered on and within Bluetooth range.
2.  **Launch the App:** Open the "Bioband Display" app on your phone.
3.  **Connect:** The main screen will show the connection status, starting with "Scanning...". Once connected, it will display "Device Connected!" and a button will appear.
4.  **View Graph:** Click the **"Show Graph"** button to navigate to the data visualization screen, where you will see the live graph update as data is received.

## Where to Get Help

-   **Project Issues:** For bugs or feature requests related to this app, please open an issue in this repository's "Issues" tab.
-   **Chaquopy Documentation:** For questions about the Python integration, refer to the [Chaquopy v15.0.1 documentation](https://chaquo.com/chaquopy/doc/15.0.1/).
-   **BLE on Android:** For general Android Bluetooth LE questions, the official [Android BLE Guide](https://developer.android.com/guide/topics/connectivity/bluetooth/ble-overview) is the best resource.

## Who We Are

This project is maintained and developed as a university capstone project.

-   **Maintainer:** Kristin
-   **Contributors:** We welcome contributions from all team members! Please see the `CONTRIBUTING.md` file (if one exists) for guidelines on how to submit pull requests and contribute to the project.