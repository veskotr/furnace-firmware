# MODBUS Temperature Monitoring System — Study Plan

## Progress Tracker

- [x] **Section 1** — Physical Hardware & Wiring
- [x] **Section 2** — MODBUS RTU Protocol Fundamentals
- [x] **Section 3** — MS9024 Register Map & Configuration
- [x] **Section 4** — ESP-IDF UART / RS485 Driver Layer
- [x] **Section 5** — Firmware Implementation Walkthrough
- [x] **Section 6** — Task Lifecycle & Periodic Reading
- [x] **Section 7** — Integration with the Rest of the Firmware
- [x] **Section 8** — Kconfig Configuration & Build Integration

---

## Section 1 — Physical Hardware & Wiring

### 1.1 Components

| Component | Role |
|-----------|------|
| **ESP32** | Microcontroller running FreeRTOS firmware; acts as MODBUS master |
| **MS9024** | Industrial temperature transmitter; acts as MODBUS slave |
| **PT100 RTD** | 4-wire platinum resistance temperature detector connected to MS9024 |
| **RS485 transceiver** | Converts UART (TTL) signals to differential RS485 signals. The ESP32 UART peripheral has built-in RS485 half-duplex support with automatic DE (Driver Enable) pin control |

### 1.2 Wiring

```
ESP32                   RS485 bus                MS9024
──────                  ─────────                ──────
GPIO 27 (TX) ──────►  A/B differential  ──────► RS485 A/B
GPIO 26 (RX) ◄──────  A/B differential  ◄────── RS485 A/B
GPIO 25 (DE) ──────►  Driver Enable pin
```

- **TX (GPIO 27):** Transmit data to the RS485 bus
- **RX (GPIO 26):** Receive data from the RS485 bus
- **DE (GPIO 25):** Driver Enable — when HIGH the transceiver transmits; when LOW it listens. The ESP32 RS485 half-duplex mode toggles this automatically

### 1.3 Half-Duplex Operation

RS485 is a shared bus — only one device can talk at a time:

1. **Master transmits:** ESP32 asserts DE HIGH → sends request bytes → waits for TX complete → DE goes LOW
2. **Slave responds:** MS9024 drives the bus with its response
3. **Master receives:** ESP32 reads the response bytes from UART RX

---

## Section 2 — MODBUS RTU Protocol Fundamentals

### 2.1 What is MODBUS RTU?

MODBUS is a **master/slave** (client/server) serial communication protocol. **RTU** (Remote Terminal Unit) is the binary variant — data is sent as raw bytes (as opposed to MODBUS ASCII which uses hex-encoded text).

Key characteristics:
- **One master, one or more slaves** — the master always initiates communication
- **Request/Response** — the master sends a request frame; the addressed slave sends a response frame
- **Each slave has a unique address** (1–247)
- **CRC-16 error detection** on every frame

### 2.2 RTU Frame Structure

Every MODBUS RTU frame has this structure:

```
┌──────────┬───────────────┬──────────────────┬───────────┐
│ Address  │ Function Code │    Data          │  CRC-16   │
│ (1 byte) │  (1 byte)     │  (N bytes)       │ (2 bytes) │
└──────────┴───────────────┴──────────────────┴───────────┘
```

| Field | Size | Description |
|-------|------|-------------|
| **Address** | 1 byte | Slave address (1–247). Address 0 = broadcast (no response) |
| **Function Code** | 1 byte | What operation to perform (read, write, etc.) |
| **Data** | Variable | Parameters specific to the function code |
| **CRC-16** | 2 bytes | Error check. Low byte first, then high byte |

### 2.3 Common Function Codes

| Code | Name | Description |
|------|------|-------------|
| `0x03` | Read Holding Registers | Read one or more 16-bit registers |
| `0x06` | Write Single Register | Write one 16-bit register |
| `0x10` | Write Multiple Registers | Write two or more consecutive 16-bit registers |

