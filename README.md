# DIY RC Excavator - ESP32-S3 Upgrade (MCP23017 Elimination)

This project is a highly optimized remix of the brilliant [DIY RC Excavator by ProfessorBoots]([https://printables.com](https://www.printables.com/model/833291-diy-rc-excavator)). 

While the original design is fantastic, it relies on the **MCP23017** port expander to handle the large number of motors. In many regions, this chip has become hard to find or extremely expensive. Furthermore, driving multiple motors smoothly via I2C can hit performance bottlenecks.

This upgrade completely eliminates the need for the MCP23017, increases motor performance, and reduces total build costs.

---

## 🚀 Key Improvements

* **Dual-MCU Architecture (No MCP23017 needed):** I added a cheap **ESP32-S3 Super Mini** directly underneath the main ESP32 Dev Board. They communicate via high-speed **TX/RX (UART)**. The second ESP32 dedicatedly generates independent **PWM signals** to control **8 motors** with full forward/reverse functionality.
* **Higher Speed & Power:** Raised the system battery voltage to **9V**.
* **Upgraded Motor Drivers:** Swapped standard drivers for red **DRV8833** modules. Combined with the 9V supply, the excavator is significantly faster, more responsive, and has much more torque.

---

## 🛠️ Hardware Requirements

* 1x ESP32 Dev Board (Main Controller)
* 1x ESP32-S3 Super Mini (Motor Coprocessor)
* 4x Red DRV8833 Motor Driver Modules  

---

## 📂 Repository Structure

* `/Firmware_Main` - Code for the primary ESP32 (Receiver / Logic)
* `/Firmware_Coprocessor` - Code for the ESP32-S3 Super Mini (PWM / Motor Control)
* `/Schematics` - Wiring diagrams for the dual-ESP32 setup and DRV8833 drivers
* `/STLs` - Modified 3D printable files for fitting the extra electronics

---

## ☕ Support this Project

Developing, wiring, and coding this dual-MCU system took a significant amount of time, testing, and reverse engineering. If this upgrade saves you money on expensive chips or makes your RC excavator build experience better, I would highly appreciate a small tip!

[![Support me on Ko-fi](https://shields.io)](https://ko-fi.com/rselectronic)

*(Please replace `(https://ko-fi.com/rselectronic)` with your actual Ko-fi URL in the markdown file!)*

---

## License & Credits
Original Concept by Proffboots. This modification is shared for community improvement. Check the respective code folders for licensing details.
