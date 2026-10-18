# DIY RC Excavator - ESP32-S3 Upgrade (MCP23017 Elimination)

This project is a highly optimized remix of the brilliant [DIY RC Excavator by ProfessorBoots](https://printables.com). 

While the original design is fantastic, it relies on the **MCP23017** port expander to handle the large number of motors. In many regions, this chip has become hard to find or extremely expensive. Furthermore, driving multiple motors smoothly via I2C can hit performance bottlenecks.

This upgrade completely eliminates the need for the MCP23017, increases motor performance, and reduces total build costs by utilizing a dual-MCU setup.

---

## 🚀 Key Improvements

* **Dual-MCU Architecture (No MCP23017 needed):** I added a cheap **ESP32-S3 Super Mini** directly underneath the main ESP32 Dev Board. They communicate via high-speed **TX/RX (UART)**. The second ESP32 dedicatedly generates independent **PWM signals** to control **8 motors** with full forward/reverse functionality.
* **Higher Speed & Power:** Raised the system battery voltage to **9V**.
* **Upgraded Motor Drivers:** Swapped standard drivers for red **DRV8833** modules. Combined with the 9V supply, the excavator is significantly faster, more responsive, and has much more torque.

---

## 📂 Repository Content & Files

Instead of a scattered folder structure, all necessary project files are included directly in this repository:

* 📄 **`RC Excavator_DE_EN.pdf`** – Complete wiring, control schematic, and documentation (in German & English).
* 📜 **`Mini_Excavator_Esp1.1.ino`** – Firmware for the primary ESP32 (Receiver / Main Logic).
* 📜 **`Mini_Excavator_Esp2.1.ino`** – Firmware for the ESP32-S3 Super Mini (PWM / Motor Coprocessor).
* 📦 **`PCB-RC-Excavator-main.zip`** – Complete Gerber files to manufacture your own custom PCB for this dual-ESP32 setup.

### 📐 3D Printable Files (STLs)
The modified 3D files to fit the extra electronics can be found directly on the official Printables Remix page:
🔗 **[Printables Remix - DIY RC Excavator]([https://printables.com/remixes)](https://www.printables.com/model/1881578-rc-excavator-proportional-control-alternative-pcb)**

---

## 🛠️ Hardware Requirements

* 1x ESP32 Dev Board (Main Controller)
* 1x ESP32-S3 Super Mini (Motor Coprocessor)
* Red DRV8833 Motor Driver Modules
* 9V Battery / Power Source Setup
* Custom PCB (using the provided Gerber ZIP)

---

## ☕ Support this Project

Developing the dual-MCU communication, designing the custom PCB, and fine-tuning the 9V power delivery took a significant amount of time, testing, and reverse engineering. If this upgrade saves you money on expensive chips or makes your RC excavator build experience better, I would highly appreciate a small tip!

[![Support me on Ko-fi](https://shields.io)](https://ko-fi.com/rselectronic)

---

## License & Credits
Original Concept by ProfessorBoots. This modification is shared for community improvement.

