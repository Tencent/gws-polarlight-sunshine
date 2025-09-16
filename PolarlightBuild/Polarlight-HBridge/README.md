# HBridge

**HBridge** is a dedicated Windows EXE designed to bridge Halo (the interface layer split from [Sunshine](https://github.com/LizardByte/sunshine) (a GPLv3-licensed remote desktop streaming server))with the **Polaris** distributed platform. It exports Halo runtime events and enables bidirectional communication via **XLang-based shared memory IPC**, without compromising GPL licensing boundaries.

---

## 🔍 Purpose and Background

Sunshine is licensed under **GPL 3.0**, which requires that any linked or derivative work also be GPL-licensed. To comply:

* **HBridge** was developed as an **isolated executable file**, load `libhalo.dll`.
* It is **separately licensed under GPLv3**, and **not built using the Sunshine toolchain**.
* All IPC communication is handled via **XLang**, which serializes and deserializes all Sunshine-related data into opaque `X::Value` objects.
* On the **Polaris** side, data is accessed and interpreted only through these opaque forms, ensuring **no direct dependency or linkage to GPL code**, as required under GPL 3.0.

---

## 🛠️ Build & Toolchain

* **ThirdParty Directory**: Create a `ThirdParty/` folder adjacent to the `HBridge/` directory and clone XLang into it:

  ```bash
  git clone -b polarlight https://github.com/xlang-foundation/xlang ThirdParty/xlang
  ```

* **Language**: C++

* **Build System**: Standard CMake (same as Polaris), not Sunshine’s internal build system.

* **IDE**: Use **Visual Studio 2022** to open the `HBridge/` folder as a CMake Project.

* **Building**: Configure and build the project within Visual Studio 2022.

* **License**: [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

* **Architecture**: IPC bridge via shared memory using XLang serialization.

---

## 🔄 Call Table Interface

### 🔽 Calls from Bridge to Halo

| Function               | Description                                                            |
| ---------------------- | ---------------------------------------------------------------------- |
| `f_CloseBlockedPair`     | Close the blocked pair request.             |

### 🔼 Calls from Halo to Bridge

| Function                   | Description                                                       |
| -------------------------- | ----------------------------------------------------------------- |
| `f_OnClientAction`  | Notifies Polaris when a client connects or disconnects.           |
| `f_OnClientSendMsg` | Notifies Polaris when a Sunshine session starts or ends.          |
| `f_QueryWordbooks`         | Retrieves WordBook values by key.     |
| `f_RemoveWordbooks`        | Delete WordBook values by key.            |
| `f_QueryRuntimeInfo`     | Allows halo to query runtime status from the Polaris context. |
| `f_TriggerPairTask`     | Allows halo to trigger the pair task |
| `f_OnPortalConfigChange`     | Allows halo to pass portal config to Polaris context. |

---

## 🔐 Licensing Boundary via XLang IPC

XLang acts as an intermediary layer that:

* Encapsulates all Sunshine-side data into **opaque, serialized `X::Value` objects**.
* Prevents Polaris (or any non-GPL system) from accessing or depending on Sunshine’s internal structures.
* Provides a **shared memory IPC call table** where only serialized data flows between Sunshine and Polaris.

This design ensures:

* Full **compliance with GPLv3**.
* No linkage or derivation of Polaris components from GPL-covered Sunshine code.
* Legal separation between GPL and proprietary systems through serialized data encapsulation.

---

## 📦 Integration

* `Hbridge.exe` dynamically loads `libhalo.dll` at runtime.
* The bridge initializes a shared memory IPC table.
* XLang scripts running inside Polaris bind to this IPC table and interact with serialized data.
* No GPL code is imported, linked, or included inside Polaris or any non-GPL module.

---

## 🧱 Directory Layout

```
/HBridge
├── Main.cpp             # C++ source code for Halo integration and IPC logic
├── tests/               # Unit tests and test harnesses
├── CMakeLists.txt       # Build configuration
└── README.md            # Docs
```

---

## ⚖️ License

This project is licensed under the **GNU General Public License v3.0**.
For full license text, see [LICENSE](./LICENSE).

All external systems interacting with HBridge via shared memory and serialized `X::Value` messages remain **outside the scope of the GPL** under the definition of "mere aggregation" and opaque data exchange.