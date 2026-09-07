# FocalTech FT6336U Register Map Reference Manual

> **Document Source:** Translated from `FT6336U_Register (1).xlsx`  
> **Target Hardware:** FocalTech FT6336U / FT6236 / FT6426 Self-Capacitive Touch Panel Controller  
> **Default I2C Address:** `0x38` (7-bit) | Write: `0x70` | Read: `0x71`  

---

## Table of Contents
1. [Overview & Page Switching](#1-overview--page-switching)
2. [Normal Operating Mode (Sheet 1: 普通模式)](#2-normal-operating-mode-sheet-1)
   - [2.1 Summary Register Table](#21-summary-register-table)
   - [2.2 Gesture Definition Registers (0xD0 - 0xD8)](#22-gesture-definition-registers-0xd0---0xd8)
3. [Factory / Test Mode (Sheet 2: 工厂模式)](#3-factory--test-mode-sheet-2)
   - [3.1 Factory Mode Register Table](#31-factory-mode-register-table)
   - [3.2 Factory Diagnostic & Calibration Instructions](#32-factory-diagnostic--calibration-instructions)
4. [Quick Programming Reference for Embedded Developers](#4-quick-programming-reference-for-embedded-developers)

---

## 1. Overview & Page Switching

The FT6336U memory map is partitioned into two distinct functional pages/modes controlled by Register `0x00` (`Mode_Switch`):

| Target Mode | Register Address | Value to Write | Typical Application |
|---|:---:|:---:|---|
| **Normal Operating Mode** | `0x00` | `0x00` | Standard coordinate reporting, gestures, system status, power modes |
| **Factory / Test Mode (TEST0)** | `0x00` | `0x40` | Manufacturing calibration, raw ADC data readout (RawData), base capacitance (CB), short-circuit test (RS) |

---

## 2. Normal Operating Mode (Sheet 1)

- **Page Name:** Normal Mode (工作模式 / 普通模式)
- **Page Switch Command:** Write `0x00` to register `0x00`

### 2.1 Summary Register Table

| Address | Length | R/W | Symbol | Name (English) | Default | Range (Min - Max) | Description & Bit Definitions |
|:---:|:---:|:---:|:---|:---|:---:|:---:|:---|
| `0x00` | 1 Byte | **RW** | `Mode_Switch` | Register Page / Mode Switch | `` | `` | Write 0x40 to switch to Factory Mode. Reading returns 0x00. |
| `0x01` | 1 Byte | **RO** | `Reserved` | Reserved | `0x00` | `0x00` | Reserved for internal use. |
| `0x02` | 1 Byte | **RO** | `TD_STATUS` | Touch Point Count | `0x00` | `0x00 ~ 0x02` | Bits [3:0]: Number of active touch points. Max 2 points simultaneously. |
| `0x03` | 1 Byte | **RO** | `P1_XH` | Touch 1 X-Coord High & Event Flag | `0xFF` | `0xFF` | Bits [7:6]: Event Flag (00b: Press Down, 01b: Lift Up, 10b: Contact, 11b: No Event).<br>Bits [3:0]: Touch 1 X-coordinate bits [11:8]. |
| `0x04` | 1 Byte | **RO** | `P1_XL` | Touch 1 X-Coord Low | `0xFF` | `0xFF` | Bits [7:0]: Touch 1 X-coordinate bits [7:0]. |
| `0x05` | 1 Byte | **RO** | `P1_YH` | Touch 1 Y-Coord High & Touch ID | `0xFF` | `0xFF` | Bits [7:4]: Touch 1 Finger ID.<br>Bits [3:0]: Touch 1 Y-coordinate bits [11:8]. |
| `0x06` | 1 Byte | **RO** | `P1_YL` | Touch 1 Y-Coord Low | `0xFF` | `0xFF` | Bits [7:0]: Touch 1 Y-coordinate bits [7:0]. |
| `0x07` | 1 Byte | **RO** | `P1_WEIGHT` | Touch 1 Pressure / Weight | `0xFF` | `0xFF` | Touch pressure weight value. |
| `0x08` | 1 Byte | **RO** | `P1_MISC` | Touch 1 Area / Misc | `0xFF` | `0xFF` | Bits [7:4]: Touch area. |
| `0x09` | 1 Byte | **RO** | `P2_XH` | Touch 2 X-Coord High & Event Flag | `0xFF` | `0xFF` | Bits [7:6]: Event Flag (00b: Press Down, 01b: Lift Up, 10b: Contact, 11b: No Event).<br>Bits [3:0]: Touch 2 X-coordinate bits [11:8]. |
| `0x0A` | 1 Byte | **RO** | `P2_XL` | Touch 2 X-Coord Low | `0xFF` | `0xFF` | Bits [7:0]: Touch 2 X-coordinate bits [7:0]. |
| `0x0B` | 1 Byte | **RO** | `P2_YH` | Touch 2 Y-Coord High & Touch ID | `0xFF` | `0xFF` | Bits [7:4]: Touch 2 Finger ID.<br>Bits [3:0]: Touch 2 Y-coordinate bits [11:8]. |
| `0x0C` | 1 Byte | **RO** | `P2_YL` | Touch 2 Y-Coord Low | `0xFF` | `0xFF` | Bits [7:0]: Touch 2 Y-coordinate bits [7:0]. |
| `0x0D` | 1 Byte | **RO** | `P2_WEIGHT` | Touch 2 Pressure / Weight | `0xFF` | `0xFF` | Touch pressure weight value. |
| `0x0E` | 1 Byte | **RO** | `P2_MISC` | Touch 2 Area / Misc | `0xFF` | `0xFF` | Bits [7:4]: Touch area. |
| `0x0F～0x7F` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal use. |
| `0x80` | 1 Byte | **RW** | `ID_G_THGROUP` | Touch Threshold | `0xBB` | `0xBB` | Threshold for touch detection = ID_G_THGROUP * 4 (or / 16 depending on config). |
| `0x81` | 1 Byte | **RW** | `ID_G_PEAKTH` | Peak Threshold | `0x0F` | `0x0F` | Peak threshold: ID_G_PEAKTH * TH_GROUP_DIVISOR = RV_G_PEAK_TH. |
| `0x82～0x84` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal use. |
| `0x85` | 1 Byte | **RW** | `ID_G_THDIFF` | Point Filter Threshold | `0xA0` | `0x00 ~ 0xFF` | Point filter differential threshold (~ RV_G_THDIFF / 16). |
| `0x86` | 1 Byte | **RW** | `ID_G_CTRL` | Monitor Mode Enable Switch | `0x01` | `0x00 ~ 0x01` | 0x01: Allow entering Monitor Mode.<br>0x00: Forbid entering Monitor Mode. |
| `0x87` | 1 Byte | **RW** | `ID_G_TIMEENTERMONITOR` | Delay to Enter Monitor Mode | `0x1E` | `0x00 ~ 0x64` | Idle delay in seconds before entering Monitor state (0 - 100s). Requires ID_G_CTRL=1. |
| `0x88` | 1 Byte | **RW** | `ID_G_PERIODACTIVE` | Active Mode Scan Period | `0x08` | `0x04 ~ 0x14` | Active mode report rate / scan period (Range: 4 - 20ms). |
| `0x89` | 1 Byte | **RW** | `ID_G_PERIODMONITOR` | Monitor Mode Scan Period | `0x08` | `0x04 ~ 0x14` | Monitor mode report rate / scan period (Range: 4 - 20ms). |
| `0x8A` | 1 Byte | **RW** | `ID_G_FRQHOPFLG` | Frequency Hopping Flag | `0x00` | `0x00` | Status flag for frequency hopping anti-noise. |
| `0x8B` | 1 Byte | **RW** | `ID_G_FREQ_HOPPING_EN` | Charger Status Indicator | `0x00` | `0x00 ~ 0x01` | Host sets this when charger is plugged:<br>0x00: Charger unplugged.<br>0x01: Charger plugged in. |
| `0x8C` | 1 Byte | **RW** | `ID_G_CURFREQIDX` | Current Operating Frequency Index | `0x00` | `0x00` | Index of currently active scan frequency channel. |
| `0x8D～0x95` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal use. |
| `0x96` | 1 Byte | **RW** | `ID_G_TEST_MODE_FILTER` | Test Mode Alpha Filter Enable | `0x00` | `0x00 ~ 0x01` | 0x00: Disable Alpha filter in production test.<br>0x01: Enable Alpha filter in production test. |
| `0x97～0x9E` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal use. |
| `0x9F` | 1 Byte | **RO** | `ID_G_CIPHER_MID` | Chip Model Code (Middle Byte) | `0x26` | `0x00 ~ 0xFF` | Default: `0x26`. |
| `0xA0` | 1 Byte | **RO** | `ID_G_CIPHER_LOW` | Chip Model Code (Low Byte) | `0x00` | `0x00 ~ 0x03` | 0x00: FT6236G<br>0x01: FT6336G<br>0x02: FT6336U<br>0x03: FT6426. |
| `0xA1` | 1 Byte | **RO** | `ID_G_LIB_VERSION_H` | App Library Version High Byte | `0x10` | `0x00 ~ 0xFF` | Application library version high byte (Default: 0x10). |
| `0xA2` | 1 Byte | **RO** | `ID_G_LIB_VERSION_L` | App Library Version Low Byte | `0x01` | `0x00 ~ 0xFF` | Application library version low byte (Default: 0x01). |
| `0xA3` | 1 Byte | **RO** | `ID_G_CIPHER_HIGH` | Chip Model Code (High Byte) | `0x64` | `0x00 ~ 0xFF` | Default: `0x64`. (Full Chip ID = `0x642602` for FT6336U). |
| `0xA4` | 1 Byte | **RW** | `ID_G_MODE` | INT Interrupt Mode | `0x01` | `0x00 ~ 0x01` | 0x00: Do not extend INT pulse width.<br>0x01: Extend INT low-pulse width. |
| `0xA5` | 1 Byte | **RW** | `ID_G_PMODE` | Power Consumption Mode | `0x00` | `0x00 ~ 0x03` | 0x00: P_ACTIVE (Normal active)<br>0x01: P_MONITOR (Low power monitor)<br>0x02: P_STANDBY (Standby)<br>0x03: P_HIBERNATE (Deep sleep hibernate). |
| `0xA6` | 1 Byte | **RO** | `ID_G_FIRMID` | Firmware Version | `0x00` | `0x00 ~ 0xFF` | Firmware build version number. |
| `0xA7` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal use. |
| `0xA8` | 1 Byte | **RO** | `ID_G_FOCALTECH_ID` | FocalTech Vendor ID | `0x11` | `0x00 ~ 0xFF` | Vendor identification code. Always reads **`0x11`**. |
| `0xA9` | 1 Byte | **RW** | `ID_G_VIRTUAL_KEY_THRES` | Virtual Key Threshold | `0X19` | `0x00 ~ 0xFF` | Virtual key detection threshold in production test (~ VIRTUAL_KEY_THRESHOLD / 40). |
| `0xAA～0xAC` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal use. |
| `0xAD` | 1 Byte | **RW** | `ID_G_IS_CALLING` | Phone Call Status Flag | `0x00` | `0x00 ~ 0x01` | Host informs chip if a call is active (0x00: Not in call, 0x01: In call) for palm rejection. |
| `0xAE` | 1 Byte | **RW** | `ID_G_FACTORY_MODE` | Factory Mode Selection | `0x00` | `0x00 ~ 0x02` | 0x00: F_NORMAL<br>0x01: F_TESTMODE_1<br>0x02: F_TESTMODE_2. |
| `0xAF` | 1 Byte | **RO** | `ID_G_RELEASE_CODE_ID` | Public Release Code ID | `0x01` | `0x00 ~ 0x01` | Release code identifier. |
| `0xB0` | 1 Byte | **RW** | `ID_G_FACE_DEC_MODE` | Proximity / Face Detect Enable | `0x00` | `0x00 ~ 0x01` | 0x00: Disable face/proximity detection.<br>0x01: Enable face/proximity detection. |
| `0xB1-0xBB` |  | **RW** | `Reserved` | Reserved | `` | `` | Reserved for internal use. |
| `0xBC` | 1 Byte | **WO** | `ID_G_STATE` | Work Mode & Bootloader Upgrade | `0x01` | `0x00 ~ 0x04` | 0x00: InFO Mode.<br>0x01: Normal Mode.<br>0x03: Factory Mode.<br>0x04: Auto-Calibration Mode.<br>*Write 0xAA then 0x55 to trigger firmware upgrade bootloader.* |
| `0xBD～0xCF` |  | **RW** | `Reserved` | Reserved | `` | `` | Reserved for internal use. |
| `0xD0` | 1 Byte | **RW** | `ID_G_SPEC_GESTURE_ENABLE` | Special Gesture Mode Enable | `0x00` | `0x00 ~ 0x01` | 0x00: Normal mode.<br>0x01: Enter gesture detection mode (write 0 to exit). |

### 2.2 Gesture Definition Registers (0xD0 - 0xDA)

When Register `0xD0` (`ID_G_SPEC_GESTURE_ENABLE`) is set to `0x01`, the controller halts standard coordinate reporting and activates the gesture detection engine. The host can enable specific gestures by setting individual bits in registers `0xD1` to `0xD8`:

| Reg | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 | Description |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---|
| `0xD1` | *Reserved* | *Reserved* | Character Master Enable | Double Tap | Swipe Down | Swipe Up | Swipe Right | Swipe Left | Basic motion gestures |
| `0xD2` | 'd' | 'a' | 'g' | 'c' | 'e' | 'm' | 'w' | 'o' | Letter character gestures (Group 1) |
| `0xD3` | \- | \- | \- | \- | \- | \- | \- | \- | **Gesture ID Register (RO):** Contains detected gesture ID code |
| `0xD5` | 'u' | 's' | 'P' | 'L' | 'q' | 'b' | *Reserved* | 'n' | Letter character gestures (Group 2) |
| `0xD6` | *Reserved* | *Reserved* | '△' (Triangle) | 'v' | '^' | '>' | *Reserved* | '@' | Symbol gestures |
| `0xD7` | *Reserved* | *Reserved* | '2' | '8' | '7' | '9' | '6' | '3' | Numeric digit gestures |
| `0xD8` | *Reserved* | *Reserved* | *Reserved* | *Reserved* | 'r' | 'y' | 'k' | 'h' | Letter character gestures (Group 3) |

---

## 3. Factory / Test Mode (Sheet 2)

- **Page Name:** Factory Test Mode (TEST0 / 工厂模式第1页)
- **Page Switch Command:** Write `0x40` to register `0x00`

### 3.1 Factory Mode Register Table

| Address | Length | R/W | Symbol | Name (English) | Default | Range | Description / Details |
|:---:|:---:|:---:|:---|:---|:---:|:---:|:---|
| `0x00` | 1 Byte | **RW** | `Mode_Switch` | Register Page Switch | `` | `` | Write 0x00 to return to Normal Mode. Write 0x40 to stay in Factory Mode. |
| `0x01` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0x02` | 1 Byte | **RW** | `Work_Mode` | Factory Operating Mode | `0x01` | `0x00 ~ 0x04` | 0x00: InFO Mode<br>0x01: Normal Mode<br>0x03: Factory Mode<br>0x04: Auto-Calibration Mode. |
| `0x03～0x06` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0x07` | 1 Byte | **RW** | `RS_MODE_EN` | Micro-Short Test Enable | `0x00` | `0x00 ~ 0x01` | 0x00: Disable micro-short circuit test.<br>0x01: Enable micro-short circuit test. |
| `0x08` | 1 Byte | **RW** | `AcmdREG` | Communication ACK Register | `0x00` | `0x00 ~ 0xFF` | 0x00: RawData is ready for Host reading.<br>0x01 - 0xFF: Host can start preparing next RawData frame. |
| `0x09` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0x0A` | 1 Byte | **RO** | `TP_Channel_Num` | VA Channel Count (RO) | `0x28` | `0x00 ~ 0x3F` | Total sensing channels in View Area (VA). Max: 63 (Default: 40 / 0x28). |
| `0x0B` | 1 Byte | **RO** | `TP_Key_Num` | Virtual Key Channel Count (RO) | `0x00` | `0x00 ~ 0x3F` | Number of touch keys outside VA area. Max: 63. |
| `0x0C` | 1 Byte | **RW** | `AFE_Sample_Cycle` | AFE Scan Sample Count | `0x07` | `0x00 ~ 0x1F` | Total samples = (AFE_Sample_Cycle + 1) * 8. |
| `0x0D` | 1 Byte | **RW** | `Area1_K1_Delay` | Area 1 Charge Delay Time | `0x10` | `0x08 ~ 0xFF` | Charge delay parameter for sensing zone 1. |
| `0x0E` | 1 Byte | **RW** | `Area1_K2_Delay` | Area 1 Sample Delay Time | `0x10` | `0x08 ~ 0xFF` | Sample delay parameter for sensing zone 1. |
| `0x0F` | 1 Byte | **RW** | `Area2_K1_Delay` | Area 2 Charge Delay Time | `0x14` | `0x08 ~ 0xFF` | Charge delay parameter for sensing zone 2. |
| `0x10` | 1 Byte | **RW** | `Area2_K2_Delay` | Area 2 Sample Delay Time | `0x14` | `0x08 ~ 0xFF` | Sample delay parameter for sensing zone 2. |
| `0x11` | 1 Byte | **RW** | `Area3_K1_Delay` | Area 3 Charge Delay Time | `0x10` | `0x08 ~ 0xFF` | Charge delay parameter for sensing zone 3. |
| `0x12` | 1 Byte | **RW** | `Area3_K2_Delay` | Area 3 Sample Delay Time | `0x10` | `0x08 ~ 0xFF` | Sample delay parameter for sensing zone 3. |
| `0x13` | 1 Byte | **RW** | `Area2_K_Cnt` | Area 2 Start Channel Index | `0` | `0 ~ 0x2D` | Start channel index for Zone 2 (Disabled if 0). |
| `0x14` | 1 Byte | **RW** | `Area3_K_Cnt` | Area 3 Start Channel Index | `0` | `0 ~ 0x2D` | Start channel index for Zone 3 (Disabled if 0). |
| `0x15` | 1 Byte | **RW** | `AFE_Sample_Mode` | AFE Sampling Mode | `0x01` | `0x00 ~ 0x01` | 0x00: Single-ended sampling.<br>0x01: Differential / Double-sided sampling. |
| `0x16` | 1 Byte | **RW** | `AFE_Split_En` | AFE Split Function Enable | `0x00` | `0x00 ~ 0x01` | 0x00: Disable Split.<br>0x01: Enable Split. |
| `0x17` | 1 Byte | **RW** | `AFE_Split_Sel` | AFE Split Mode Selection | `0x00` | `0x00 ~ 0x01` | 0x00: AFE outputs ACC, A (1st half sum), B (2nd half sum).<br>0x01: AFE outputs ACC, abs(A - B). |
| `0x18` | 1 Byte | **RW** | `Water_Proof_Level` | Waterproof Level | `0x03` | `0x00 ~ 0x03` | 0x00: Disabled<br>0x01: Level 1<br>0x02: Level 2<br>0x03: Full waterproof mode. |
| `0x19` | 1 Byte | **RW** | `Close_Mode_EN` | Proximity Mode Enable | `0x00` | `0x00 ~ 0x01` | 0x00: Disable close mode.<br>0x01: Enable close mode (requires FACE_DETECTION). |
| `0x1A` | 1 Byte | **RW** | `VDD5_Value` | Internal VDD5 Voltage Level | `0x03` | `0x00 ~ 0x07` | 0: 4.1V, 1: 4.4V, 2: 4.7V, 3: 5.0V, 4: 5.3V, 5: 5.5V, 6: 5.8V, 7: 6.2V. |
| `0x1B` | 1 Byte | **RW** | `CF_Value` | Feedback Capacitor CF Range | `0x30` | `0x00 ~ 0x3F` | Analog front-end feedback capacitor tuning range (0x00 - 0x3F). |
| `0x1C` | 1 Byte | **RW** | `Auto_CLB_Sample_Cycles` | Auto-Calibration Sample Cycles | `0x00` | `0x00 ~ 0x03` | 0: 8 scans, 1: 16 scans, 2: 24 scans, 3: 32 scans. |
| `0x1D` | 1 Byte | **RW** | `2nd_Auto_CLB` | Secondary Auto-Calibration | `0x00` | `0x00 ~ 0x01` | 0x00: Disable secondary calibration.<br>0x01: Enable secondary calibration. |
| `0x1E` | 1 Byte | **RW** | `Mid5_En` | Median-5 Filter Algorithm | `0x00` | `0x00 ~ 0x01` | 0x00: Disable Mid5 filter.<br>0x01: Enable Mid5 filter. |
| `0x1F` | 1 Byte | **RW** | `NLC_En` | Non-Linear Compensation (NLC) | `0x00` | `0x00 ~ 0x01` | 0x00: Disable NLC.<br>0x01: Enable NLC. |
| `0x20` | 1 Byte | **RW** | `NLC_THR` | NLC Threshold | `0x1c` | `0x00 ~ 0xFF` | NLC threshold = NLC_THR << 2. |
| `0x21` | 1 Byte | **RW** | `IIR_En` | IIR Filter Algorithm | `0x00` | `0x00 ~ 0x01` | 0x00: Disable IIR filter.<br>0x01: Enable IIR filter. |
| `0x22` | 1 Byte | **RW** | `Sync_Mode` | Vsync / Hsync Sync Selection | `0x00` | `0x00 ~ 0x03` | 0x00: Sync off<br>0x01: Vsync only<br>0x02: Hsync only<br>0x03: Vsync + Hsync. |
| `0x23～0x24` | 2 Bytes | **RW** | `Vsync_Width` | Vsync Pulse Width (2 Bytes) | `0x9AF2` | `0x0000 ~ 0xFFFF` | Vsync duration: Tvsync = Vsync_Width / adcclk. |
| `0x25` | 1 Byte | **RW** | `Vsync_Front_Porch` | Vsync Front Porch Time | `0x20` | `0x00 ~ 0xFF` | Tvfp = (Vsync_Front_Porch << 4) / adcclk. |
| `0x26` | 1 Byte | **RW** | `Vsync_Back_Porch` | Vsync Back Porch Time | `0x20` | `0x00 ~ 0xFF` | Tvbp = (Vsync_Back_Porch << 4) / adcclk. |
| `0x27` | 1 Byte | **RW** | `Hsync_Offset1` | Area 1 Hsync Offset Delay | `0x20` | `0x00 ~ 0xFF` | Thsyncoff1 = (Hsync_Offset1 << 2) / adcclk. |
| `0x28` | 1 Byte | **RW** | `Hsync_Offset2` | Area 2 Hsync Offset Delay | `0x20` | `0x00 ~ 0xFF` | Thsyncoff2 = (Hsync_Offset2 << 2) / adcclk. |
| `0x29` | 1 Byte | **RW** | `Hsync_Offset3` | Area 3 Hsync Offset Delay | `0x20` | `0x00 ~ 0xFF` | Thsyncoff3 = (Hsync_Offset3 << 2) / adcclk. |
| `0x2A` | 1 Byte | **RW** | `Hsync_Cycle` | Hsync Sampling Period | `0x00` | `0x00 ~ 0x01` | 0x00: 1 Hsync per sample.<br>0x01: 2 Hsync per sample. |
| `0x2B` | 1 Byte | **RW** | `SSCG_Mode` | Spread Spectrum Clock Mode | `0x00` | `0x00 ~ 0x03` | 0x00: Off.<br>0x01: (Fsys * 99%) -> Fsys -> (Fsys * 99%).<br>0x02: (Fsys * 101%) -> Fsys -> (Fsys * 101%).<br>0x03: Full spectrum spread. |
| `0x2C` | 1 Byte | **RW** | `SSCG_Delay` | SSCG Step Delay | `0x00` | `0x00 ~ 0x03` | 0: 5us, 1: 10us, 2: 15us, 3: 20us. |
| `0x2D` | 1 Byte | **RW** | `Big_Noise_Mask` | Large Noise Region Masking | `0x00` | `0x00 ~ 0x01` | 0x00: Do not mask.<br>0x01: Mask large noise zones. |
| `0x2E` | 1 Byte | **RW** | `Big_Noise_Dummy` | Large Noise Dummy Insert | `0x00` | `0x00 ~ 0x01` | 0x00: No dummy.<br>0x01: Insert 1 Hsync dummy cycle. |
| `0x2F` | 1 Byte | **RO** | `Chip_Type` | IC Model Code (RO) | `0x00` | `0x00 ~ 0x03` | 0x00: FT6236G<br>0x01: FT6336G<br>0x02: FT6336U<br>0x03: FT6436U. |
| `0x30` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0x31` | 1 Byte | **RW** | `Channel_Order_addr_R` | Channel Order Read Start Address | `0x00` | `0x00 ~ 0xFF` | Initial counter offset for reading channel mapping sequence. |
| `0x32` | 1 Byte | **RW** | `CB_addr_W` | Base Cap (CB) Write Start Addr | `0x00` | `0x00 ~ 0xFF` | Initial counter offset for writing baseline capacitance. |
| `0x33` | 1 Byte | **RW** | `CB_addr_R` | Base Cap (CB) Read Start Addr | `0x00` | `0x00 ~ 0xFF` | Initial counter offset for reading baseline capacitance. |
| `0x34` | 1 Byte | **RW** | `RawData_addr_R` | RawData Read Start Address | `0x00` | `0x00 ~ 0xFF` | Initial counter offset for reading raw sensor ADC counts. |
| `0x35` | 1 Byte | **RO** | `RawData_buf` | RawData Read Buffer (RO) | `NA` | `0x00 ~ 0xFF` | Raw ADC counts buffer (2 bytes per channel). See Section 3.2. |
| `0x36` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0x37` | 1 Byte | **RO** | `Channel_Order_buf` | Channel Order Buffer (RO) | `NA` | `0x00 ~ 0x3F` | Channel mapping order buffer. See Section 3.2. |
| `0x38` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0x39` | 1 Byte | **RW** | `CB_buf` | Base Cap (CB) Buffer (RW) | `NA` | `0x00 ~ 0xFF` | Base capacitance data buffer (2 bytes per channel). See Section 3.2. |
| `0x3A～0x40` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0x41` | 1 Byte | **RW** | `Test_1xCb_8xCb` | Test Mode Cb Selection | `0x01` | `0x00 ~ 0x01` | 0x00: 1xCb (identical to normal mode scan).<br>0x01: 8xCb (close mode scan with 8x Cb accumulation). |
| `0x42` | 1 Byte | **RW** | `ID_G_MODE12_GET_RAW` | Post-Calibration Scan Report | `0x00` | `0x00 ~ 0x01` | 0x00: Do not scan after calibration.<br>0x01: Perform scan and report done flag after calibration. |
| `0x3A～0xAD` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0xAE` | 1 Byte | **RW** | `ID_G_FACTORY_MODE` | Factory Mode Status & Type | `0x00` | `0x00 ~ 0x72` | Bit 7: 0 = Calibration in progress, 1 = Calibration completed.<br>Bits [6:0]: 0x00: F_NORMAL, 0x01: F_TESTMODE_1, 0x02: F_TESTMODE_2. |
| `0xAF～0xF3` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |
| `0xF4` | 1 Byte | **RO** | `RS_DATA_BUF` | Micro-Short Test Data Buffer | `NA` | `0x00 ~ 0xFF` | Short circuit measurement data buffer. |
| `0xF5～0xFF` |  | **** | `Reserved` | Reserved | `` | `` | Reserved for internal controller logic. |

### 3.2 Factory Mode Operation Instructions

#### 1. Reading Channel Order Sequence
1. Write the starting channel offset into register `0x31` (`Channel_Order_addr_R`), e.g., write `0x00` to start from channel 0: `I2C Write: [0x31, 0x00]`.
2. Read from register `0x37` (`Channel_Order_buf`). The total bytes to read equals: `N = TP_Channel_Num + TP_Key_Num`.

#### 2. Reading Raw ADC Data (RawData)
1. Write the starting channel offset into register `0x34` (`RawData_addr_R`), e.g., write `0x00` for channel 0: `I2C Write: [0x34, 0x00]`.
2. Read from register `0x35` (`RawData_buf`). Each channel requires 2 bytes (Big-Endian 16-bit ADC value). Total bytes to read = `2 * N` (where `N` is total channels). Bytes `2*N` and `(2*N)+1` contain the proximity close-mode raw data.

#### 3. Reading / Writing Base Capacitance (CB)
1. Write the starting channel offset into register `0x33` (`CB_addr_R`) for read, or `0x32` (`CB_addr_W`) for write.
2. Access register `0x39` (`CB_buf`). Each channel occupies 2 bytes.

#### 4. Micro-Short (RS) Test Data Readout
1. Write `0x01` to register `0x07` (`RS_MODE_EN`) to enable micro-short testing.
2. Write the start offset into register `0x34` (`RawData_addr_R`).
3. Read data from register `0xF4` (`RS_DATA_BUF`). Each channel corresponds to 2 bytes.

---

## 4. Quick Programming Reference for Embedded Developers

### Coordinate Extraction Formula (Point 1)
```c
// Read 5 consecutive bytes from register 0x02
uint8_t reg = 0x02; // TD_STATUS
uint8_t buf[5];
i2c_master_transmit_receive(touch_dev, &reg, 1, buf, 5, 100);

uint8_t touch_count = buf[0] & 0x0F; // 0, 1, or 2 points
uint8_t event_flag  = (buf[1] >> 6) & 0x03; // 0: Press Down, 1: Lift Up, 2: Contact
uint16_t x_coord    = ((uint16_t)(buf[1] & 0x0F) << 8) | buf[2];
uint8_t touch_id    = (buf[3] >> 4) & 0x0F;
uint16_t y_coord    = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[4];
```

### Chip ID Verification Table
| Register Address | Name | Expected Value (FT6336U) | Description |
|:---:|:---:|:---:|---|
| `0xA8` | `ID_G_FOCALTECH_ID` | `0x11` | FocalTech Vendor ID |
| `0xA3` | `ID_G_CIPHER_HIGH` | `0x64` | Chip Series High Byte |
| `0x9F` | `ID_G_CIPHER_MID` | `0x26` | Chip Series Mid Byte |
| `0xA0` | `ID_G_CIPHER_LOW` | `0x02` | Chip Model Code (0x02 = FT6336U) |