### 2.4 Function Code 0x03 — Read Holding Registers

This is the most-used function in our system. It reads one or more consecutive 16-bit registers from the slave.

#### Request Frame (Master → Slave)

```
┌─────────┬──────┬────────────┬────────────┬────────────┬────────────┬──────┬──────┐
│  Byte 0 │  B1  │    B2      │    B3      │    B4      │    B5      │  B6  │  B7  │
│  Slave  │ Func │ Start Reg  │ Start Reg  │ Num Regs   │ Num Regs   │ CRC  │ CRC  │
│  Addr   │ 0x03 │  (Hi)      │  (Lo)      │  (Hi)      │  (Lo)      │ (Lo) │ (Hi) │
└─────────┴──────┴────────────┴────────────┴────────────┴────────────┴──────┴──────┘
```

**Total: always 8 bytes**

#### Response Frame (Slave → Master)

```
┌─────────┬──────┬────────────┬──────────────────────────┬──────┬──────┐
│  Byte 0 │  B1  │    B2      │  B3 … B(2+N)            │ CRC  │ CRC  │
│  Slave  │ Func │ Byte Count │  Register Data           │ (Lo) │ (Hi) │
│  Addr   │ 0x03 │  (N)       │  (N bytes, 2 per reg)    │      │      │
└─────────┴──────┴────────────┴──────────────────────────┴──────┴──────┘
```

**Total: 5 + (num_regs × 2) bytes**

#### Concrete Example: Read PV Temperature (Register 728, 2 registers)

Our firmware reads the Process Value (temperature) as an IEEE-754 float stored across two consecutive registers starting at address 728.

**Request (Master → MS9024):**

```
Byte:  01   03   02 D8   00 02   XX XX
       ──   ──   ─────   ─────   ─────
       │    │      │       │       └─ CRC-16 (calculated over bytes 0–5)
       │    │      │       └─ Number of registers: 0x0002 = 2 registers (= 4 bytes = 1 float)
       │    │      └─ Starting register: 0x02D8 = 728 decimal
       │    └─ Function code: 0x03 = Read Holding Registers
       └─ Slave address: 0x01 = device 1
```

Breakdown:
| Byte(s) | Hex | Decimal | Meaning |
|---------|-----|---------|---------|
| `01` | 0x01 | 1 | Slave address — we're talking to device #1 |
| `03` | 0x03 | 3 | Function code — Read Holding Registers |
| `02 D8` | 0x02D8 | 728 | Starting register address (PV in float format) |
| `00 02` | 0x0002 | 2 | Read 2 registers (each register is 16-bit; 2 registers = 32 bits = 1 IEEE-754 float) |
| `XX XX` | — | — | CRC-16 checksum (low byte first) |

**Response (MS9024 → Master) — example for 25.50 °C:**

```
Byte:  01   03   04   A4 CC   41 CC   XX XX
       ──   ──   ──   ─────   ─────   ─────
       │    │    │      │       │       └─ CRC-16
       │    │    │      │       └─ Register 729 data (high word): 0x41CC
       │    │    │      └─ Register 728 data (low word): 0xA4CC
       │    │    └─ Byte count: 4 bytes of register data follow
       │    └─ Function code: 0x03 (echoed back)
       └─ Slave address: 0x01 (echoed back)
```

Breakdown:
| Byte(s) | Hex | Meaning |
|---------|-----|---------|
| `01` | 0x01 | Slave address echoed back — confirms device #1 is responding |
| `03` | 0x03 | Function code echoed back — confirms this is a read-registers response |
| `04` | 0x04 | Byte count — 4 bytes of data follow (2 registers × 2 bytes each) |
| `A4 CC` | 0xA4CC | Register 728 — **low word** of the float (bytes C, D in CDAB order) |
| `41 CC` | 0x41CC | Register 729 — **high word** of the float (bytes A, B in CDAB order) |
| `XX XX` | — | CRC-16 checksum |

