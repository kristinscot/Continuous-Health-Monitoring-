'''
The following code is a general example that chatgpt gave to help kickstart intergrating
the any microprocessor with an app. Obviously this will be edited with any specifications 
we are looking for. Just a good launch point. 

Author: Kristin Scott
Date: Tuesday, September 30, 2025
Version: 1.0
*Note this code has use of AI*
'''

import asyncio
import threading
from kivy.app import App
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.button import Button
from kivy.clock import Clock
from bleak import BleakClient, BleakScanner

# 🔧 Replace with your device info
DEVICE_NAME = "MyBLEDevice"  # <-- change to your device name

# Each sensor’s characteristic UUID (replace with your own)
CHAR_UUIDS = {
    "temperature": "00002a6e-0000-1000-8000-00805f9b34fb",  # Temperature
    "heart_rate":  "00002a37-0000-1000-8000-00805f9b34fb",  # Heart Rate
    "pressure":    "00002a6d-0000-1000-8000-00805f9b34fb",  # Pressure
    "ph_level":    "12345678-1234-5678-1234-56789abcdef0",  # Example custom UUID
}

class BLEApp(App):
    def build(self):
        self.layout = BoxLayout(orientation='vertical', padding=20, spacing=10)

        # Labels for each sensor
        self.status_label = Label(text="Press 'Connect' to start reading", font_size=16)
        self.temp_label = Label(text="Temperature: -- °C", font_size=18)
        self.hr_label = Label(text="Heart Rate: -- bpm", font_size=18)
        self.pressure_label = Label(text="Pressure: -- Pa", font_size=18)
        self.ph_label = Label(text="pH Level: --", font_size=18)

        self.connect_button = Button(text="Connect", size_hint=(1, 0.2))
        self.connect_button.bind(on_press=self.start_ble_thread)

        # Add widgets
        self.layout.add_widget(self.status_label)
        self.layout.add_widget(self.temp_label)
        self.layout.add_widget(self.hr_label)
        self.layout.add_widget(self.pressure_label)
        self.layout.add_widget(self.ph_label)
        self.layout.add_widget(self.connect_button)
        return self.layout

    def start_ble_thread(self, instance):
        self.connect_button.disabled = True
        self.status_label.text = "Scanning for device..."
        threading.Thread(target=self.run_ble_loop, daemon=True).start()

    def run_ble_loop(self):
        asyncio.run(self.ble_main())

    async def ble_main(self):
        try:
            device = await self.find_device_by_name(DEVICE_NAME)
            if not device:
                Clock.schedule_once(lambda dt: self.update_status("Device not found"))
                self.connect_button.disabled = False
                return

            Clock.schedule_once(lambda dt: self.update_status(f"Connecting to {device.name}..."))

            async with BleakClient(device) as client:
                if not client.is_connected:
                    Clock.schedule_once(lambda dt: self.update_status("Failed to connect"))
                    self.connect_button.disabled = False
                    return

                Clock.schedule_once(lambda dt: self.update_status("Connected! Reading data..."))

                while client.is_connected:
                    await self.read_all_sensors(client)
                    await asyncio.sleep(1)  # Polling interval (seconds)

        except Exception as e:
            Clock.schedule_once(lambda dt: self.update_status(f"Error: {e}"))
            self.connect_button.disabled = False

    async def read_all_sensors(self, client):
        """Poll all sensor characteristics and update UI"""
        for sensor, uuid in CHAR_UUIDS.items():
            try:
                data = await client.read_gatt_char(uuid)
                value = self.parse_data(data)
                Clock.schedule_once(lambda dt, s=sensor, v=value: self.update_sensor_label(s, v))
            except Exception as e:
                Clock.schedule_once(lambda dt: self.update_status(f"Read error ({sensor}): {e}"))

    async def find_device_by_name(self, name):
        devices = await BleakScanner.discover(timeout=5.0)
        for d in devices:
            if d.name == name:
                return d
        return None

    def parse_data(self, data: bytes):
        """Convert BLE bytes to int or string"""
        try:
            return round(int.from_bytes(data, byteorder='little') / 100, 2)
        except Exception:
            try:
                return data.decode('utf-8')
            except:
                return data.hex()

    # 🧱 UI Update Helpers
    def update_status(self, text): self.status_label.text = text

    def update_sensor_label(self, sensor, value):
        if sensor == "temperature":
            self.temp_label.text = f"Temperature: {value} °C"
        elif sensor == "heart_rate":
            self.hr_label.text = f"Heart Rate: {value} bpm"
        elif sensor == "pressure":
            self.pressure_label.text = f"Pressure: {value} Pa"
        elif sensor == "ph_level":
            self.ph_label.text = f"pH Level: {value}"

if __name__ == "__main__":
    BLEApp().run()