#### Decoding the Float (CDAB Word Order)

The MS9024 stores IEEE-754 floats in **CDAB** (word-swapped) byte order:

```
Register 728 (first)  = low word  = 0xA4CC  → bytes C, D
Register 729 (second) = high word = 0x41CC  → bytes A, B

Reassemble as ABCD: 0x41CC A4CC

IEEE-754 decode:
  Sign:     0 (positive)
  Exponent: 10000011 (131) → 131 - 127 = 4
  Mantissa: 1.10011001010010011001100
  Value:    1.10011001010010011001100 × 2⁴ = 25.5 °C ✓
```

In C code (from our firmware):
```c
/* d[0]=0xA4, d[1]=0xCC, d[2]=0x41, d[3]=0xCC */
uint32_t raw = ((uint32_t)d[2] << 24) | ((uint32_t)d[3] << 16) |
               ((uint32_t)d[0] << 8)  |  (uint32_t)d[1];
/* raw = 0x41CCA4CC → 25.5 °C */
```

> **⚠ Common Pitfall:** If you don't swap the words, you get `0xA4CC41CC` which decodes to approximately `-1.57 × 10⁻¹⁷` — a nonsense value, or `-0.0` depending on the actual bytes. This is exactly what happened in the colleague's code that wasn't working.

### 2.5 Function Code 0x06 — Write Single Register

Used to configure the MS9024 (sensor type, wiring mode, etc.).

#### Request Frame (Master → Slave)

```
┌─────────┬──────┬────────────┬────────────┬────────────┬────────────┬──────┬──────┐
│  Byte 0 │  B1  │    B2      │    B3      │    B4      │    B5      │  B6  │  B7  │
│  Slave  │ Func │  Register  │  Register  │   Value    │   Value    │ CRC  │ CRC  │
│  Addr   │ 0x06 │   (Hi)     │   (Lo)     │   (Hi)     │   (Lo)     │ (Lo) │ (Hi) │
└─────────┴──────┴────────────┴────────────┴────────────┴────────────┴──────┴──────┘
```

**Total: always 8 bytes (same as request!)**

#### Response Frame (Slave → Master)

On success, the slave **echoes back the exact same 8 bytes**. This makes verification simple — just compare request and response.

#### Concrete Example: Set Sensor Type to PT100 (α=0.00385)

```
Byte:  01   06   00 1C   00 10   XX XX
       ──   ──   ─────   ─────   ─────
       │    │      │       │       └─ CRC-16
       │    │      │       └─ Value: 0x0010 = 16 decimal = PT100 (α=0.00385)
       │    │      └─ Register address: 0x001C = 28 decimal (SENS register)
       │    └─ Function code: 0x06 = Write Single Register
       └─ Slave address: 0x01
```

Breakdown:
| Byte(s) | Hex | Decimal | Meaning |
|---------|-----|---------|---------|
| `01` | 0x01 | 1 | Slave address |
| `06` | 0x06 | 6 | Function code — Write Single Register |
| `00 1C` | 0x001C | 28 | Register 28 — sensor type (SENS) |
| `00 10` | 0x0010 | 16 | Value 16 = PT100 with α=0.00385 coefficient |
| `XX XX` | — | — | CRC-16 |

#### Concrete Example: Set Wiring Mode to 4-Wire

```
Byte:  01   06   00 20   00 04   XX XX
       ──   ──   ─────   ─────   ─────
       │    │      │       │       └─ CRC-16
       │    │      │       └─ Value: 0x0004 = 4 = 4-wire RTD mode
       │    │      └─ Register: 0x0020 = 32 decimal (WIRE register)
       │    └─ Function code: 0x06
       └─ Slave address: 0x01
```

### 2.6 Exception Responses

If the slave cannot process a request, it responds with an **exception**:

```
┌─────────┬──────────────────┬────────────────┬──────┬──────┐
│  Slave  │ Function Code    │ Exception Code │ CRC  │ CRC  │
│  Addr   │  + 0x80          │  (1 byte)      │ (Lo) │ (Hi) │
└─────────┴──────────────────┴────────────────┴──────┴──────┘
```

The function code has bit 7 set (OR'd with `0x80`). For example, if you send function `0x03` and the slave returns `0x83`, that's an exception.

| Exception Code | Name | Meaning |
|---------------|------|---------|
| `0x01` | Illegal Function | Function code not supported |
| `0x02` | Illegal Data Address | Register address doesn't exist |
| `0x03` | Illegal Data Value | Value out of allowed range |
| `0x04` | Server Device Failure | Internal slave error |

Example exception response:
```
Byte:  01   83   02   XX XX
       ──   ──   ──   ─────
       │    │    │     └─ CRC-16
       │    │    └─ Exception code 0x02 = Illegal Data Address (register doesn't exist!)
       │    └─ 0x83 = 0x03 + 0x80 → exception for function 0x03
       └─ Slave address: 0x01
```

Our firmware detects this with:
```c
if (resp[1] & 0x80) {
    /* This is an exception response! */
    uint8_t exception_code = resp[2];
}
```

### 2.7 CRC-16 Calculation

Every frame ends with a 2-byte CRC (Cyclic Redundancy Check) computed over all preceding bytes.

- **Polynomial:** 0xA001 (bit-reversed form of 0x8005)
- **Initial value:** 0xFFFF
- **Byte order:** Low byte first, high byte second

```c
static uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;          /* 1. Start with 0xFFFF             */
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];             /* 2. XOR each byte into CRC        */
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001)        /* 3. For each bit:                 */
                crc = (crc >> 1) ^ 0xA001;  /*  if LSB=1, shift & XOR   */
            else
                crc >>= 1;                  /*  if LSB=0, just shift    */
        }
    }
    return crc;                     /* 4. Result: low byte, then high   */
}
```

The receiver computes the CRC over the received data (excluding the 2 CRC bytes), then compares it against the received CRC. If they don't match, the frame is corrupted and must be discarded.

### 2.8 Timing & Framing

MODBUS RTU uses **silence gaps** to delimit frames (no start/end byte markers):

- **3.5 character times** of silence = frame boundary
- At 9600 baud with 8N1 (10 bits/char): 1 char ≈ 1.04 ms → 3.5 chars ≈ **3.6 ms**
- The UART driver and hardware handle this for us — we just read/write byte arrays

### 2.9 Summary of a Complete Read Transaction

```
Time ──────────────────────────────────────────────────────►

Master (ESP32)                                Slave (MS9024)
     │                                              │
     │  ┌─────────────────────────────────┐         │
     ├──│ 01 03 02D8 0002 [CRC]          │────────►│
     │  │ "Read 2 regs starting at 728"   │         │
     │  └─────────────────────────────────┘         │
     │                                              │
     │         (slave processes request)            │
     │                                              │
     │  ┌─────────────────────────────────┐         │
     │◄─│ 01 03 04 A4CC 41CC [CRC]       │─────────┤
     │  │ "Here are 4 bytes of data"      │         │
     │  └─────────────────────────────────┘         │
     │                                              │
     │  Decode CDAB float: 0x41CCA4CC              │
     │  = 25.50 °C ✓                                │
```

---

## Section 3 — MS9024 Register Map & Configuration

### 3.1 MS9024 Overview

The MS9024 is an industrial temperature transmitter that:
- Accepts RTD (PT100) or thermocouple inputs
- Converts the analog sensor reading to digital
- Exposes the measurement (and configuration) via MODBUS RTU over RS485
- Outputs a 4-20 mA analog signal proportional to temperature

### 3.2 Key Registers

| Register | Name | Type | Description |
|----------|------|------|-------------|
| 28 | SENS | uint16 (LSByte) | Sensor type: 16=PT100(0.00385), 20=PT100(0.00392), 21=PT100(0.00391) |
| 32 | WIRE | uint16 (LSByte) | Wiring mode: 2=2-wire, 3=3-wire, 4=4-wire |
| 26 | FIN | uint16 (LSByte) | Input filter: 1=heaviest … 127=off (no filtering) |
| 126 | FW | uint16 | Firmware version |
| 447 | CF | coil / uint16 | 0=Celsius, 1=Fahrenheit |
| 524 | IN_OFFSET | float (2 regs) | Input offset calibration (°C) |
| 728 | PV | float (2 regs) | **Process Value — the actual temperature reading** |
| 730 | T2 | float (2 regs) | Cold junction (board) temperature |
| 726 | AOUT | float (2 regs) | Analog output value |

### 3.3 Integer vs Float Registers

- **Integer registers** (like SENS, WIRE, FIN): single 16-bit register, value in the **least significant byte** (LSByte)
- **Float registers** (like PV, T2): span **two consecutive** 16-bit registers, encoded as IEEE-754 float in CDAB word order

### 3.4 Auto-Configuration on Startup

Our firmware reads the current sensor config and auto-corrects if it doesn't match the expected values from Kconfig:

1. Read SENS register → if not PT100, write the correct value
2. Read WIRE register → if not 4-wire, write the correct value
3. Read FIN register → if out of range, reset to 127 (filter off)
4. Dump full config to log for debugging

---

## Section 4 — ESP-IDF UART / RS485 Driver Layer

### 4.1 UART Configuration

```c
const uart_config_t uart_cfg = {
    .baud_rate  = 9600,             /* Must match MS9024 setting     */
    .data_bits  = UART_DATA_8_BITS, /* Standard 8-bit data           */
    .parity     = UART_PARITY_DISABLE, /* No parity (8N1)            */
    .stop_bits  = UART_STOP_BITS_1, /* 1 stop bit                    */
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};
```

This configures **8N1** (8 data bits, no parity, 1 stop bit) at 9600 baud — the most common MODBUS RTU serial configuration.

### 4.2 RS485 Half-Duplex Mode

```c
uart_set_mode(UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
```

This tells the ESP32 UART hardware to:
- Automatically assert the DE (Driver Enable) pin HIGH before transmitting
- Automatically de-assert DE LOW after the last byte is sent
- This eliminates manual GPIO toggling and timing issues

### 4.3 Pin Assignment

```c
uart_set_pin(UART_PORT, TX_PIN, RX_PIN, DE_PIN, UART_PIN_NO_CHANGE);
```

The RTS pin parameter is used for the DE pin in RS485 mode.

### 4.4 Send / Receive Flow

1. **Flush** any stale data in the RX buffer: `uart_flush_input()`
2. **Transmit** the request: `uart_write_bytes()`
3. **Wait** for TX to complete: `uart_wait_tx_done()` — critical! The DE pin stays HIGH until this returns
4. **Receive** the response: `uart_read_bytes()` with a timeout

---

## Section 5 — Firmware Implementation Walkthrough

### 5.1 File Structure

```
components/modbus_temp_monitor/
├── include/
│   └── modbus_temp_monitor.h    ← Public API (init / shutdown)
├── src/
│   └── modbus_temp_monitor.c    ← All implementation
├── Kconfig                      ← Configuration options
└── CMakeLists.txt               ← Build integration
```

### 5.2 Why Raw MODBUS Instead of esp_modbus?

We implement the MODBUS protocol manually (raw byte construction + CRC) rather than using Espressif's `esp_modbus_master` component because:

1. **Full control** over byte-level timing and framing
2. **No abstraction surprises** — we know exactly what goes on the wire
3. **CDAB float handling** done explicitly — no guessing what the library does internally
4. **Simpler debugging** — we log every TX and RX byte
5. **Smaller footprint** — no large Modbus stack dependency

### 5.3 Key Functions

| Function | Purpose |
|----------|---------|
| `modbus_crc16()` | Calculate CRC-16 for a byte buffer |
| `init_rs485_uart()` | Configure UART2 in RS485 half-duplex mode |
| `modbus_read_holding_registers()` | Build FC03 request, send, receive, validate CRC, extract data |
| `modbus_write_single_register()` | Build FC06 request, send, verify echo response |
| `ms9024_read_uint16()` | Read a single 16-bit register |
| `ms9024_read_float()` | Read 2 registers and decode CDAB float |
| `ms9024_write_and_verify()` | Write a register and read it back to confirm |
| `ms9024_log_config()` | Dump all MS9024 configuration registers to the log |
| `modbus_temp_read_task()` | FreeRTOS task: auto-configure, then read temperature periodically |

### 5.4 Error Handling in `modbus_read_holding_registers()`

The function validates the response step by step:

1. **Timeout** — no bytes received → `ESP_ERR_TIMEOUT`
2. **Short response** — fewer bytes than expected → `ESP_ERR_INVALID_SIZE`
3. **Exception** — function code has bit 7 set → log exception code, return `ESP_FAIL`
4. **Wrong slave address** — response from different device → `ESP_FAIL`
5. **Wrong function code** — unexpected response type → `ESP_FAIL`
6. **Byte count mismatch** — data length doesn't match → `ESP_FAIL`
7. **CRC mismatch** — corrupted frame → `ESP_FAIL`
8. **All checks pass** — copy data payload to output buffer → `ESP_OK`

### 5.5 Float Decoding & Validation in `ms9024_read_float()`

After decoding the CDAB word order, the function rejects invalid readings:

| Check | Reason |
|-------|--------|
| `isnan(val)` | Not-a-Number — corrupted or meaningless register data |
| `isinf(val)` | Infinity — overflow or sensor fault |
| `raw == 0x80000000` | Negative zero (`-0.0`) — MS9024 returns this when no measurement is ready |
| `val < -200` or `val > 1500` | Out of PT100 physical range — noise or misconfiguration |

---

## Section 6 — Task Lifecycle & Periodic Reading

### 6.1 Task Startup Sequence

```
modbus_temp_monitor_init()
    │
    ├── init_rs485_uart()          ← Configure UART hardware
    │
    └── xTaskCreate(modbus_temp_read_task)
            │
            ├── vTaskDelay(1000ms)     ← Let MS9024 settle after power-on
            │
            ├── Auto-correct SENS      ← Read, compare with Kconfig, write if different
            ├── Auto-correct WIRE      ← Same
            ├── Reset FIN if invalid   ← Same
            │
            ├── ms9024_log_config()    ← Full config dump to log
            │
            └── while (s_running) {
                    vTaskDelay(READ_INTERVAL)
                    ms9024_read_float(PV_REG, &temperature)
                    → post event on success
                    → count errors on failure, alarm after MAX_ERRORS
                }
```

### 6.2 Error Recovery

- **Consecutive error counter** tracks how many reads in a row have failed
- After `MAX_ERRORS` (default 5) consecutive failures:
  - Logs a diagnostic message with wiring details
  - Posts a `FURNACE_ERROR_EVENT` so the system can react (alarm, display warning, etc.)
  - Resets the counter and **keeps retrying** — the MS9024 might recover
- If a read succeeds after errors, logs "recovered" and resets the counter

### 6.3 Shutdown

```c
modbus_temp_monitor_shutdown()
    → sets s_running = false
    → task detects this on next iteration and self-deletes
```

---

## Section 7 — Integration with the Rest of the Firmware

### 7.1 Event-Based Architecture

The firmware uses ESP-IDF's event system. The MODBUS temperature monitor **emits the same events** as the SPI thermocouple processor would:

```c
temp_processor_data_t data = {
    .average_temperature = temperature,
    .valid               = true,
};
event_manager_post_blocking(
    TEMP_PROCESSOR_EVENT,
    PROCESS_TEMPERATURE_EVENT_DATA,
    &data, sizeof(data));
```

This means the **coordinator** and **HMI** don't know or care whether the temperature came from an SPI thermocouple or a MODBUS RTD — they just listen for `TEMP_PROCESSOR_EVENT`.

### 7.2 Event Flow

```
MS9024 (MODBUS slave)
    │
    │  RS485
    ▼
modbus_temp_monitor
    │
    │  TEMP_PROCESSOR_EVENT
    ▼
event_manager
    │
    ├──► coordinator    (PID control, safety checks)
    └──► HMI           (display temperature on screen)
```

### 7.3 Error Events

On repeated MODBUS failures, the monitor posts:

```c
furnace_error_t fe = {
    .severity   = SEVERITY_ERROR,
    .source     = SOURCE_TEMP_MONITOR,
    .error_code = 0xFF,
};
event_manager_post_blocking(FURNACE_ERROR_EVENT, ...);
```

This allows the coordinator to take safety action (e.g. shut down heater if no valid temperature readings).

---

## Section 8 — Kconfig Configuration & Build Integration

### 8.1 Kconfig Options

All MODBUS parameters are configurable through `menuconfig`:

| Option | Default | Description |
|--------|---------|-------------|
| `MODBUS_TEMP_ENABLED` | n | Master enable/disable for the whole component |
| `MODBUS_TEMP_UART_PORT` | 2 | UART peripheral number (0, 1, or 2) |
| `MODBUS_TEMP_RS485_TX_PIN` | 27 | GPIO for RS485 TX |
| `MODBUS_TEMP_RS485_RX_PIN` | 26 | GPIO for RS485 RX |
| `MODBUS_TEMP_RS485_DE_PIN` | 25 | GPIO for RS485 Driver Enable |
| `MODBUS_TEMP_SLAVE_ADDR` | 1 | MODBUS slave address of the MS9024 |
| `MODBUS_TEMP_BAUD_RATE` | 9600 | Baud rate |
| `MODBUS_TEMP_PV_REGISTER` | 728 | PV register address |
| `MODBUS_TEMP_READ_INTERVAL_MS` | 1000 | How often to read temperature |
| `MODBUS_TEMP_RESPONSE_TIMEOUT_MS` | 500 | Max wait for slave response |
| `MODBUS_TEMP_MAX_CONSECUTIVE_ERRORS` | 5 | Errors before posting alarm |
| `MODBUS_TEMP_SENSOR_TYPE` | 16 | Expected sensor type (16=PT100/385) |
| `MODBUS_TEMP_WIRE_MODE` | 4 | Expected wiring mode (4=4-wire) |

### 8.2 Conditional Compilation

The entire component compiles to no-ops when disabled:

```c
#ifdef CONFIG_MODBUS_TEMP_ENABLED
    /* ... full implementation ... */
#else
    esp_err_t modbus_temp_monitor_init(void)     { return ESP_OK; }
    esp_err_t modbus_temp_monitor_shutdown(void)  { return ESP_OK; }
#endif
```

### 8.3 Build Integration

In `CMakeLists.txt`, the component declares its dependencies:

```cmake
idf_component_register(
    SRCS "src/modbus_temp_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES driver event_manager logger_component ...
)
```

---

## Appendix A — Common Code Bugs

A common failure mode when working with the MS9024:

| Issue | Wrong | Correct |
|-------|-------|---------|
| Register address | `728 + 512 = 1240` | `728` (already the correct address) |
| Float word order | Using first variant (no swap) | CDAB — swap the two 16-bit words |
| `-0.0` handling | Displayed as valid `−0.00` | Must be rejected — means "no reading" |
| Using esp_modbus | Byte/word ordering may be hidden | Know your byte order or use raw protocol |
